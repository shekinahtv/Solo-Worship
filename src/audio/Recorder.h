#pragma once
// ---------------------------------------------------------------------------
// Lock-free WAV recorder. The audio thread pushes interleaved float frames into
// a ring; a background thread writes 24-bit PCM or 32-bit float WAV.
// ---------------------------------------------------------------------------
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

namespace lvs {

class Recorder {
public:
  Recorder() = default;
  ~Recorder();

  bool start(const std::wstring& path, int sampleRate, int channels, int bitDepth,
             std::wstring* error);
  void stop();

  // Audio thread. Never blocks; drops the newest samples if the disk falls
  // behind (better than stalling the render callback).
  void push(const float* interleaved, int frames, int channels);

  bool active() const { return active_.load(std::memory_order_acquire); }
  double seconds() const {
    return sampleRate_ > 0 ? (double)written_.load(std::memory_order_relaxed) / sampleRate_ : 0.0;
  }
  uint64_t framesWritten() const { return written_.load(std::memory_order_relaxed); }
  uint64_t dropped() const { return dropped_.load(std::memory_order_relaxed); }
  const std::wstring& path() const { return path_; }

private:
  void diskLoop();
  void writeHeader(uint32_t dataBytes);
  bool writeRaw(const void* data, size_t bytes);

  std::vector<float> ring_;
  size_t ringMask_ = 0;
  std::atomic<uint64_t> writePos_{0};
  std::atomic<uint64_t> readPos_{0};
  std::atomic<uint64_t> written_{0};
  std::atomic<uint64_t> dropped_{0};
  std::atomic<bool> active_{false};
  std::atomic<bool> stop_{false};

  std::thread thread_;
  FILE* file_ = nullptr;
  std::wstring path_;
  int sampleRate_ = 48000;
  int channels_ = 2;
  int bitDepth_ = 24;
  uint64_t dataBytes_ = 0;
};

} // namespace lvs
