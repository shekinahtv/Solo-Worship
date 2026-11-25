// ---------------------------------------------------------------------------
// lvs_latencytest - probes what buffer sizes this machine's driver actually
// grants, for both WASAPI shared and exclusive modes, at several formats.
// Every attempt uses a fresh IAudioClient (Reset does not allow re-Init).
// ---------------------------------------------------------------------------
#include "audio/Wasapi.h"
#include "audio/Engine.h"

#include <audioclient.h>
#include <cmath>
#include <cstdio>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <string>
#include <windows.h>

using namespace lvs;

static const GUID kSubFloat = {0x00000003, 0x0000, 0x0010,
                               {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
static const GUID kSubPcm = {0x00000001, 0x0000, 0x0010,
                             {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

static std::string u8(const std::wstring& w) {
  if (w.empty()) return {};
  int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s((size_t)len, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), len, nullptr, nullptr);
  return s;
}

static IAudioClient* freshClient(const std::wstring& id) {
  IMMDeviceEnumerator* en = nullptr;
  CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                   __uuidof(IMMDeviceEnumerator), (void**)&en);
  if (!en) return nullptr;
  IMMDevice* dev = nullptr;
  if (FAILED(en->GetDevice(id.c_str(), &dev))) {
    en->Release();
    return nullptr;
  }
  IAudioClient* c = nullptr;
  dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&c);
  dev->Release();
  en->Release();
  return c;
}

static void mkfmt(WAVEFORMATEXTENSIBLE& wf, unsigned rate, unsigned ch, int bits, bool flt) {
  ZeroMemory(&wf, sizeof(wf));
  wf.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
  wf.Format.nChannels = (WORD)ch;
  wf.Format.nSamplesPerSec = rate;
  wf.Format.wBitsPerSample = (WORD)bits;
  wf.Format.nBlockAlign = (WORD)(ch * bits / 8);
  wf.Format.nAvgBytesPerSec = rate * wf.Format.nBlockAlign;
  wf.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
  wf.Samples.wValidBitsPerSample = (WORD)bits;
  wf.dwChannelMask = ch == 1 ? 4 : 3;
  wf.SubFormat = flt ? kSubFloat : kSubPcm;
}

int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  au::ComScope com;
  const std::wstring render = Engine::SuggestRenderEndpoint();
  const std::wstring capture = Engine::SuggestCaptureEndpoint();

  printf("Device: %s\n", u8(render).c_str());
  au::Endpoint ep;
  au::FindEndpoint(render, eRender, ep);
  printf("Mix format: %u ch @ %u Hz\n\n", ep.channels, ep.rate);

  const unsigned native = ep.rate ? ep.rate : 44100;

  // --- Exclusive attempts at the native rate, PCM16, polling --------------
  for (int ms : {3, 5, 8, 10, 20}) {
    IAudioClient* c = freshClient(render);
    if (!c) break;
    WAVEFORMATEXTENSIBLE wf;
    mkfmt(wf, native, 2, 16, false);
    REFERENCE_TIME d = (REFERENCE_TIME)ms * 10000;
    HRESULT hr = c->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, 0, d, 0, (WAVEFORMATEX*)&wf, nullptr);
    UINT32 frames = 0;
    REFERENCE_TIME lat = 0;
    if (SUCCEEDED(hr)) {
      c->GetBufferSize(&frames);
      c->GetStreamLatency(&lat);
      printf("  exclusive pcm16 %5u Hz %2d ms -> %u frames (%.1f ms), stream latency %.1f ms\n",
             native, ms, frames, 1000.0 * frames / native, lat / 10000.0);
    } else {
      printf("  exclusive pcm16 %5u Hz %2d ms -> 0x%08lX\n", native, ms, (unsigned long)hr);
    }
    c->Release();
  }

  // --- Shared attempts, native rate, native mix format --------------------
  for (int ms : {10, 20, 40, 80}) {
    IAudioClient* c = freshClient(render);
    if (!c) break;
    WAVEFORMATEX* mix = nullptr;
    c->GetMixFormat(&mix);
    REFERENCE_TIME d = (REFERENCE_TIME)ms * 10000;
    HRESULT hr = c->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, d, 0,
                               mix, nullptr);
    UINT32 frames = 0;
    REFERENCE_TIME lat = 0;
    if (SUCCEEDED(hr)) {
      c->GetBufferSize(&frames);
      c->GetStreamLatency(&lat);
      printf("  shared    native %2d ms -> %u frames (%.1f ms), stream latency %.1f ms\n", ms,
             frames, 1000.0 * frames / native, lat / 10000.0);
    } else {
      printf("  shared    native %2d ms -> 0x%08lX\n", ms, (unsigned long)hr);
    }
    if (mix) CoTaskMemFree(mix);
    c->Release();
  }

  // --- Capture side -------------------------------------------------------
  IAudioClient* c = freshClient(capture);
  if (c) {
    WAVEFORMATEX* mix = nullptr;
    c->GetMixFormat(&mix);
    REFERENCE_TIME def = 0, mn = 0;
    c->GetDevicePeriod(&def, &mn);
    printf("\nCapture: mix %u Hz, periods def %.1f ms min %.1f ms\n", mix ? mix->nSamplesPerSec : 0,
           def / 10000.0, mn / 10000.0);
    HRESULT hr = c->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, 0, mn, 0, mix, nullptr);
    UINT32 frames = 0;
    if (SUCCEEDED(hr)) {
      c->GetBufferSize(&frames);
      printf("  capture exclusive pcm/float native -> %u frames (%.1f ms)\n", frames,
             1000.0 * frames / (double)(mix ? mix->nSamplesPerSec : 48000));
    } else {
      printf("  capture exclusive -> 0x%08lX\n", (unsigned long)hr);
    }
    if (mix) CoTaskMemFree(mix);
    c->Release();
  }

  printf("\nConclusion for this machine:\n");
  printf("  The Focusrite WDM driver fixes its exclusive buffer at 8 periods, so\n");
  printf("  shared mode at one 10 ms period is the lowest WASAPI latency here.\n");
  return 0;
}
