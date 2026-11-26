#include "audio/Wasapi.h"

#include <functiondiscoverykeys_devpkey.h>
#include <mmreg.h>
#include <propsys.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace au {

// KSDATAFORMAT_SUBTYPE_IEEE_FLOAT / PCM, declared locally.
static const GUID kSubFloat = {0x00000003, 0x0000, 0x0010,
                               {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
static const GUID kSubPcm = {0x00000001, 0x0000, 0x0010,
                             {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

ComScope::ComScope() {
  HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  ok = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
}
ComScope::~ComScope() {
  if (ok) CoUninitialize();
}

std::wstring HrToString(HRESULT hr) {
  wchar_t buf[64];
  swprintf(buf, 64, L"0x%08lX", (unsigned long)hr);
  return buf;
}

std::wstring Utf8ToWide(const std::string& s) {
  if (s.empty()) return {};
  int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring out((size_t)len, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), len);
  return out;
}

static IMMDeviceEnumerator* MakeEnumerator() {
  IMMDeviceEnumerator* e = nullptr;
  CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                   __uuidof(IMMDeviceEnumerator), (void**)&e);
  return e;
}

std::wstring DefaultId(EDataFlow flow) {
  IMMDeviceEnumerator* e = MakeEnumerator();
  if (!e) return {};
  std::wstring out;
  IMMDevice* dev = nullptr;
  if (SUCCEEDED(e->GetDefaultAudioEndpoint(flow, eConsole, &dev)) && dev) {
    LPWSTR id = nullptr;
    if (SUCCEEDED(dev->GetId(&id)) && id) {
      out = id;
      CoTaskMemFree(id);
    }
    dev->Release();
  }
  e->Release();
  return out;
}

static void Describe(IMMDevice* dev, Endpoint& ep) {
  DWORD st = 0;
  if (SUCCEEDED(dev->GetState(&st))) ep.state = (unsigned)st;
  LPWSTR id = nullptr;
  if (SUCCEEDED(dev->GetId(&id)) && id) {
    ep.id = id;
    CoTaskMemFree(id);
  }
  IPropertyStore* props = nullptr;
  if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props)) && props) {
    PROPVARIANT v;
    PropVariantInit(&v);
    if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR)
      ep.name = v.pwszVal;
    PropVariantClear(&v);
    PropVariantInit(&v);
    if (SUCCEEDED(props->GetValue(PKEY_DeviceInterface_FriendlyName, &v)) && v.vt == VT_LPWSTR)
      ep.device = v.pwszVal;
    PropVariantClear(&v);
    props->Release();
  }
  // Only active endpoints can be opened. Activating IAudioClient on a ghost
  // (unplugged / disabled) endpoint costs a driver round trip that can block
  // for tens of milliseconds, and the list is walked on the UI thread.
  IAudioClient* client = nullptr;
  if ((ep.state & DEVICE_STATE_ACTIVE) &&
      SUCCEEDED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&client)) &&
      client) {
    WAVEFORMATEX* fmt = nullptr;
    if (SUCCEEDED(client->GetMixFormat(&fmt)) && fmt) {
      ep.channels = fmt->nChannels;
      ep.rate = fmt->nSamplesPerSec;
      CoTaskMemFree(fmt);
    }
    client->Release();
  }
}

std::vector<Endpoint> List(EDataFlow flow) {
  std::vector<Endpoint> out;
  IMMDeviceEnumerator* e = MakeEnumerator();
  if (!e) return out;
  const std::wstring defId = DefaultId(flow);
  IMMDeviceCollection* coll = nullptr;
  if (SUCCEEDED(e->EnumAudioEndpoints(flow, DEVICE_STATEMASK_ALL, &coll)) && coll) {
    UINT count = 0;
    coll->GetCount(&count);
    for (UINT i = 0; i < count; ++i) {
      IMMDevice* dev = nullptr;
      if (FAILED(coll->Item(i, &dev)) || !dev) continue;
      Endpoint ep;
      Describe(dev, ep);
      ep.isDefault = (!defId.empty() && ep.id == defId);
      if (!ep.name.empty()) out.push_back(ep);
      dev->Release();
    }
    coll->Release();
  }
  e->Release();
  std::stable_sort(out.begin(), out.end(),
                   [](const Endpoint& a, const Endpoint& b) { return a.isDefault && !b.isDefault; });
  return out;
}

bool FindEndpoint(const std::wstring& id, EDataFlow flow, Endpoint& out) {
  for (auto& ep : List(flow))
    if (ep.id == id) {
      out = ep;
      return true;
    }
  return false;
}

// Builds a WAVEFORMATEXTENSIBLE for the requested rate / channel count.
static void MakeFormat(WAVEFORMATEXTENSIBLE& wf, unsigned rate, unsigned channels, bool asFloat) {
  ZeroMemory(&wf, sizeof(wf));
  wf.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
  wf.Format.nChannels = (WORD)channels;
  wf.Format.nSamplesPerSec = rate;
  wf.Format.wBitsPerSample = asFloat ? 32 : 16;
  wf.Format.nBlockAlign = (WORD)(channels * wf.Format.wBitsPerSample / 8);
  wf.Format.nAvgBytesPerSec = wf.Format.nSamplesPerSec * wf.Format.nBlockAlign;
  wf.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
  wf.Samples.wValidBitsPerSample = wf.Format.wBitsPerSample;
  // SPEAKER_FRONT_CENTER / SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT written
  // literally so the build never depends on which ks headers are present.
  wf.dwChannelMask = channels == 1 ? 0x4u : 0x3u;
  wf.SubFormat = asFloat ? kSubFloat : kSubPcm;
}

// Real hardware often refuses IsFormatSupported but accepts Initialize, so the
// capability list is built by actually initialising a throw-away client.
std::vector<unsigned> SupportedRates(const std::wstring& id, EDataFlow flow, unsigned channels) {
  std::vector<unsigned> rates;
  IMMDeviceEnumerator* e = MakeEnumerator();
  if (!e) return rates;
  IMMDevice* dev = nullptr;
  if (FAILED(e->GetDevice(id.c_str(), &dev)) || !dev) {
    e->Release();
    return rates;
  }
  static const unsigned candidates[] = {44100, 48000, 88200, 96000, 192000};
  for (unsigned r : candidates) {
    IAudioClient* client = nullptr;
    if (FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&client)) ||
        !client)
      continue;
    REFERENCE_TIME def = 0, mn = 0;
    client->GetDevicePeriod(&def, &mn);
    REFERENCE_TIME dur = (REFERENCE_TIME)((double)128 * 10000000.0 / (double)r);
    if (mn > 0 && dur < mn) dur = mn;
    WAVEFORMATEXTENSIBLE wf;
    MakeFormat(wf, r, channels, false); // PCM 16 is the most widely accepted
    HRESULT hr = client->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                    dur, 0, (WAVEFORMATEX*)&wf, nullptr);
    if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED) {
      UINT32 frames = 0;
      if (SUCCEEDED(client->GetBufferSize(&frames)) && frames > 0)
        hr = client->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                (REFERENCE_TIME)(10000.0 * frames / (double)r * 1000.0), 0,
                                (WAVEFORMATEX*)&wf, nullptr);
    }
    if (SUCCEEDED(hr)) {
      rates.push_back(r);
      client->Reset();
    }
    client->Release();
  }
  dev->Release();
  e->Release();
  return rates;
}

HRESULT Client::Open(const std::wstring& deviceId, EDataFlow flow, bool loopback, unsigned rate,
                     unsigned channels, unsigned bufferFrames, bool exclusive,
                     HANDLE eventHandle) {
  return OpenImpl(deviceId, flow, loopback, rate, channels, bufferFrames, exclusive, eventHandle, 0);
}

// Exclusive negotiation is fussy on real drivers: interfaces may reject the
// float mix format, answer AUDCLNT_E_UNSUPPORTED_FORMAT to IsFormatSupported
// and still accept Initialize, or demand a buffer that is an exact multiple of
// the driver period. We walk candidate formats, retry the alignment error, and
// only then fall back to a shared stream with format conversion.
HRESULT Client::OpenImpl(const std::wstring& deviceId, EDataFlow flow, bool loopback, unsigned rate,
                         unsigned channels, unsigned bufferFrames, bool exclusive,
                         HANDLE eventHandle, int depth) {
  Close();
  event_ = eventHandle;
  underruns_ = dropouts_ = 0;
  IMMDeviceEnumerator* enumerator = MakeEnumerator();
  if (!enumerator) return E_FAIL;
  IMMDevice* dev = nullptr;
  HRESULT hr = enumerator->GetDevice(deviceId.c_str(), &dev);
  enumerator->Release();
  if (FAILED(hr) || !dev) {
    err_ = L"device not found";
    return hr;
  }
  hr = dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&client_);
  dev->Release();
  if (FAILED(hr) || !client_) {
    err_ = L"cannot activate IAudioClient " + HrToString(hr);
    return hr;
  }

  WAVEFORMATEX* mix = nullptr;
  client_->GetMixFormat(&mix);
  const unsigned nativeRate = mix ? mix->nSamplesPerSec : rate;
  const unsigned nativeCh = mix ? mix->nChannels : channels;
  bool mixFloat = false;
  if (mix) {
    mixFloat = (mix->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) ||
               (mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
                ((WAVEFORMATEXTENSIBLE*)mix)->SubFormat == kSubFloat);
  }

  REFERENCE_TIME defPeriod = 0, minPeriod = 0;
  client_->GetDevicePeriod(&defPeriod, &minPeriod);
  auto periodDuration = [&](unsigned wantFrames, unsigned atRate) -> REFERENCE_TIME {
    REFERENCE_TIME want = (REFERENCE_TIME)((double)wantFrames * 10000000.0 / (double)atRate);
    if (minPeriod > 0) {
      REFERENCE_TIME n = (want + minPeriod - 1) / minPeriod;
      if (n < 1) n = 1;
      return n * minPeriod;
    }
    return want > 0 ? want : 300000;
  };

  DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
  eventDriven_ = true;
  WAVEFORMATEXTENSIBLE want;
  ZeroMemory(&want, sizeof(want));
  WAVEFORMATEX* useFormat = nullptr;
  bool useOwn = false;

  if (depth > 0) {
    // Final fallback: shared stream in the endpoint's native mix format.
    if (!mix) {
      client_->Release();
      client_ = nullptr;
      err_ = L"no mix format";
      return E_FAIL;
    }
    useFormat = mix;
    exclusive = false;
    rate_ = nativeRate;
    channels_ = nativeCh;
    float_ = mixFloat;
    bytesPer_ = mix->wBitsPerSample / 8;
  } else if (loopback) {
    if (!mix) {
      client_->Release();
      client_ = nullptr;
      err_ = L"no mix format";
      return E_FAIL;
    }
    useFormat = mix;
    flags |= AUDCLNT_STREAMFLAGS_LOOPBACK;
    exclusive = false;
    rate_ = nativeRate;
    channels_ = nativeCh;
    float_ = mixFloat;
    bytesPer_ = mix->wBitsPerSample / 8;
  } else if (exclusive) {
    struct Candidate {
      WAVEFORMATEXTENSIBLE wf;
      bool isFloat;
    };
    Candidate cands[3];
    MakeFormat(cands[0].wf, rate, channels, true); // float32
    cands[0].isFloat = true;
    MakeFormat(cands[1].wf, rate, channels, false); // 16-bit PCM
    cands[1].isFloat = false;
    MakeFormat(cands[2].wf, rate, channels, false); // 24-in-32 PCM
    cands[2].wf.Format.wBitsPerSample = 32;
    cands[2].wf.Format.nBlockAlign = (WORD)(channels * 4);
    cands[2].wf.Format.nAvgBytesPerSec =
        cands[2].wf.Format.nSamplesPerSec * cands[2].wf.Format.nBlockAlign;
    cands[2].wf.Samples.wValidBitsPerSample = 24;
    cands[2].isFloat = false;

    REFERENCE_TIME duration = periodDuration(bufferFrames, rate);
    bool opened = false;
    for (int i = 0; i < 3 && !opened; ++i) {
      for (int mode = 0; mode < 2 && !opened; ++mode) {
        const DWORD useFlags = mode == 0 ? AUDCLNT_STREAMFLAGS_EVENTCALLBACK : 0;
        hr = client_->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, useFlags, duration, 0,
                                (WAVEFORMATEX*)&cands[i].wf, nullptr);
        if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED) {
          UINT32 alignedFrames = 0;
          if (SUCCEEDED(client_->GetBufferSize(&alignedFrames)) && alignedFrames > 0) {
            duration =
                (REFERENCE_TIME)(10000.0 * 1000.0 * (double)alignedFrames / (double)rate + 0.5);
            hr = client_->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, useFlags, duration, 0,
                                     (WAVEFORMATEX*)&cands[i].wf, nullptr);
          }
        }
        if (SUCCEEDED(hr)) {
          want = cands[i].wf;
          useFormat = (WAVEFORMATEX*)&want;
          useOwn = true;
          eventDriven_ = (mode == 0);
          rate_ = rate;
          channels_ = channels;
          float_ = cands[i].isFloat;
          bytesPer_ = cands[i].wf.Format.wBitsPerSample / 8;
          opened = true;
        }
      }
    }
    if (!opened) exclusive = false;
  }

  if (!useOwn && !exclusive && !loopback) {
    // Shared mode, converted to the engine rate.
    MakeFormat(want, rate, channels, true);
    useFormat = (WAVEFORMATEX*)&want;
    flags |= AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    rate_ = rate;
    channels_ = channels;
    float_ = true;
    bytesPer_ = 4;
    REFERENCE_TIME duration = periodDuration(bufferFrames, rate);
    if (duration < defPeriod) duration = defPeriod * 2;
    hr = client_->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, duration, 0, useFormat, nullptr);
    if (FAILED(hr)) {
      if (mix) CoTaskMemFree(mix);
      HRESULT h2 = OpenImpl(deviceId, flow, loopback, rate, channels, bufferFrames, false,
                            eventHandle, depth + 1);
      if (SUCCEEDED(h2)) return h2;
      err_ = L"shared Initialize failed " + HrToString(hr);
      Close();
      return hr;
    }
  } else if (!useOwn && loopback) {
    REFERENCE_TIME duration = 200000;
    hr = client_->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, duration, 0, useFormat, nullptr);
    if (FAILED(hr)) {
      err_ = L"loopback Initialize failed " + HrToString(hr);
      if (mix) CoTaskMemFree(mix);
      Close();
      return hr;
    }
  } else if (!useOwn && depth > 0) {
    REFERENCE_TIME duration = 1000000;
    hr = client_->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, duration, 0, useFormat, nullptr);
    if (FAILED(hr)) {
      err_ = L"native Initialize failed " + HrToString(hr);
      if (mix) CoTaskMemFree(mix);
      Close();
      return hr;
    }
  }

  if (mix) {
    CoTaskMemFree(mix);
    mix = nullptr;
  }

  UINT32 buf = 0;
  client_->GetBufferSize(&buf);
  bufferFrames_ = buf;
  exclusive_ = exclusive;

  REFERENCE_TIME lat = 0;
  if (SUCCEEDED(client_->GetStreamLatency(&lat)) && lat > 0) streamLatencyMs_ = lat / 10000.0;
  else streamLatencyMs_ = bufferLatencyMs();

  if (loopback || depth > 0 || flow == eCapture)
    client_->GetService(__uuidof(IAudioCaptureClient), (void**)&capture_);
  else client_->GetService(__uuidof(IAudioRenderClient), (void**)&render_);

  if (event_ && eventDriven_) {
    hr = client_->SetEventHandle(event_);
    if (FAILED(hr)) {
      err_ = L"SetEventHandle " + HrToString(hr);
      Close();
      return hr;
    }
  }
  return S_OK;
}

void Client::Close() {
  if (client_) client_->Stop();
  if (capture_) {
    capture_->Release();
    capture_ = nullptr;
  }
  if (render_) {
    render_->Release();
    render_ = nullptr;
  }
  if (client_) {
    client_->Release();
    client_ = nullptr;
  }
}

HRESULT Client::Start() {
  if (!client_) return E_FAIL;
  if (capture_) capture_->GetNextPacketSize(&packetSize_);
  HRESULT hr = client_->Start();
  if (FAILED(hr)) err_ = L"Start " + HrToString(hr);
  return hr;
}

void Client::Stop() {
  if (client_) client_->Stop();
}

UINT32 Client::PacketSize() {
  UINT32 n = 0;
  if (capture_) capture_->GetNextPacketSize(&n);
  packetSize_ = n;
  return n;
}

HRESULT Client::ReadNext(BYTE** data, UINT32* frames) {
  *data = nullptr;
  *frames = 0;
  if (!capture_) return E_FAIL;
  UINT32 packet = 0;
  HRESULT hr = capture_->GetNextPacketSize(&packet);
  if (FAILED(hr)) return hr;
  if (packet == 0) return S_FALSE;
  DWORD flags = 0;
  hr = capture_->GetBuffer(data, &packet, &flags, nullptr, nullptr);
  if (FAILED(hr)) return hr;
  *frames = packet;
  if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) ++dropouts_;
  if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
    std::memset(*data, 0, (size_t)packet * channels_ * bytesPer_);
    silent_ = true;
  } else {
    silent_ = false;
  }
  return S_OK;
}

void Client::ReleaseCapture(UINT32 frames) {
  if (capture_) capture_->ReleaseBuffer(frames);
}

HRESULT Client::GetRenderBuffer(BYTE** data, UINT32* frames) {
  if (!render_ || !client_) return E_FAIL;
  UINT32 padding = 0;
  HRESULT hr = client_->GetCurrentPadding(&padding);
  if (FAILED(hr)) return hr;
  UINT32 avail = bufferFrames_ - padding;
  if (avail == 0) {
    *data = nullptr;
    *frames = 0;
    return S_OK;
  }
  hr = render_->GetBuffer(avail, data);
  if (FAILED(hr)) {
    ++dropouts_;
    return hr;
  }
  memset(*data, 0, (size_t)avail * channels_ * bytesPer_);
  *frames = avail;
  return S_OK;
}

HRESULT Client::CommitRender(UINT32 frames) {
  if (!render_) return E_FAIL;
  return render_->ReleaseBuffer(frames, 0);
}

void Client::ToMono(const BYTE* data, UINT32 frames, unsigned channels, int bytesPer, bool isFloat,
                    int selectedChannel, float* out) {
  const int ch = std::max(1, (int)channels);
  const int sel = std::clamp(selectedChannel, 0, ch - 1);
  for (UINT32 i = 0; i < frames; ++i) {
    float v = 0.0f;
    const size_t base = (size_t)i * ch;
    if (isFloat) {
      v = ((const float*)data)[base + sel];
    } else if (bytesPer == 2) {
      v = ((const short*)data)[base + sel] / 32768.0f;
    } else if (bytesPer == 4) {
      // 24-in-32 or 32-bit PCM.
      v = (float)(((const int*)data)[base + sel] / 2147483648.0);
    }
    out[i] = std::clamp(v, -4.0f, 4.0f);
  }
}

void Client::ToStereo(const BYTE* data, UINT32 frames, unsigned channels, int bytesPer, bool isFloat,
                      float* out) {
  const int ch = std::max(1, (int)channels);
  auto sample = [&](size_t index) -> float {
    if (isFloat) return ((const float*)data)[index];
    if (bytesPer == 2) return ((const short*)data)[index] / 32768.0f;
    if (bytesPer == 4) return (float)(((const int*)data)[index] / 2147483648.0);
    return 0.0f;
  };
  for (UINT32 i = 0; i < frames; ++i) {
    const size_t base = (size_t)i * ch;
    float l, r;
    if (ch == 1) {
      l = r = sample(base);
    } else {
      l = sample(base);
      r = sample(base + 1);
    }
    out[i * 2] = std::clamp(l, -4.0f, 4.0f);
    out[i * 2 + 1] = std::clamp(r, -4.0f, 4.0f);
  }
}

} // namespace au
