#pragma once
// ---------------------------------------------------------------------------
// Real-time engine.
//
// Topology:
//   capture thread  -> mic ring (mono, engine rate)
//   loopback thread -> music ring (stereo, engine rate, drift corrected)
//   render thread (clock) -> vocal chain + music EQ -> master bus -> device
//
// No allocation, no locks and no OS calls (other than the disk recorder's
// lock-free push) happen inside the render path.
// ---------------------------------------------------------------------------
#include "audio/Recorder.h"
#include "audio/Wasapi.h"
#include "core/Params.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

namespace lvs {

struct EngineConfig {
  // ASIO takes priority when a driver CLSID is supplied: it is the only path
  // that reaches the hardware's native small buffers.
  std::wstring asioClsid;
  bool useAsio = false;
  std::wstring captureId;
  std::wstring renderId;
  std::wstring loopbackId;
  bool loopbackFollowDefault = true; // capture whatever Windows plays by default // empty == no karaoke capture
  int sampleRate = 48000;
  int bufferFrames = 128;
  int captureChannel = 0;   // zero based physical input on the interface
  int captureChannels = 2;  // physical input count requested
  bool exclusive = true;    // WASAPI exclusive low latency
  int outLeft = 0;
  int outRight = 1;
  int deviceChannels = 2;   // informational, refreshed on open
  bool captureEnabled = true;
  bool musicEnabled = true;
};

struct EngineStats {
  float inputPeak = 0.0f;
  float outputPeakL = 0.0f;
  float outputPeakR = 0.0f;
  float musicPeak = 0.0f;
  float compGr = 0.0f;
  float limiterGr = 0.0f;
  float cpuLoad = 0.0f;       // 0..1 of the audio budget
  double bufferLatencyMs = 0.0;
  double streamLatencyMs = 0.0;
  double roundTripMs = 0.0;   // estimated in + out
  int dropouts = 0;
  int underruns = 0;
  uint64_t frames = 0;
  int sampleRate = 0;
  int bufferFrames = 0;
  int deviceChannels = 0;
  bool running = false;
  bool renderOpen = false;
  bool captureOpen = false;
  bool musicOpen = false;
  bool exclusive = false;
  bool asio = false;
  double asioInputLatencyMs = 0.0;
  double asioOutputLatencyMs = 0.0;
  std::wstring asioDriver;
};

class Engine {
public:
  Engine();
  ~Engine();

  bool Start(const EngineConfig& cfg, std::wstring* error);
  void Stop();
  bool Restart(const EngineConfig& cfg, std::wstring* error);
  bool running() const { return running_.load(); }

  ParamStore& params() { return params_; }
  const ParamStore& params() const { return params_; }

  EngineConfig config() const;
  void setConfig(const EngineConfig&); // applied by the next Restart
  EngineStats stats() const;

  // Karaoke transport state (set by the UI, used for the music meter gate).
  void setMusicSourceActive(bool active) { musicActive_.store(active); }
  bool musicActive() const { return musicActive_.load(); }

  // True while the embedded player's Windows audio session is under fader
  // control. The song is then audible straight from Windows, so the monitor
  // bus must not add a second delayed copy of it.
  void setMusicDirect(bool direct) { musicDirect_.store(direct); }
  bool musicDirect() const { return musicDirect_.load(); }

  // Recording
  bool startRecording(const std::wstring& path, int bitDepth, bool recordVocal,
                      std::wstring* error);
  void stopRecording();
  bool recording() const { return recorder_.active(); }
  double recordingSeconds() const { return recorder_.seconds(); }
  float recordingPeak() const { return recPeak_.load(); }

  // Measurement helper: opens the render endpoint's loopback and reports the
  // measured output latency by timing an impulse. Returns -1 when unavailable.
  double measureOutputLatencyMs(const std::wstring& renderId, std::wstring* note);

  // ASIO driver discovery (registry), preferring the Focusrite.
  static std::wstring SuggestAsioClsid(std::wstring* driverName = nullptr);
  static std::vector<std::pair<std::wstring, std::wstring>> ListAsioDrivers();

  static std::wstring SuggestCaptureEndpoint();
  static std::wstring SuggestRenderEndpoint();
  static std::wstring SuggestLoopbackEndpoint();

private:
  void renderLoop();
  void asioLoop();
  void captureLoop();
  void musicLoop();
  void pullMusic(int frames);
  void process(float* dryMono, const float* musicL, const float* musicR, float* outL,
               float* outR, int frames);

  ParamStore params_;
  EngineConfig cfg_;
  Recorder recorder_;

  std::thread renderThread_, captureThread_, musicThread_;
  std::atomic<bool> running_{false}, stop_{false};
  std::atomic<bool> deviceLost_{false};
  std::atomic<bool> musicActive_{true};
  std::atomic<bool> musicDirect_{false};

  // meters (published to the UI thread)
  std::atomic<float> inPeak_{0}, outPeakL_{0}, outPeakR_{0}, musicPeak_{0};
  std::atomic<float> compGr_{0}, limiterGr_{0}, cpu_{0}, recPeak_{0};
  std::atomic<int> dropouts_{0}, underruns_{0};
  std::atomic<uint64_t> frames_{0};
  std::atomic<double> bufLat_{0}, streamLat_{0};
  std::atomic<int> sr_{0}, bufFrames_{0}, devCh_{0};
  std::atomic<bool> renderOpen_{false}, captureOpen_{false}, musicOpen_{false};
  std::atomic<bool> exclusive_{false};
  std::atomic<bool> asioActive_{false};
  std::atomic<double> asioInLat_{0}, asioOutLat_{0};
  std::atomic<int> openError_{0};
  std::wstring lastError_;

  struct Impl;
  Impl* impl_ = nullptr;
};

} // namespace lvs
