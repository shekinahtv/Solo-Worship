#include "audio/Engine.h"

#include "audio/Asio.h"
#include "dsp/Effects.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <timeapi.h>
#include <vector>
#include <windows.h>

namespace lvs {

namespace {

// Single-producer / single-consumer float frame ring.
struct FrameRing {
  std::vector<float> buf;
  size_t frames = 1, mask = 0;
  int ch = 1;
  std::atomic<uint64_t> w{0};

  void init(int channels, size_t capacityFrames) {
    ch = channels;
    size_t pow2 = 1;
    while (pow2 < capacityFrames) pow2 <<= 1;
    frames = pow2;
    mask = pow2 - 1;
    buf.assign(frames * (size_t)ch, 0.0f);
    w.store(0);
  }
  void reset() {
    std::fill(buf.begin(), buf.end(), 0.0f);
    w.store(0);
  }
  void push(const float* src, size_t n) {
    const uint64_t start = w.load(std::memory_order_relaxed);
    for (size_t i = 0; i < n; ++i) {
      const size_t idx = (size_t)((start + i) & mask) * (size_t)ch;
      for (int c = 0; c < ch; ++c) buf[idx + c] = src[i * ch + c];
    }
    w.store(start + n, std::memory_order_release);
  }
  const float* at(uint64_t frame) const { return &buf[(size_t)(frame & mask) * (size_t)ch]; }
  uint64_t written() const { return w.load(std::memory_order_acquire); }
  size_t capacity() const { return frames; }
};

inline float peakOf(const float* x, int n) {
  float p = 0.0f;
  for (int i = 0; i < n; ++i) p = std::max(p, std::fabs(x[i]));
  return p;
}

// Decay a meter value with a fixed release so the UI reads nicely.
inline float meterFollow(float current, float peak, float releasePerBlock) {
  return peak > current ? peak : current * releasePerBlock;
}

} // namespace

struct Engine::Impl {
  FrameRing micRing, musicRing;
  uint64_t micPos = 0;
  double musicPos = 0.0;
  std::atomic<double> musicRate{1.0};
  bool musicPosInit = false;

  std::vector<float> mic, send, wetL, wetR, musL, musR, mixL, mixR;
  std::vector<float> mwL, mwR, micMixL, micMixR, musOutL, musOutR, recInter;
  std::vector<uint8_t> outBytes;
  int maxBlock = 8192;

  dsp::Preamp pre;
  dsp::BritishEQ eq;
  dsp::Compressor comp;
  dsp::Reverb rev;
  dsp::MusicEQ musEq;
  dsp::Compressor busL, busR;
  dsp::Limiter limiter;

  bool dspReady = false;
  double sr = 48000.0;
  bool recordVocal = false;
  float inMeter = 0, outMeterL = 0, outMeterR = 0, musMeter = 0;
  float recPeakLocal = 0;

  void prepare(double sampleRate) {
    sr = sampleRate;
    pre.prepare(sr);
    eq.prepare(sr);
    comp.prepare(sr);
    rev.prepare(sr, maxBlock);
    musEq.prepare(sr);
    busL.prepare(sr);
    busR.prepare(sr);
    limiter.prepare(sr);
    mic.assign(maxBlock, 0.0f);
    send.assign(maxBlock, 0.0f);
    wetL.assign(maxBlock, 0.0f);
    wetR.assign(maxBlock, 0.0f);
    musL.assign(maxBlock, 0.0f);
    musR.assign(maxBlock, 0.0f);
    mixL.assign(maxBlock, 0.0f);
    mixR.assign(maxBlock, 0.0f);
    mwL.assign(maxBlock, 0.0f);
    mwR.assign(maxBlock, 0.0f);
    micMixL.assign(maxBlock, 0.0f);
    micMixR.assign(maxBlock, 0.0f);
    musOutL.assign(maxBlock, 0.0f);
    musOutR.assign(maxBlock, 0.0f);
    recInter.assign((size_t)maxBlock * 2, 0.0f);
    outBytes.assign((size_t)maxBlock * 32 * 4, 0);
    dspReady = true;
  }
};

Engine::Engine() {
  impl_ = new Impl();
  impl_->micRing.init(1, 16384);
  impl_->musicRing.init(2, 16384);
}

Engine::~Engine() {
  Stop();
  delete impl_;
  impl_ = nullptr;
}

std::vector<std::pair<std::wstring, std::wstring>> Engine::ListAsioDrivers() {
  std::vector<std::pair<std::wstring, std::wstring>> out;
  for (auto& d : asio::EnumerateDrivers()) out.emplace_back(d.name, d.clsid);
  return out;
}

std::wstring Engine::SuggestAsioClsid(std::wstring* driverName) {
  std::wstring best;
  for (auto& d : asio::EnumerateDrivers()) {
    if (d.name.find(L"Focusrite USB") != std::wstring::npos) {
      if (driverName) *driverName = d.name;
      return d.clsid;
    }
  }
  auto all = asio::EnumerateDrivers();
  if (!all.empty()) {
    if (driverName) *driverName = all.front().name;
    return all.front().clsid;
  }
  if (driverName) driverName->clear();
  return {};
}

std::wstring Engine::SuggestCaptureEndpoint() {
  for (auto& ep : au::List(eCapture)) {
    if (ep.name.find(L"Focusrite") != std::wstring::npos) return ep.id;
  }
  const auto def = au::DefaultId(eCapture);
  return def;
}

std::wstring Engine::SuggestRenderEndpoint() {
  for (auto& ep : au::List(eRender)) {
    if (ep.name.find(L"Focusrite") != std::wstring::npos) return ep.id;
  }
  return au::DefaultId(eRender);
}

std::wstring Engine::SuggestLoopbackEndpoint() {
  // Prefer an endpoint that is *not* the Focusrite so karaoke playback is not
  // doubled through the studio monitors.
  auto list = au::List(eRender);
  for (auto& ep : list) {
    if (ep.name.find(L"Focusrite") == std::wstring::npos && ep.isDefault) return ep.id;
  }
  for (auto& ep : list) {
    if (ep.name.find(L"Focusrite") == std::wstring::npos) return ep.id;
  }
  const auto def = au::DefaultId(eRender);
  return def;
}

EngineConfig Engine::config() const { return cfg_; }
void Engine::setConfig(const EngineConfig& c) { cfg_ = c; }

EngineStats Engine::stats() const {
  EngineStats s;
  s.inputPeak = inPeak_.load();
  s.outputPeakL = outPeakL_.load();
  s.outputPeakR = outPeakR_.load();
  s.musicPeak = musicPeak_.load();
  s.compGr = compGr_.load();
  s.limiterGr = limiterGr_.load();
  s.cpuLoad = cpu_.load();
  s.bufferLatencyMs = bufLat_.load();
  s.streamLatencyMs = streamLat_.load();
  s.dropouts = dropouts_.load();
  s.underruns = underruns_.load();
  s.frames = frames_.load();
  s.sampleRate = sr_.load();
  s.bufferFrames = bufFrames_.load();
  s.deviceChannels = devCh_.load();
  s.running = running_.load();
  s.renderOpen = renderOpen_.load();
  s.captureOpen = captureOpen_.load();
  s.musicOpen = musicOpen_.load();
  s.exclusive = exclusive_.load();
  s.asio = asioActive_.load();
  s.asioInputLatencyMs = asioInLat_.load();
  s.asioOutputLatencyMs = asioOutLat_.load();
  s.roundTripMs = s.asio ? (s.asioInputLatencyMs + s.asioOutputLatencyMs + bufLat_.load())
                         : (bufLat_.load() * 2.0 + streamLat_.load() * 2.0);
  return s;
}

bool Engine::Start(const EngineConfig& inCfg, std::wstring* error) {
  Stop();
  cfg_ = inCfg;
  if (cfg_.sampleRate <= 0) cfg_.sampleRate = 48000;
  if (cfg_.bufferFrames < 32) cfg_.bufferFrames = 128;

  if (cfg_.useAsio) {
    if (cfg_.asioClsid.empty()) {
      if (error) *error = L"No ASIO driver available.";
      return false;
    }
    sr_.store(cfg_.sampleRate);
  } else {
    au::Endpoint rep;
    if (cfg_.renderId.empty() || !au::FindEndpoint(cfg_.renderId, eRender, rep)) {
      cfg_.renderId = SuggestRenderEndpoint();
      if (!au::FindEndpoint(cfg_.renderId, eRender, rep)) {
        if (error) *error = L"No render device available.";
        return false;
      }
    }
    cfg_.deviceChannels = (int)std::max(1u, rep.channels);
    sr_.store((int)rep.rate);
  }

  au::Endpoint cep;
  if (cfg_.captureId.empty() || !au::FindEndpoint(cfg_.captureId, eCapture, cep)) {
    cfg_.captureId = SuggestCaptureEndpoint();
    if (!cfg_.captureId.empty()) au::FindEndpoint(cfg_.captureId, eCapture, cep);
  }
  if (!cep.id.empty()) cfg_.captureChannels = (int)std::max(1u, cep.channels);
  if (cfg_.captureChannels < 1) cfg_.captureChannels = 2;

  // Karaoke audio is whatever Windows plays, so the default render endpoint is
  // the correct capture source unless the user pinned a specific device.
  if (cfg_.loopbackFollowDefault) {
    const std::wstring def = au::DefaultId(eRender);
    if (!def.empty()) cfg_.loopbackId = def;
  }

  impl_->micRing.reset();
  impl_->musicRing.reset();
  impl_->micPos = 0;
  impl_->musicPos = 0;
  impl_->musicPosInit = false;

  stop_.store(false);
  deviceLost_.store(false);
  dropouts_.store(0);
  underruns_.store(0);
  frames_.store(0);
  renderOpen_.store(false);
  captureOpen_.store(false);
  musicOpen_.store(false);

  // 1 ms timer resolution so the polling exclusive path is steady.
  timeBeginPeriod(1);
  renderThread_ = std::thread([this] { renderLoop(); });
  captureThread_ = std::thread([this] { captureLoop(); });
  musicThread_ = std::thread([this] { musicLoop(); });

  // Wait briefly for the first render client to come up so the caller gets a
  // truthful answer.
  for (int i = 0; i < 60; ++i) {
    if (renderOpen_.load()) break;
    if (openError_.load() != 0 && !renderOpen_.load() && i > 4) break;
    Sleep(50);
  }

  if (!renderOpen_.load()) {
    std::wstring msg = lastError_.empty() ? L"Could not open the output device." : lastError_;
    Stop();
    if (error) *error = msg;
    return false;
  }
  running_.store(true);
  return true;
}

void Engine::Stop() {
  stop_.store(true);
  if (renderThread_.joinable()) renderThread_.join();
  if (captureThread_.joinable()) captureThread_.join();
  if (musicThread_.joinable()) musicThread_.join();
  running_.store(false);
  renderOpen_.store(false);
  captureOpen_.store(false);
  musicOpen_.store(false);
  if (recorder_.active()) recorder_.stop();
  timeEndPeriod(1);
}

bool Engine::Restart(const EngineConfig& cfg, std::wstring* error) {
  Stop();
  Sleep(80);
  return Start(cfg, error);
}

bool Engine::startRecording(const std::wstring& path, int bitDepth, bool recordVocal,
                            std::wstring* error) {
  impl_->recordVocal = recordVocal;
  inPeak_.store(0);
  recPeak_.store(0);
  const int recRate = sr_.load() > 0 ? sr_.load() : cfg_.sampleRate;
  return recorder_.start(path, recRate, 2, bitDepth, error);
}

void Engine::stopRecording() { recorder_.stop(); }

// ---------------------------------------------------------------------------
// Render thread: the master clock of the application.
// ---------------------------------------------------------------------------
void Engine::renderLoop() {
  if (cfg_.useAsio && !cfg_.asioClsid.empty()) {
    asioLoop();
    return;
  }
  au::ComScope com;
  HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  au::Client client;
  bool announced = false;
  LARGE_INTEGER freq;
  QueryPerformanceFrequency(&freq);
  uint64_t consecutiveFailures = 0;

  while (!stop_.load()) {
    if (!client.isOpen()) {
      unsigned openRate = (unsigned)cfg_.sampleRate;
      HRESULT hr = client.Open(cfg_.renderId, eRender, false, openRate, cfg_.deviceChannels,
                               cfg_.bufferFrames, cfg_.exclusive, ev);
      // The Focusrite WDM endpoint only runs exclusive in the rate Windows is
      // currently using. If we did not get exclusive at the requested rate but
      // can get it at the endpoint rate, prefer exclusive low latency.
      if (SUCCEEDED(hr) && client.isOpen() && cfg_.exclusive && !client.exclusive()) {
        au::Endpoint ep;
        if (au::FindEndpoint(cfg_.renderId, eRender, ep) && ep.rate > 0 &&
            (int)ep.rate != cfg_.sampleRate) {
          client.Close();
          openRate = ep.rate;
          hr = client.Open(cfg_.renderId, eRender, false, openRate, cfg_.deviceChannels,
                           cfg_.bufferFrames, true, ev);
        }
      }
      if (FAILED(hr) || !client.isOpen()) {
        renderOpen_.store(false);
        openError_.store(hr ? (int)hr : -1);
        lastError_ = client.lastError();
        if (!announced) announced = true;
        Sleep(300);
        continue;
      }
      impl_->prepare((double)client.rate());
      impl_->limiter.reset();
      impl_->rev.reset();
      bufFrames_.store((int)client.bufferFrames());
      devCh_.store((int)client.channels());
      sr_.store((int)client.rate());
      bufLat_.store(client.bufferLatencyMs());
      streamLat_.store(client.streamLatencyMs());
      exclusive_.store(client.exclusive());
      impl_->micRing.reset();
      impl_->musicRing.reset();
      impl_->micPos = 0;
      impl_->musicPosInit = false;
      HRESULT shr = client.Start();
      if (FAILED(shr)) {
        lastError_ = client.lastError();
        client.Close();
        Sleep(300);
        continue;
      }
      renderOpen_.store(true);
      deviceLost_.store(false);
      consecutiveFailures = 0;
      announced = true;
      SetThreadPriority(GetCurrentThread(), client.eventDriven() ? THREAD_PRIORITY_TIME_CRITICAL
                                                                : THREAD_PRIORITY_HIGHEST);
      continue;
    }

    if (client.eventDriven()) {
      DWORD wait = WaitForSingleObject(ev, 500);
      if (stop_.load()) break;
      if (wait != WAIT_OBJECT_0) {
        ++consecutiveFailures;
        if (consecutiveFailures > 3) {
          client.Close();
          renderOpen_.store(false);
          deviceLost_.store(true);
          ++consecutiveFailures;
        }
        continue;
      }
    } else {
      // Polling exclusive stream: wait for the engine to hand the buffer back.
      Sleep(1);
      if (stop_.load()) break;
      if (++consecutiveFailures > 2500) {
        client.Close();
        renderOpen_.store(false);
        deviceLost_.store(true);
        consecutiveFailures = 0;
        continue;
      }
    }

    for (int guard = 0; guard < 32 && !stop_.load(); ++guard) {
      BYTE* data = nullptr;
      UINT32 avail = 0;
      HRESULT hr = client.GetRenderBuffer(&data, &avail);
      if (hr == AUDCLNT_E_DEVICE_INVALIDATED || hr == AUDCLNT_E_SERVICE_NOT_RUNNING) {
        client.Close();
        renderOpen_.store(false);
        deviceLost_.store(true);
        dropouts_.fetch_add(1);
        break;
      }
      if (FAILED(hr)) {
        ++dropouts_;
        client.Close();
        renderOpen_.store(false);
        break;
      }
      if (!data || avail == 0) break;
      consecutiveFailures = 0;

      LARGE_INTEGER t0, t1;
      QueryPerformanceCounter(&t0);

      int remaining = (int)avail;
      int offset = 0;
      while (remaining > 0) {
        const int chunk = std::min(remaining, impl_->maxBlock);
        // --- pull microphone -------------------------------------------------
        const uint64_t mw = impl_->micRing.written();
        if (cfg_.captureEnabled && mw > impl_->micPos) {
          const uint64_t have = mw - impl_->micPos;
          const int take = (int)std::min<uint64_t>(have, (uint64_t)chunk);
          for (int i = 0; i < take; ++i) impl_->mic[i] = impl_->micRing.at(impl_->micPos + i)[0];
          for (int i = take; i < chunk; ++i) impl_->mic[i] = 0.0f;
          impl_->micPos += (uint64_t)take;
          if (have > (uint64_t)chunk * 6) impl_->micPos = mw - (uint64_t)chunk; // trim latency
        } else {
          std::fill(impl_->mic.begin(), impl_->mic.begin() + chunk, 0.0f);
          impl_->micPos = mw;
          underruns_.fetch_add(1);
        }

        pullMusic(chunk);

        process(impl_->mic.data(), impl_->musL.data(), impl_->musR.data(), impl_->mixL.data(),
                impl_->mixR.data(), chunk);

        // --- write to the device --------------------------------------------
        const int devCh = (int)client.channels();
        const bool isF = client.isFloat();
        const int bps = client.bytesPerSample();
        const int ol = std::clamp(cfg_.outLeft, 0, devCh - 1);
        const int orr = std::clamp(cfg_.outRight, 0, devCh - 1);
        BYTE* dst = data + (size_t)offset * devCh * bps;
        if (isF) {
          float* f = (float*)dst;
          for (int i = 0; i < chunk; ++i) {
            f[(size_t)i * devCh + ol] = impl_->mixL[i];
            f[(size_t)i * devCh + orr] = impl_->mixR[i];
          }
        } else if (bps == 2) {
          short* s = (short*)dst;
          auto conv = [](float v) {
            v = std::clamp(v, -1.0f, 1.0f);
            return (short)std::lround(v * 32767.0f);
          };
          for (int i = 0; i < chunk; ++i) {
            s[(size_t)i * devCh + ol] = conv(impl_->mixL[i]);
            s[(size_t)i * devCh + orr] = conv(impl_->mixR[i]);
          }
        } else if (bps == 4) {
          // 24-in-32 / 32-bit PCM container.
          int* q = (int*)dst;
          auto conv = [](float v) {
            v = std::clamp(v, -1.0f, 1.0f);
            return (int)std::llround((double)v * 2147483647.0);
          };
          for (int i = 0; i < chunk; ++i) {
            q[(size_t)i * devCh + ol] = conv(impl_->mixL[i]);
            q[(size_t)i * devCh + orr] = conv(impl_->mixR[i]);
          }
        }
        offset += chunk;
        remaining -= chunk;
      }

      client.CommitRender(avail);

      LARGE_INTEGER t1b;
      QueryPerformanceCounter(&t1b);
      const double elapsed = (double)(t1b.QuadPart - t0.QuadPart) / (double)freq.QuadPart;
      const double budget = (double)avail / (double)client.rate();
      const float load = (float)std::clamp(elapsed / std::max(1e-6, budget), 0.0, 4.0);
      cpu_.store(cpu_.load() * 0.85f + load * 0.15f);
      frames_.fetch_add(avail);
    }
  }

  client.Close();
  CloseHandle(ev);
  renderOpen_.store(false);
}

// Karaoke feed: fractional read with slow drift correction, so a playback
// device on a different clock never over/underruns the ring.
void Engine::pullMusic(int frames) {
  Impl& I = *impl_;
  std::fill(I.musL.begin(), I.musL.begin() + frames, 0.0f);
  std::fill(I.musR.begin(), I.musR.begin() + frames, 0.0f);
  if (!cfg_.musicEnabled || cfg_.loopbackId.empty()) return;
  const uint64_t written = I.musicRing.written();
  const size_t cap = I.musicRing.capacity();
  // A few render blocks of cushion. The old quarter-buffer target held the song
  // ~85 ms behind the picture at 128 frames.
  const double target =
      std::clamp<double>((double)cfg_.bufferFrames * 4.0 + 256.0, 512.0, 2048.0);
  if (!I.musicPosInit) {
    I.musicPos = std::max(0.0, (double)written - target);
    I.musicPosInit = true;
  }
  double fill = (double)written - I.musicPos;
  if (fill > (double)cap - 64.0 || fill < 0.0) {
    I.musicPos = std::max(0.0, (double)written - target);
    fill = target;
  }
  const double baseRate = I.musicRate.load();
  const double corr = std::clamp((fill - target) / std::max(64.0, target) * 0.05, -0.02, 0.02);
  const double step = baseRate * (1.0 + corr);
  for (int i = 0; i < frames; ++i) {
    const double pos = I.musicPos;
    const uint64_t i0 = (uint64_t)pos;
    const double frac = pos - (double)i0;
    if (i0 + 1 < written) {
      const float* a = I.musicRing.at(i0);
      const float* b = I.musicRing.at(i0 + 1);
      I.musL[i] = (float)(a[0] * (1.0 - frac) + b[0] * frac);
      I.musR[i] = (float)(a[1] * (1.0 - frac) + b[1] * frac);
    }
    I.musicPos += step;
  }
}

// ---------------------------------------------------------------------------
// ASIO render path: the driver's callback is the clock, inputs arrive
// directly with no ring-buffer hop.
// ---------------------------------------------------------------------------
void Engine::asioLoop() {
  au::ComScope com;
  WNDCLASSEXW wc = {sizeof(wc)};
  wc.lpfnWndProc = DefWindowProcW;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"LVS_AsioMessageWindow";
  RegisterClassExW(&wc);
  HWND wnd = CreateWindowExW(0, wc.lpszClassName, L"LVS ASIO", WS_OVERLAPPED, 0, 0, 0, 0,
                             nullptr, nullptr, wc.hInstance, nullptr);

  asio::Engine ae;
  LARGE_INTEGER freq;
  QueryPerformanceFrequency(&freq);

  while (!stop_.load()) {
    if (!ae.opened() || ae.resetRequested()) {
      ae.clearResetRequest();
      ae.stop();
      ae.close();
      std::wstring err;
      if (!ae.open(cfg_.asioClsid, wnd, cfg_.sampleRate, cfg_.bufferFrames, &err)) {
        lastError_ = err;
        renderOpen_.store(false);
        openError_.store(-1);
        deviceLost_.store(true);
        Sleep(700);
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
          TranslateMessage(&msg);
          DispatchMessageW(&msg);
        }
        continue;
      }

      impl_->prepare((double)ae.sampleRate());
      impl_->rev.reset();
      impl_->limiter.reset();
      impl_->micRing.reset();
      impl_->musicRing.reset();
      impl_->musicPosInit = false;
      impl_->micPos = 0;
      sr_.store(ae.sampleRate());
      bufFrames_.store(ae.bufferSize());
      devCh_.store(ae.outputChannels());
      bufLat_.store(1000.0 * ae.bufferSize() / (double)ae.sampleRate());
      streamLat_.store(ae.inputLatencyMs() + ae.outputLatencyMs());
      asioInLat_.store(ae.inputLatencyMs());
      asioOutLat_.store(ae.outputLatencyMs());
      exclusive_.store(true);
      asioActive_.store(true);

      const int micCh = std::clamp(cfg_.captureChannel, 0, std::max(0, ae.inputChannels() - 1));
      const int outL = std::clamp(cfg_.outLeft, 0, std::max(0, ae.outputChannels() - 1));
      const int outR = std::clamp(cfg_.outRight, 0, std::max(0, ae.outputChannels() - 1));

      ae.setProcess([this, micCh, outL, outR, &ae, &freq](const float* const* ins,
                                                          float* const* outs, int frames) {
        LARGE_INTEGER t0, t1;
        QueryPerformanceCounter(&t0);
        const int n = std::min(frames, impl_->maxBlock);
        if (ins && ins[micCh])
          std::memcpy(impl_->mic.data(), ins[micCh], sizeof(float) * (size_t)n);
        else
          std::fill(impl_->mic.begin(), impl_->mic.begin() + n, 0.0f);
        pullMusic(n);
        process(impl_->mic.data(), impl_->musL.data(), impl_->musR.data(), impl_->mixL.data(),
                impl_->mixR.data(), n);
        for (int c = 0; c < ae.outputChannels(); ++c) {
          if (!outs || !outs[c]) continue;
          if (c == outL)
            std::memcpy(outs[c], impl_->mixL.data(), sizeof(float) * (size_t)n);
          else if (c == outR)
            std::memcpy(outs[c], impl_->mixR.data(), sizeof(float) * (size_t)n);
          else
            std::fill(outs[c], outs[c] + n, 0.0f);
        }
        frames_.fetch_add((uint64_t)frames);
        QueryPerformanceCounter(&t1);
        const double elapsed = (double)(t1.QuadPart - t0.QuadPart) / (double)freq.QuadPart;
        const double budget = (double)frames / (double)ae.sampleRate();
        const float load = (float)std::clamp(elapsed / std::max(1e-6, budget), 0.0, 4.0);
        cpu_.store(cpu_.load() * 0.9f + load * 0.1f);
      });

      if (!ae.start()) {
        lastError_ = L"ASIO start() failed";
        renderOpen_.store(false);
        asioActive_.store(false);
        ae.close();
        Sleep(500);
        continue;
      }
      renderOpen_.store(true);
      captureOpen_.store(true);
      deviceLost_.store(false);
      openError_.store(0);
      continue;
    }

    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    Sleep(10);
  }

  ae.stop();
  ae.close();
  asioActive_.store(false);
  renderOpen_.store(false);
  captureOpen_.store(false);
  if (wnd) DestroyWindow(wnd);
  UnregisterClassW(wc.lpszClassName, wc.hInstance);
}

void Engine::captureLoop() {
  au::ComScope com;
  HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  au::Client client;
  std::vector<float> mono;
  mono.resize(16384);
  int fails = 0;

  while (!stop_.load()) {
    if (cfg_.captureId.empty() || !cfg_.captureEnabled) {
      captureOpen_.store(false);
      Sleep(200);
      continue;
    }
    if (!client.isOpen()) {
      const unsigned wantRate = sr_.load() > 0 ? (unsigned)sr_.load() : (unsigned)cfg_.sampleRate;
      HRESULT hr = client.Open(cfg_.captureId, eCapture, false, wantRate, cfg_.captureChannels,
                               cfg_.bufferFrames, cfg_.exclusive, ev);
      if (FAILED(hr) || !client.isOpen()) {
        captureOpen_.store(false);
        Sleep(400);
        continue;
      }
      impl_->micRing.reset();
      impl_->micPos = 0;
      client.Start();
      captureOpen_.store(true);
      SetThreadPriority(GetCurrentThread(), client.eventDriven() ? THREAD_PRIORITY_TIME_CRITICAL
                                                                : THREAD_PRIORITY_HIGHEST);
      fails = 0;
      continue;
    }

    if (client.eventDriven()) {
      DWORD w = WaitForSingleObject(ev, 400);
      if (stop_.load()) break;
      if (w != WAIT_OBJECT_0) {
        if (++fails > 6) {
          client.Close();
          captureOpen_.store(false);
        }
        continue;
      }
    } else {
      Sleep(1);
      if (stop_.load()) break;
    }
    fails = 0;
    for (int guard = 0; guard < 64 && !stop_.load(); ++guard) {
      BYTE* data = nullptr;
      UINT32 frames = 0;
      HRESULT hr = client.ReadNext(&data, &frames);
      if (hr == S_FALSE) break;
      if (hr == AUDCLNT_E_DEVICE_INVALIDATED || hr == AUDCLNT_E_SERVICE_NOT_RUNNING) {
        client.Close();
        captureOpen_.store(false);
        deviceLost_.store(true);
        break;
      }
      if (FAILED(hr)) {
        client.Close();
        captureOpen_.store(false);
        break;
      }
      if (frames > 0 && data) {
        au::Client::ToMono(data, frames, client.channels(), client.bytesPerSample(), client.isFloat(),
                           cfg_.captureChannel, mono.data());
        impl_->micRing.push(mono.data(), frames);
      }
      client.ReleaseCapture(frames);
    }
  }

  client.Close();
  CloseHandle(ev);
  captureOpen_.store(false);
}

void Engine::musicLoop() {
  au::ComScope com;
  HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  au::Client client;
  std::vector<float> stereo;
  stereo.resize(16384 * 2);
  int fails = 0;

  while (!stop_.load()) {
    if (cfg_.loopbackFollowDefault) {
      // Follow the Windows default device as it changes.
      const std::wstring def = au::DefaultId(eRender);
      if (!def.empty() && def != cfg_.loopbackId) {
        if (client.isOpen()) client.Close();
        cfg_.loopbackId = def;
      }
    }
    if (cfg_.loopbackId.empty() || !cfg_.musicEnabled) {
      musicOpen_.store(false);
      Sleep(300);
      continue;
    }
    if (!client.isOpen()) {
      HRESULT hr = client.Open(cfg_.loopbackId, eRender, true, cfg_.sampleRate, 2,
                               std::max(256, cfg_.bufferFrames), false, ev);
      if (FAILED(hr) || !client.isOpen()) {
        musicOpen_.store(false);
        Sleep(600);
        continue;
      }
      impl_->musicRing.reset();
      impl_->musicPosInit = false;
      const double engineRate = sr_.load() > 0 ? (double)sr_.load() : (double)cfg_.sampleRate;
      impl_->musicRate.store((double)client.rate() / engineRate);
      client.Start();
      musicOpen_.store(true);
      fails = 0;
      continue;
    }
    DWORD w = WaitForSingleObject(ev, 500);
    if (stop_.load()) break;
    if (w != WAIT_OBJECT_0) {
      if (++fails > 6) {
        client.Close();
        musicOpen_.store(false);
      }
      continue;
    }
    fails = 0;
    for (int guard = 0; guard < 64 && !stop_.load(); ++guard) {
      BYTE* data = nullptr;
      UINT32 frames = 0;
      HRESULT hr = client.ReadNext(&data, &frames);
      if (hr == S_FALSE) break;
      if (hr == AUDCLNT_E_DEVICE_INVALIDATED || hr == AUDCLNT_E_SERVICE_NOT_RUNNING) {
        client.Close();
        musicOpen_.store(false);
        break;
      }
      if (FAILED(hr)) {
        client.Close();
        musicOpen_.store(false);
        break;
      }
      if (frames > 0 && data) {
        const UINT32 capped = std::min<UINT32>(frames, 16384);
        au::Client::ToStereo(data, capped, client.channels(), client.bytesPerSample(),
                             client.isFloat(), stereo.data());
        impl_->musicRing.push(stereo.data(), capped);
      }
      client.ReleaseCapture(frames);
    }
  }
  client.Close();
  CloseHandle(ev);
  musicOpen_.store(false);
}

// ---------------------------------------------------------------------------
// The channel strip + master bus.
// ---------------------------------------------------------------------------
void Engine::process(float* mic, const float* musicL, const float* musicR, float* outL,
                     float* outR, int n) {
  Impl& I = *impl_;
  if (!I.dspReady || n <= 0) return;

  const float rawPeak = peakOf(mic, n);
  inPeak_.store(meterFollow(inPeak_.load(), rawPeak, 0.90f));

  // --- Vocal chain ---------------------------------------------------------
  const int mode = (int)std::lround(params_.get(P::PreMode));
  I.pre.setParams(params_.get(P::PreGain), params_.get(P::PreDrive), params_.get(P::PreWarmth),
                  params_.get(P::PreSat), params_.get(P::PreOut), params_.getBool(P::PrePhase), mode);
  I.pre.process(mic, n);
  I.eq.setParams(params_);
  I.eq.process(mic, n);
  I.comp.setParams(params_.get(P::CompThresh), params_.get(P::CompRatio),
                   params_.get(P::CompAttack), params_.get(P::CompRelease), params_.get(P::CompKnee),
                   params_.get(P::CompMakeup), params_.get(P::CompMix), params_.getBool(P::CompOn));
  I.comp.process(mic, n);
  compGr_.store(I.comp.gainReductionDb());

  // --- Reverb send ---------------------------------------------------------
  const float send = params_.getBool(P::RevOn) ? params_.get(P::RevSend) : 0.0f;
  I.rev.setParams(params_);
  I.rev.setEarlyMix(params_.get(P::RevEarly));
  for (int i = 0; i < n; ++i) I.send[i] = mic[i] * send;
  I.rev.process(I.send.data(), I.wetL.data(), I.wetR.data(), n);

  // --- Music EQ ------------------------------------------------------------
  I.musEq.setParams(params_.get(P::MusEqLow), params_.get(P::MusEqMid), params_.get(P::MusEqHigh),
                    params_.getBool(P::MusEqOn));

  // --- Faders, pan, solo ---------------------------------------------------
  const bool anySolo = params_.getBool(P::MicSolo) || params_.getBool(P::MusicSolo);
  float micGain = params_.get(P::MicFader);
  if (params_.getBool(P::MicMute) || (anySolo && !params_.getBool(P::MicSolo))) micGain = 0.0f;
  float musGain = params_.get(P::MusicFader);
  if (params_.getBool(P::MusicMute) || (anySolo && !params_.getBool(P::MusicSolo))) musGain = 0.0f;

  auto panGains = [](float pan, float& l, float& r) {
    const float theta = (std::clamp(pan, -1.0f, 1.0f) + 1.0f) * (dsp::kPi * 0.25f);
    l = std::cos(theta) * 1.41421356f * 0.70710678f;
    r = std::sin(theta) * 1.41421356f * 0.70710678f;
  };
  float ml, mr, xl, xr;
  panGains(params_.get(P::MicPan), ml, mr);
  panGains(params_.get(P::MusicPan), xl, xr);

  // Copy music into the working stereo buffer so the EQ can go in place.
  std::memcpy(I.mwL.data(), musicL, sizeof(float) * (size_t)n);
  std::memcpy(I.mwR.data(), musicR, sizeof(float) * (size_t)n);
  I.musEq.process(I.mwL.data(), I.mwR.data(), n);
  const float musPeak = std::max(peakOf(I.mwL.data(), n), peakOf(I.mwR.data(), n));
  musicPeak_.store(meterFollow(musicPeak_.load(), musPeak, 0.90f));

  // When the player's Windows session volume follows the MUSIC fader, the song
  // is already audible straight from Windows. Adding the captured copy to the
  // monitor bus as well would double (and delay) it, so in that mode the copy
  // only feeds the meter and the recording tap.
  const bool monitorMusic = !musicDirect_.load();

  for (int i = 0; i < n; ++i) {
    const float dry = mic[i] * micGain;
    I.micMixL[i] = dry * ml + I.wetL[i] * micGain;
    I.micMixR[i] = dry * mr + I.wetR[i] * micGain;
    I.musOutL[i] = I.mwL[i] * musGain * xl;
    I.musOutR[i] = I.mwR[i] * musGain * xr;
    outL[i] = I.micMixL[i] + (monitorMusic ? I.musOutL[i] : 0.0f);
    outR[i] = I.micMixR[i] + (monitorMusic ? I.musOutR[i] : 0.0f);
  }

  // --- Master bus ----------------------------------------------------------
  const float master = params_.get(P::MasterMute) ? 0.0f : params_.get(P::MasterFader);
  const float bal = params_.get(P::MasterBalance);
  const float balL = bal <= 0.0f ? 1.0f : 1.0f - bal;
  const float balR = bal >= 0.0f ? 1.0f : 1.0f + bal;

  if (params_.getBool(P::MasterCompOn)) {
    I.busL.setParams(params_.get(P::MasterCompThr), params_.get(P::MasterCompRatio), 15.0f, 220.0f,
                     6.0f, 0.0f, 1.0f, true);
    I.busR.setParams(params_.get(P::MasterCompThr), params_.get(P::MasterCompRatio), 15.0f, 220.0f,
                     6.0f, 0.0f, 1.0f, true);
    I.busL.process(outL, n);
    I.busR.process(outR, n);
  }

  for (int i = 0; i < n; ++i) {
    outL[i] = outL[i] * master * balL;
    outR[i] = outR[i] * master * balR;
  }

  I.limiter.process(outL, outR, n, 0.944f, params_.getBool(P::LimiterOn));
  limiterGr_.store(I.limiter.gainReductionDb());

  const float pkL = peakOf(outL, n), pkR = peakOf(outR, n);
  I.outMeterL = meterFollow(I.outMeterL, pkL, 0.90f);
  I.outMeterR = meterFollow(I.outMeterR, pkR, 0.90f);
  outPeakL_.store(I.outMeterL);
  outPeakR_.store(I.outMeterR);
  if (pkL > 0.999f || pkR > 0.999f) {
    // Clip LEDs are derived in the UI from a sustained peak above 0 dBFS.
  }

  // --- Recording tap -------------------------------------------------------
  if (recorder_.active()) {
    for (int i = 0; i < n; ++i) {
      float l = I.recordVocal ? mic[i] : outL[i];
      float r = I.recordVocal ? mic[i] : outR[i];
      if (!I.recordVocal && !monitorMusic) {
        // The monitor bus carries the vocal only in direct mode; a master
        // recording still has to contain the song.
        l += I.musOutL[i] * master * balL;
        r += I.musOutR[i] * master * balR;
      }
      I.recInter[(size_t)i * 2] = l;
      I.recInter[(size_t)i * 2 + 1] = r;
    }
    recorder_.push(I.recInter.data(), n, 2);
    const float pk = std::max(peakOf(outL, n), peakOf(outR, n));
    recPeak_.store(meterFollow(recPeak_.load(), pk, 0.9f));
  }
}

double Engine::measureOutputLatencyMs(const std::wstring& renderId, std::wstring* note) {
  au::ComScope com;
  HANDLE rev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  HANDLE cev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  au::Client out, lb;
  HRESULT h1 = out.Open(renderId, eRender, false, 48000, 2, 256, false, rev);
  if (FAILED(h1) || !out.isOpen()) {
    if (note) *note = L"Cannot open the output endpoint for measurement.";
    if (rev) CloseHandle(rev);
    if (cev) CloseHandle(cev);
    return -1.0;
  }
  HRESULT h2 = lb.Open(renderId, eRender, true, 48000, 2, 256, false, cev);
  if (FAILED(h2) || !lb.isOpen()) {
    if (note) *note = L"Loopback capture is unavailable on this endpoint.";
    if (rev) CloseHandle(rev);
    if (cev) CloseHandle(cev);
    return -1.0;
  }
  out.Start();
  lb.Start();
  LARGE_INTEGER freq, tStart;
  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&tStart);

  // Emit 250 ms of silence then a 1 kHz burst, and time its arrival.
  int sent = 0;
  const int total = 48000 / 2;
  const int burstStart = 4800;
  std::vector<BYTE*> pending;
  std::vector<UINT32> pendingFrames;
  bool burstSent = false;
  LARGE_INTEGER burstTime{};
  std::vector<float> captureBuf(4096 * 2);

  while (sent < total) {
    WaitForSingleObject(rev, 100);
    BYTE* data = nullptr;
    UINT32 frames = 0;
    if (out.GetRenderBuffer(&data, &frames) == S_OK && data && frames > 0) {
      float* f = (float*)data;
      for (UINT32 i = 0; i < frames; ++i) {
        const int idx = sent + (int)i;
        float v = 0.0f;
        if (idx >= burstStart && idx < burstStart + 480) {
          v = 0.35f * std::sin(2.0f * dsp::kPi * 1000.0f * (float)(idx - burstStart) / 48000.0f);
        }
        f[i * 2] = v;
        f[i * 2 + 1] = v;
      }
      if (!burstSent && sent + (int)frames > burstStart) {
        QueryPerformanceCounter(&burstTime);
        burstSent = true;
      }
      out.CommitRender(frames);
      sent += (int)frames;
    }
    // Drain loopback looking for the burst.
    for (int g = 0; g < 8; ++g) {
      BYTE* cdata = nullptr;
      UINT32 cframes = 0;
      HRESULT hr = lb.ReadNext(&cdata, &cframes);
      if (hr == S_FALSE) break;
      if (FAILED(hr)) break;
      if (cframes > 0 && cdata && burstSent) {
        float pk = 0.0f;
        au::Client::ToStereo(cdata, std::min<UINT32>(cframes, 4096), lb.channels(),
                             lb.bytesPerSample(), lb.isFloat(), captureBuf.data());
        for (UINT32 i = 0; i < std::min<UINT32>(cframes, 4096) * 2; ++i)
          pk = std::max(pk, std::fabs(captureBuf[i]));
        if (pk > 0.05f) {
          LARGE_INTEGER now;
          QueryPerformanceCounter(&now);
          const double ms = (double)(now.QuadPart - burstTime.QuadPart) * 1000.0 / (double)freq.QuadPart;
          lb.ReleaseCapture(cframes);
          lb.Close();
          out.Close();
          if (rev) CloseHandle(rev);
          if (cev) CloseHandle(cev);
          if (note) *note = L"Measured with a WASAPI loopback impulse (one buffer resolution).";
          return std::max(0.0, ms);
        }
      }
      lb.ReleaseCapture(cframes);
    }
  }
  lb.Close();
  out.Close();
  if (rev) CloseHandle(rev);
  if (cev) CloseHandle(cev);
  if (note) *note = L"No impulse detected in the loopback stream.";
  return -1.0;
}

} // namespace lvs
