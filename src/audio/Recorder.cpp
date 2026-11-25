#include "audio/Recorder.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <windows.h>

namespace lvs {

namespace {
void put16(unsigned char* p, uint16_t v) {
  p[0] = (unsigned char)(v & 0xFF);
  p[1] = (unsigned char)(v >> 8);
}
void put32(unsigned char* p, uint32_t v) {
  p[0] = (unsigned char)(v & 0xFF);
  p[1] = (unsigned char)((v >> 8) & 0xFF);
  p[2] = (unsigned char)((v >> 16) & 0xFF);
  p[3] = (unsigned char)((v >> 24) & 0xFF);
}
} // namespace

Recorder::~Recorder() { stop(); }

bool Recorder::start(const std::wstring& path, int sampleRate, int channels, int bitDepth,
                     std::wstring* error) {
  if (active()) stop();
  path_ = path;
  sampleRate_ = sampleRate;
  channels_ = std::max(1, channels);
  bitDepth_ = (bitDepth == 32) ? 32 : 24;
  dataBytes_ = 0;
  writePos_.store(0);
  readPos_.store(0);
  written_.store(0);
  dropped_.store(0);

  const size_t ringFrames = (size_t)sampleRate_ * 2; // ~2 s of slack
  ring_.assign(ringFrames * channels_, 0.0f);
  ringMask_ = ringFrames - 1; // sampleRate is power-of-two friendly enough; mask on frames
  // Guard: use modulo-safe mask by rounding the frame count up to a power of two.
  size_t pow2 = 1;
  while (pow2 < ringFrames) pow2 <<= 1;
  if (pow2 != ringFrames) {
    ring_.assign(pow2 * channels_, 0.0f);
    ringMask_ = pow2 - 1;
  }

  file_ = _wfopen(path.c_str(), L"wb");
  if (!file_) {
    if (error) *error = L"Cannot create recording file (check the folder and permissions).";
    return false;
  }
  writeHeader(0);
  stop_.store(false);
  active_.store(true, std::memory_order_release);
  thread_ = std::thread([this] { diskLoop(); });
  return true;
}

void Recorder::writeHeader(uint32_t dataBytes) {
  if (!file_) return;
  unsigned char h[44] = {};
  memcpy(h, "RIFF", 4);
  put32(h + 4, 36 + dataBytes);
  memcpy(h + 8, "WAVE", 4);
  memcpy(h + 12, "fmt ", 4);
  put32(h + 16, 16);
  put16(h + 20, 1); // PCM; patched to 3 for float below
  put16(h + 22, (uint16_t)channels_);
  put32(h + 24, (uint32_t)sampleRate_);
  const int bits = bitDepth_;
  const int blockAlign = channels_ * bits / 8;
  put32(h + 28, (uint32_t)(sampleRate_ * blockAlign));
  put16(h + 32, (uint16_t)blockAlign);
  put16(h + 34, (uint16_t)bits);
  memcpy(h + 36, "data", 4);
  put32(h + 40, dataBytes);
  if (bitDepth_ == 32) put16(h + 20, 3); // IEEE float
  _fseeki64(file_, 0, SEEK_SET);
  fwrite(h, 1, 44, file_);
  fseek(file_, 0, SEEK_END);
}

void Recorder::push(const float* interleaved, int frames, int channels) {
  if (!active_.load(std::memory_order_acquire)) return;
  const int ch = std::min(channels, channels_);
  const uint64_t w = writePos_.load(std::memory_order_relaxed);
  const uint64_t r = readPos_.load(std::memory_order_acquire);
  const size_t capacity = ringMask_ + 1;
  if ((w - r) + (uint64_t)frames > capacity) {
    dropped_.fetch_add((uint64_t)frames, std::memory_order_relaxed);
    return;
  }
  for (int i = 0; i < frames; ++i) {
    const size_t idx = (size_t)((w + (uint64_t)i) & ringMask_);
    for (int c = 0; c < channels_; ++c) {
      ring_[idx * channels_ + c] = c < ch ? interleaved[i * channels + c] : 0.0f;
    }
  }
  writePos_.store(w + (uint64_t)frames, std::memory_order_release);
}

void Recorder::diskLoop() {
  std::vector<float> chunk;
  chunk.resize(4096 * (size_t)channels_);
  while (true) {
    const uint64_t w = writePos_.load(std::memory_order_acquire);
    const uint64_t r = readPos_.load(std::memory_order_relaxed);
    const bool stopping = stop_.load(std::memory_order_acquire);
    uint64_t available = w - r;
    if (available == 0) {
      if (stopping) break;
      Sleep(4);
      continue;
    }
    const size_t chunkFrames = std::min<uint64_t>(available, 4096);
    for (size_t i = 0; i < chunkFrames * channels_; ++i) {
      const size_t idx = (size_t)((r + i / channels_) & ringMask_) * channels_ + (i % channels_);
      chunk[i] = ring_[idx];
    }
    readPos_.store(r + chunkFrames, std::memory_order_release);
    written_.fetch_add(chunkFrames, std::memory_order_relaxed);

    if (bitDepth_ == 32) {
      std::vector<unsigned char> bytes(chunkFrames * channels_ * 4);
      memcpy(bytes.data(), chunk.data(), chunkFrames * channels_ * sizeof(float));
      writeRaw(bytes.data(), bytes.size());
      dataBytes_ += bytes.size();
    } else {
      const size_t samples = chunkFrames * channels_;
      std::vector<unsigned char> bytes(samples * 3);
      for (size_t i = 0; i < samples; ++i) {
        float v = std::clamp(chunk[i], -1.0f, 1.0f);
        int s = (int)std::lround(v * 8388607.0f);
        if (s > 8388607) s = 8388607;
        if (s < -8388608) s = -8388608;
        bytes[i * 3 + 0] = (unsigned char)(s & 0xFF);
        bytes[i * 3 + 1] = (unsigned char)((s >> 8) & 0xFF);
        bytes[i * 3 + 2] = (unsigned char)((s >> 16) & 0xFF);
      }
      writeRaw(bytes.data(), bytes.size());
      dataBytes_ += bytes.size();
    }
  }
}

bool Recorder::writeRaw(const void* data, size_t bytes) {
  if (!file_) return false;
  return fwrite(data, 1, bytes, file_) == bytes;
}

void Recorder::stop() {
  stop_.store(true, std::memory_order_release);
  if (thread_.joinable()) thread_.join();
  active_.store(false, std::memory_order_release);
  if (file_) {
    writeHeader((uint32_t)dataBytes_);
    fclose(file_);
    file_ = nullptr;
  }
}

} // namespace lvs
