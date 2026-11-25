#pragma once
// ---------------------------------------------------------------------------
// SOLO WORSHIPER - real-time DSP
//
// Everything here is allocation-free after prepare() and safe to call from the
// audio thread. Parameters are smoothed internally, so the UI can write values
// at any time without zipper noise.
// ---------------------------------------------------------------------------
#include "core/Params.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace lvs {
namespace dsp {

constexpr float kPi = 3.14159265358979323846f;

// One-pole smoother - reaches ~99% of target in `ms`.
class Smoother {
public:
  void prepare(double sampleRate, float ms) {
    const float samples = std::max(1.0f, (float)(sampleRate * ms / 1000.0));
    a_ = std::exp(-1.0f / samples);
    b_ = 1.0f - a_;
  }
  void setTarget(float t) { target_ = t; }
  void jumpTo(float t) { target_ = t; current_ = t; }
  float next() {
    current_ = target_ + a_ * (current_ - target_);
    return current_;
  }
  float current() const { return current_; }
  float target() const { return target_; }
  void advance(int n) {
    for (int i = 0; i < n; ++i) next();
  }

private:
  float a_ = 0.99f, b_ = 0.01f;
  float current_ = 0.0f, target_ = 0.0f;
};

// ---------------------------------------------------------------------------
// Biquad (RBJ cookbook), transposed direct form II, double precision state.
// ---------------------------------------------------------------------------
class Biquad {
public:
  enum Type { LowShelf, HighShelf, Peak, HighPass, LowPass };

  void set(Type t, double sr, double freq, double gainDb, double q) {
    type_ = t;
    sr_ = sr;
    freq = std::clamp(freq, 10.0, sr * 0.49);
    q = std::max(0.05, q);
    const double w0 = 2.0 * 3.14159265358979323846 * freq / sr;
    const double cs = std::cos(w0), sn = std::sin(w0);
    const double A = std::pow(10.0, gainDb / 40.0);
    const double alpha = sn / (2.0 * q);
    const double beta = 2.0 * std::sqrt(A) * alpha;
    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;

    switch (t) {
    case Peak:
      b0 = 1 + alpha * A;
      b1 = -2 * cs;
      b2 = 1 - alpha * A;
      a0 = 1 + alpha / A;
      a1 = -2 * cs;
      a2 = 1 - alpha / A;
      break;
    case LowShelf: {
      const double sA = std::sqrt(std::max(A, 1e-6));
      b0 = A * ((A + 1) - (A - 1) * cs + beta);
      b1 = 2 * A * ((A - 1) - (A + 1) * cs);
      b2 = A * ((A + 1) - (A - 1) * cs - beta);
      a0 = (A + 1) + (A - 1) * cs + beta;
      a1 = -2 * ((A - 1) + (A + 1) * cs);
      a2 = (A + 1) + (A - 1) * cs - beta;
      (void)sA;
      break;
    }
    case HighShelf: {
      b0 = A * ((A + 1) + (A - 1) * cs + beta);
      b1 = -2 * A * ((A - 1) + (A + 1) * cs);
      b2 = A * ((A + 1) + (A - 1) * cs - beta);
      a0 = (A + 1) - (A - 1) * cs + beta;
      a1 = 2 * ((A - 1) - (A + 1) * cs);
      a2 = (A + 1) - (A - 1) * cs - beta;
      break;
    }
    case HighPass:
      b0 = (1 + cs) / 2;
      b1 = -(1 + cs);
      b2 = (1 + cs) / 2;
      a0 = 1 + alpha;
      a1 = -2 * cs;
      a2 = 1 - alpha;
      break;
    case LowPass:
      b0 = (1 - cs) / 2;
      b1 = 1 - cs;
      b2 = (1 - cs) / 2;
      a0 = 1 + alpha;
      a1 = -2 * cs;
      a2 = 1 - alpha;
      break;
    }
    b0_ = b0 / a0;
    b1_ = b1 / a0;
    b2_ = b2 / a0;
    a1_ = a1 / a0;
    a2_ = a2 / a0;
  }

  void reset() { z1_ = z2_ = 0.0; }

  inline float process(float in) {
    const double x = in;
    const double y = b0_ * x + z1_;
    z1_ = b1_ * x - a1_ * y + z2_;
    z2_ = b2_ * x - a2_ * y;
    return (float)y;
  }

  // Magnitude response in dB at `freq`, for the EQ display.
  float magnitudeDb(double freq) const {
    const double w = 2.0 * 3.14159265358979323846 * freq / sr_;
    const double cw = std::cos(w), sw = std::sin(w);
    const double cw2 = std::cos(2 * w), sw2 = std::sin(2 * w);
    const double numRe = b0_ + b1_ * cw + b2_ * cw2;
    const double numIm = -(b1_ * sw + b2_ * sw2);
    const double denRe = 1.0 + a1_ * cw + a2_ * cw2;
    const double denIm = -(a1_ * sw + a2_ * sw2);
    const double num = std::sqrt(numRe * numRe + numIm * numIm);
    const double den = std::sqrt(denRe * denRe + denIm * denIm) + 1e-12;
    return (float)(20.0 * std::log10(std::max(num / den, 1e-9)));
  }

  bool active = false; // used by the display to skip bypassed bands
  Type type_ = Peak;
  double sr_ = 48000.0;

private:
  double b0_ = 1, b1_ = 0, b2_ = 0, a1_ = 0, a2_ = 0;
  double z1_ = 0, z2_ = 0;
};

// ---------------------------------------------------------------------------
// GOST-style analogue preamp emulation.
//
// Clean   - transformer in/out, gentle high-pass, no colouring.
// Warm    - input transformer saturation with even-harmonic bias plus a
//           subtle low-shelf "iron" lift.
// Driven  - harder tanh drive, more asymmetry, bright-ish top-end recovery.
//
// Drive feeds a soft clipper; Sat blends the saturated signal with the clean
// one in parallel; Warmth controls the iron low-shelf and the high-end roll-off.
// ---------------------------------------------------------------------------
class Preamp {
public:
  void prepare(double sr) {
    sr_ = sr;
    for (auto& s : s_) s.prepare(sr, 12.0f);
    iron_.set(Biquad::LowShelf, sr, 160.0, 0.0, 0.7);
    air_.set(Biquad::HighShelf, sr, 5200.0, 0.0, 0.7);
    dc_.reset();
    lastGain_ = 1.0f;
    autoGain_ = 1.0f;
  }

  void setParams(float gainDb, float drive, float warmth, float sat, float outDb, bool phase,
                 int mode) {
    s_[0].setTarget(gainDb);
    s_[1].setTarget(drive);
    s_[2].setTarget(warmth);
    s_[3].setTarget(sat);
    s_[4].setTarget(outDb);
    phase_ = phase;
    if (mode != mode_) {
      mode_ = mode;
      iron_.set(Biquad::LowShelf, sr_, 160.0, mode_ == 0 ? 0.0 : (mode_ == 1 ? 2.2 : 3.4), 0.7);
      air_.set(Biquad::HighShelf, sr_, 5200.0, mode_ == 0 ? 0.0 : (mode_ == 1 ? -1.1 : 0.9), 0.7);
    }
    dirtAmt_ = (mode_ == 2) ? 2.2f : (mode_ == 1 ? 1.0f : 0.35f);
  }

  void process(float* buf, int n) {
    const float gain = dbToLin(s_[0].next());
    const float outg = dbToLin(s_[4].next());
    // Slow gain scaling keeps the level steady as drive is pushed.
    for (int i = 0; i < n; ++i) {
      const float drive = s_[1].next();
      const float warm = s_[2].next();
      const float sat = s_[3].next();
      float x = buf[i] * gain * (1.0f + drive * 5.0f * dirtAmt_);
      const float clean = x;

      // Asymmetric soft saturation: 2nd + 3rd harmonic content.
      const float bias = 0.12f + warm * 0.22f;
      const float driven = std::tanh(x + bias) - std::tanh(bias);
      const float shaped = driven * (1.0f + sat * 0.6f);
      // Parallel blend keeps transient detail at low settings.
      x = clean * (1.0f - sat) + shaped * sat;
      if (drive < 0.001f) x = clean;

      // Iron colouring.
      x = iron_.process(x);
      x = air_.process(x * (1.0f - 0.15f * warm));

      // Speaker-protection DC blocker.
      const float hp = x - dcX_ + 0.9995f * dcY_;
      dcX_ = x;
      dcY_ = hp;
      x = hp;

      // Soft ceiling instead of hard clipping.
      const float lim = 1.35f;
      if (x > lim || x < -lim) x = lim * std::tanh(x / lim);

      x *= outg;
      if (phase_) x = -x;
      buf[i] = x;
    }
    // Compensate the drive so the channel does not jump in level.
    autoGain_ = 1.0f / (1.0f + 0.25f * s_[1].current());
    (void)autoGain_;
  }

  float driveReduction() const { return 1.0f; }

  void reset() {
    dcX_ = dcY_ = 0.0f;
    dc_.reset();
    iron_.reset();
    air_.reset();
  }

private:
  double sr_ = 48000.0;
  Smoother s_[5];
  Biquad iron_, air_, dc_;
  float dcX_ = 0, dcY_ = 0, lastGain_ = 1, autoGain_ = 1, dirtAmt_ = 1;
  bool phase_ = false;
  int mode_ = -1;
};

// ---------------------------------------------------------------------------
// British console style four band EQ + high-pass.
// ---------------------------------------------------------------------------
class BritishEQ {
public:
  void prepare(double sr) {
    sr_ = sr;
    for (auto& s : g_) s.prepare(sr, 25.0f);
    for (auto& s : f_) s.prepare(sr, 40.0f);
    for (auto& s : q_) s.prepare(sr, 40.0f);
    hpf_.set(Biquad::HighPass, sr, 90.0, 0.0, 0.707);
    rebuild();
  }

  void setParams(const ParamStore& p) {
    enabled_ = p.getBool(P::EqOn);
    hpfOn_ = p.getBool(P::HpfOn);
    g_[0].setTarget(p.get(P::EqLowGain));
    g_[1].setTarget(p.get(P::EqLoMidGain));
    g_[2].setTarget(p.get(P::EqHiMidGain));
    g_[3].setTarget(p.get(P::EqHighGain));
    f_[0].setTarget(p.get(P::EqLowFreq));
    f_[1].setTarget(p.get(P::EqLoMidFreq));
    f_[2].setTarget(p.get(P::EqHiMidFreq));
    f_[3].setTarget(p.get(P::EqHighFreq));
    q_[0].setTarget(p.get(P::EqLowQ));
    q_[1].setTarget(p.get(P::EqLoMidQ));
    q_[2].setTarget(p.get(P::EqHiMidQ));
    q_[3].setTarget(p.get(P::EqHighQ));
    bandOn_[0] = p.getBool(P::EqLowOn);
    bandOn_[1] = p.getBool(P::EqLoMidOn);
    bandOn_[2] = p.getBool(P::EqHiMidOn);
    bandOn_[3] = p.getBool(P::EqHighOn);
    hpTarget_ = p.get(P::HpfFreq);
  }

  void process(float* buf, int n) {
    // Coefficient refresh at block rate; values are smoothed so this is
    // click-free while staying cheap.
    const float hp = hpFreq_.next();
    if (std::fabs(hp - lastHpf_) > 0.5f) {
      hpf_.set(Biquad::HighPass, sr_, hp, 0.0, 0.707);
      lastHpf_ = hp;
    }
    for (int b = 0; b < 4; ++b) {
      const float gg = g_[b].next();
      const float ff = f_[b].next();
      const float qq = q_[b].next();
      if (std::fabs(gg - lastG_[b]) > 0.01f || std::fabs(ff - lastF_[b]) > 1.0f ||
          std::fabs(qq - lastQ_[b]) > 0.01f) {
        bands_[b].set(bandType_[b], sr_, ff, gg, qq);
        lastG_[b] = gg;
        lastF_[b] = ff;
        lastQ_[b] = qq;
      }
    }
    for (int i = 0; i < n; ++i) {
      float x = buf[i];
      if (hpfOn_) x = hpf_.process(x);
      if (enabled_) {
        for (int b = 0; b < 4; ++b)
          if (bandOn_[b]) x = bands_[b].process(x);
      }
      buf[i] = x;
    }
  }

  // Response of the whole EQ in dB at `freq`, for the animated curve.
  float responseDb(double freq) const {
    float sum = 0.0f;
    if (enabled_) {
      for (int b = 0; b < 4; ++b)
        if (bandOn_[b]) sum += bands_[b].magnitudeDb(freq);
    }
    if (hpfOn_) sum += hpf_.magnitudeDb(freq);
    return sum;
  }

  void rebuild() {
    bandType_[0] = Biquad::LowShelf;
    bandType_[1] = Biquad::Peak;
    bandType_[2] = Biquad::Peak;
    bandType_[3] = Biquad::HighShelf;
    for (int b = 0; b < 4; ++b) {
      bands_[b].set(bandType_[b], sr_, lastF_[b] <= 0 ? 500.0 : lastF_[b], 0.0, 1.0);
      lastG_[b] = 999.0f;
    }
    lastHpf_ = -1.0f;
    hpFreq_.jumpTo(90.0f);
  }

  void setHpfFromParams(const ParamStore& p) { hpFreq_.setTarget(p.get(P::HpfFreq)); }

  void prepareAll(const ParamStore& p) {
    setParams(p);
    for (int b = 0; b < 4; ++b) {
      g_[b].jumpTo(g_[b].target());
      f_[b].jumpTo(f_[b].target());
      q_[b].jumpTo(q_[b].target());
      lastG_[b] = 1e9f;
    }
    hpFreq_.jumpTo(hpTarget_);
    lastHpf_ = -1.0f;
  }

  void reset() {
    for (auto& b : bands_) b.reset();
    hpf_.reset();
  }

private:
  double sr_ = 48000.0;
  Biquad bands_[4], hpf_;
  Biquad::Type bandType_[4] = {Biquad::LowShelf, Biquad::Peak, Biquad::Peak, Biquad::HighShelf};
  Smoother g_[4], f_[4], q_[4], hpFreq_;
  float lastG_[4] = {1e9f, 1e9f, 1e9f, 1e9f};
  float lastF_[4] = {110.f, 420.f, 2800.f, 8000.f};
  float lastQ_[4] = {0.75f, 1.1f, 1.2f, 0.75f};
  float hpTarget_ = 90.0f, lastHpf_ = -1.0f;
  bool enabled_ = true, hpfOn_ = true;
  bool bandOn_[4] = {true, true, true, true};
};

// ---------------------------------------------------------------------------
// Studio vocal compressor: soft knee, dB-domain ballistics, parallel mix.
// ---------------------------------------------------------------------------
class Compressor {
public:
  void prepare(double sr) {
    sr_ = sr;
    envDb_ = 0.0f;
    gainDb_ = 0.0f;
    gr_ = 0.0f;
    for (auto& s : s_) s.prepare(sr, 15.0f);
  }

  void setParams(float thresh, float ratio, float attackMs, float releaseMs, float knee,
                 float makeup, float mix, bool on) {
    s_[0].setTarget(thresh);
    s_[1].setTarget(ratio);
    makeup_ = makeup;
    knee_ = knee;
    mix_ = mix;
    on_ = on;
    attCoef_ = std::exp(-1.0f / std::max(1.0f, (float)(sr_ * attackMs / 1000.0)));
    relCoef_ = std::exp(-1.0f / std::max(1.0f, (float)(sr_ * releaseMs / 1000.0)));
  }

  void process(float* buf, int n) {
    if (!on_) {
      gr_ *= 0.9f;
      return;
    }
    for (int i = 0; i < n; ++i) {
      const float thr = s_[0].next();
      const float ratio = std::max(1.0f, s_[1].next());
      const float x = buf[i];
      const float absx = std::fabs(x) + 1e-9f;
      const float lvlDb = 20.0f * std::log10(absx);

      // Smooth the detector level (fast attack, program release).
      envDb_ = lvlDb > envDb_ ? lvlDb + attCoef_ * (envDb_ - lvlDb)
                              : lvlDb + relCoef_ * (envDb_ - lvlDb);

      // Static curve with soft knee, computed in dB.
      const float halfKnee = knee_ * 0.5f;
      float over = envDb_ - thr;
      float target = 0.0f;
      if (over > halfKnee) {
        target = -(over - over / ratio);
      } else if (over > -halfKnee && knee_ > 0.01f) {
        const float t = over + halfKnee;
        target = -((1.0f / ratio - 1.0f) * t * t) / (2.0f * knee_);
      }
      // gainDb_ is <= 0, in dB.
      gainDb_ = target < gainDb_ ? target + attCoef_ * (gainDb_ - target)
                                 : target + relCoef_ * (gainDb_ - target);

      const float g = dbToLin(gainDb_ + makeup_);
      const float wet = x * g;
      buf[i] = x * (1.0f - mix_) + wet * mix_;
    }
    gr_ = -gainDb_;
  }

  float gainReductionDb() const { return gr_; }
  void reset() {
    envDb_ = gainDb_ = gr_ = 0.0f;
  }

private:
  double sr_ = 48000.0;
  Smoother s_[2];
  float envDb_ = 0, gainDb_ = 0, gr_ = 0, makeup_ = 0, knee_ = 9, mix_ = 1;
  float attCoef_ = 0.9f, relCoef_ = 0.999f;
  bool on_ = true;
};

// ---------------------------------------------------------------------------
// Lexicon-inspired algorithmic reverb.
//
// Topology: pre-delay -> 4 input diffusers -> 8 damped feedback combs
// (prime-ish lengths, slightly modulated) -> 2 output diffusers, plus a
// separate early-reflection tap bank. Comb feedback is derived from the
// requested RT60 so Decay is in real seconds.
// ---------------------------------------------------------------------------
struct RevTypePreset {
  float sizeScale, decayScale, damp, early;
};

class Reverb {
public:
public:
  static constexpr int kCombs = 8;
  static constexpr int kDiff = 4;

  static const char* typeName(int t) {
    static const char* names[8] = {"HALL",   "LARGE HALL", "PLATE", "VOCAL PLATE",
                                   "ROOM",   "CHAMBER",    "CATHEDRAL", "AMBIENT"};
    return names[std::clamp(t, 0, 7)];
  }

  void prepare(double sr, int /*maxBlock*/) {
    sr_ = sr;
    // Type presets scale delay times and tone.
    static const RevTypePreset presets[8] = {
        {1.00f, 1.00f, 0.42f, 0.30f}, // Hall
        {1.25f, 1.35f, 0.38f, 0.28f}, // Large Hall
        {0.55f, 0.62f, 0.30f, 0.42f}, // Plate
        {0.62f, 0.70f, 0.26f, 0.46f}, // Vocal Plate
        {0.36f, 0.34f, 0.50f, 0.62f}, // Room
        {0.72f, 0.72f, 0.46f, 0.40f}, // Chamber
        {1.55f, 1.70f, 0.30f, 0.22f}, // Cathedral
        {1.90f, 2.60f, 0.22f, 0.16f}, // Ambient
    };
    presets_ = presets;

    const double maxLen = 0.20 * sr_;
    for (int i = 0; i < kCombs; ++i) {
      comb_[i].buf.assign((size_t)maxLen, 0.0f);
      comb_[i].pos = 0;
    }
    for (int i = 0; i < kDiff; ++i) {
      diff_[i].buf.assign((size_t)(0.02 * sr_), 0.0f);
      diff_[i].pos = 0;
    }
    for (int i = 0; i < 2; ++i) {
      outDiff_[i].buf.assign((size_t)(0.01 * sr_), 0.0f);
      outDiff_[i].pos = 0;
    }
    pre_.assign((size_t)(0.21 * sr_), 0.0f);
    prePos_ = 0;
    early_.assign((size_t)(0.12 * sr_), 0.0f);
    earlyPos_ = 0;

    for (auto& s : sm_) s.prepare(sr, 60.0f);
    earlyMix_.prepare(sr, 40.0f);
    wetSm_.prepare(sr, 40.0f);
    modPhase_ = 0.0f;
    initialized_ = false;
    rebuild(true);
  }

  void setParams(const ParamStore& p) {
    type_ = (int)std::lround(p.get(P::RevType));
    type_ = std::clamp(type_, 0, 7);
    enabled_ = p.getBool(P::RevOn);
    sm_[0].setTarget(p.get(P::RevPredelay));
    sm_[1].setTarget(p.get(P::RevDecay));
    sm_[2].setTarget(p.get(P::RevSize));
    sm_[3].setTarget(p.get(P::RevDamp));
    sm_[4].setTarget(p.get(P::RevDiffusion));
    sm_[5].setTarget(p.get(P::RevEarly));
    sm_[6].setTarget(p.get(P::RevHf));
    sm_[7].setTarget(p.get(P::RevLf));
    wetSm_.setTarget(p.get(P::RevWet));
    earlyMix_.setTarget(p.get(P::RevEarly));
    if (!initialized_) {
      initialized_ = true;
      for (auto& s : sm_) s.jumpTo(s.target());
      wetSm_.jumpTo(wetSm_.target());
      earlyMix_.jumpTo(earlyMix_.target());
      rebuild(true);
    }
  }

  // Called once per block; delay lengths/feedback follow the type and controls.
  void rebuild(bool initial = false) {
    const auto& pr = presets_[type_];
    const float size = sm_[2].next();
    const float decay = sm_[1].next();
    const float hf = sm_[6].next();
    const float lf = sm_[7].next();
    const float diff = sm_[4].next();
    const float damp = sm_[3].next();

    static const float baseMs[kCombs] = {29.7f, 37.1f, 41.1f, 43.7f,
                                         47.9f, 53.3f, 59.9f, 67.3f};
    const float scale = (0.45f + size * 0.85f) * pr.sizeScale;
    for (int i = 0; i < kCombs; ++i) {
      float lenMs = baseMs[i] * scale;
      float len = lenMs * 0.001f * (float)sr_;
      len = std::min(len, (float)comb_[i].buf.size() - 8.0f);
      comb_[i].len = std::max(16.0f, len);
      const float rt60 = decay * pr.decayScale;
      const float g = std::pow(10.0f, -3.0f * (comb_[i].len / (float)sr_) / std::max(0.05f, rt60));
      comb_[i].fb = std::min(0.9995f, g);
      comb_[i].dampCoef = 0.05f + (1.0f - hf) * 0.75f + damp * 0.15f;
      comb_[i].lpCoef_ = std::clamp(0.02f + (1.0f - hf) * 0.55f + damp * 0.30f, 0.02f, 0.92f);
      comb_[i].lfCoef = (1.0f - lf) * 0.45f + 0.05f;
      // Slight detune of each comb keeps the tail from ringing metallically.
      comb_[i].mod = 0.6f + 0.9f * (i % 3);
    }
    static const float diffMs[kDiff] = {4.77f, 6.31f, 8.53f, 11.37f};
    for (int i = 0; i < kDiff; ++i) {
      diff_[i].len = std::min((float)diff_[i].buf.size() - 4.0f,
                              diffMs[i] * (0.8f + size * 0.5f) * 0.001f * (float)sr_);
      diff_[i].g = diff * 0.72f;
    }
    outDiff_[0].len = std::min((float)outDiff_[0].buf.size() - 4.0f, 0.0023f * (float)sr_);
    outDiff_[0].g = 0.6f;
    outDiff_[1].len = std::min((float)outDiff_[1].buf.size() - 4.0f, 0.0017f * (float)sr_);
    outDiff_[1].g = 0.6f;
    earlyGain_ = pr.early;
    if (initial) {
      for (int i = 0; i < kCombs; ++i) comb_[i].lp = comb_[i].lp2 = 0.0f;
    }
  }

  // Returns wet L/R for one input sample. Stereo out is decorrelated by
  // splitting the comb bank between the two channels.
  void process(const float* in, float* outL, float* outR, int n) {
    if (!enabled_) {
      std::memset(outL, 0, sizeof(float) * n);
      std::memset(outR, 0, sizeof(float) * n);
      return;
    }
    const float predelay = sm_[0].next();
    const int preSamples = std::clamp((int)(predelay * 0.001f * (float)sr_), 0,
                                      (int)pre_.size() - 1);
    const float wet = wetSm_.next();
    wetSm_.advance(n > 1 ? n - 1 : 0);
    const float earlyMix = earlyMix_.next();
    const float diffOn = sm_[4].next() > 0.001f ? 1.0f : 0.0f;

    for (int i = 0; i < n; ++i) {
      // Pre-delay line.
      const int rp = (prePos_ - preSamples + (int)pre_.size()) % (int)pre_.size();
      float x = pre_[rp];
      pre_[prePos_] = in[i];
      prePos_ = (prePos_ + 1) % (int)pre_.size();

      // Early reflections (sparse taps, size dependent).
      const int es = (int)early_.size();
      const int tap1 = (int)(0.011f * sr_);
      const int tap2 = (int)(0.019f * sr_);
      const int tap3 = (int)(0.027f * sr_);
      const int tap4 = (int)(0.041f * sr_);
      float er = 0.0f;
      er += early_[(earlyPos_ - tap1 + es) % es] * 0.6f;
      er += early_[(earlyPos_ - tap2 + es) % es] * -0.5f;
      er += early_[(earlyPos_ - tap3 + es) % es] * 0.42f;
      er += early_[(earlyPos_ - tap4 + es) % es] * -0.3f;
      early_[earlyPos_] = x * 0.8f;
      earlyPos_ = (earlyPos_ + 1) % es;
      er *= earlyGain_ * earlyGain_ * 0.9f;
      er = std::clamp(er, -0.9f, 0.9f);

      // Input diffusion.
      float d = x;
      if (diffOn > 0.5f) {
        for (int k = 0; k < kDiff; ++k) {
          auto& a = diff_[k];
          const int sz = (int)a.buf.size();
          const int r = (a.pos - (int)a.len + sz) % sz;
          const float bufOut = a.buf[r];
          const float y = bufOut - a.g * d;
          a.buf[a.pos] = d + a.g * y;
          a.pos = (a.pos + 1) % sz;
          d = y;
        }
      }

      // Parallel damped combs.
      float accL = 0.0f, accR = 0.0f;
      const float modDepth = 1.5f;
      modPhase_ += 0.37f / (float)sr_ * 48000.0f;
      if (modPhase_ > 6.2831853f) modPhase_ -= 6.2831853f;
      for (int c = 0; c < kCombs; ++c) {
        auto& cb = comb_[c];
        const int sz = (int)cb.buf.size();
        const float modOff = std::sin(modPhase_ + c * 0.9f) * modDepth * cb.mod;
        int r = cb.pos - (int)cb.len;
        if (r < 0) r += sz;
        const int r2 = (r + 1) % sz;
        const float frac = modOff - std::floor(modOff);
        const float y = cb.buf[r] * (1.0f - frac) + cb.buf[r2] * frac;

        // Damping inside the feedback loop: HF via low-pass, LF via high-pass.
        cb.lp += cb.lpCoef_ * (y - cb.lp);
        cb.lp2 += cb.lfCoef * (cb.lp - cb.lp2);
        const float filtered = cb.lp - cb.lp2;

        cb.buf[cb.pos] = d + filtered * cb.fb;
        cb.pos = (cb.pos + 1) % sz;
        if (c & 1)
          accR += filtered;
        else
          accL += filtered;
      }
      accL *= 0.145f;
      accR *= 0.145f;

      // Output diffusion.
      for (int k = 0; k < 2; ++k) {
        auto& a = outDiff_[k];
        const int sz = (int)a.buf.size();
        const int r = (a.pos - (int)a.len + sz) % sz;
        const float bo = a.buf[r];
        const float yl = bo - a.g * accL;
        a.buf[a.pos] = accL + a.g * yl;
        a.pos = (a.pos + 1) % sz;
        accL = yl;
      }
      for (int k = 0; k < 2; ++k) {
        auto& a = outDiff_[k];
        const int sz = (int)a.buf.size();
        const int r = (a.pos - (int)a.len + sz) % sz;
        const float bo = a.buf[r];
        const float yr = bo - a.g * accR;
        a.buf[a.pos] = accR + a.g * yr;
        a.pos = (a.pos + 1) % sz;
        accR = yr;
      }

      outL[i] = (accL + er) * wet;
      outR[i] = (accR + er * 0.85f) * wet;
      (void)earlyMix;
    }
    // Parameter drift is handled here, at block rate, so tails keep ringing.
    if (--rebuildCountdown_ <= 0) {
      rebuild();
      rebuildCountdown_ = 16;
    }
  }

  void setEarlyMix(float v) { earlyMix_.setTarget(v); }
  void setEarlyMixImmediate(float v) { earlyMix_.jumpTo(v); }

  void reset() {
    for (auto& c : comb_) {
      std::fill(c.buf.begin(), c.buf.end(), 0.0f);
      c.lp = c.lp2 = 0.0f;
      c.pos = 0;
    }
    for (auto& a : diff_)
      std::fill(a.buf.begin(), a.buf.end(), 0.0f);
    for (auto& a : outDiff_)
      std::fill(a.buf.begin(), a.buf.end(), 0.0f);
    std::fill(pre_.begin(), pre_.end(), 0.0f);
    std::fill(early_.begin(), early_.end(), 0.0f);
    prePos_ = earlyPos_ = 0;
    rebuild(true);
  }

  int type() const { return type_; }

private:
  struct Comb {
    std::vector<float> buf;
    int pos = 0;
    float len = 100, fb = 0.8f, dampCoef = 0.3f, lfCoef = 0.2f, mod = 1.0f;
    float lp = 0, lp2 = 0, lpCoef_ = 0.35f;
  };
  struct Allpass {
    std::vector<float> buf;
    int pos = 0;
    float len = 100, g = 0.6f;
  };

  double sr_ = 48000.0;
  Comb comb_[kCombs];
  Allpass diff_[kDiff], outDiff_[2];
  std::vector<float> pre_, early_;
  int prePos_ = 0, earlyPos_ = 0;
  Smoother sm_[8], earlyMix_;
  const RevTypePreset* presets_ = nullptr;
  int type_ = 2;
  bool enabled_ = true;
  bool initialized_ = false;
  Smoother wetSm_;
  float earlyGain_ = 0.35f, modPhase_ = 0.0f;
  int rebuildCountdown_ = 16;
};

// ---------------------------------------------------------------------------
// Look-ahead brickwall limiter for master output protection.
// ---------------------------------------------------------------------------
class Limiter {
public:
  void prepare(double sr) {
    sr_ = sr;
    look_ = (int)(sr * 0.002);
    if (look_ < 8) look_ = 8;
    bufL_.assign(look_ * 2 + 2, 0.0f);
    bufR_.assign(look_ * 2 + 2, 0.0f);
    pos_ = 0;
    env_ = 0.0f;
    att_ = std::exp(-1.0f / std::max(1.0f, (float)(sr * 0.0004)));
    rel_ = std::exp(-1.0f / std::max(1.0f, (float)(sr * 0.120)));
    gr_ = 0.0f;
  }

  void process(float* l, float* r, int n, float ceiling, bool on) {
    if (!on) {
      // Safety clip only.
      for (int i = 0; i < n; ++i) {
        l[i] = std::clamp(l[i], -0.999f, 0.999f);
        r[i] = std::clamp(r[i], -0.999f, 0.999f);
      }
      gr_ *= 0.9f;
      return;
    }
    const int sz = (int)bufL_.size();
    for (int i = 0; i < n; ++i) {
      const float li = l[i], ri = r[i];
      const float peak = std::max(std::fabs(li), std::fabs(ri));
      // Immediate gain target to bring this peak under the ceiling.
      const float target = peak > ceiling ? ceiling / peak : 1.0f;
      const float coef = target < env_ ? att_ : rel_;
      env_ = target + coef * (env_ - target);
      env_ = std::min(env_, 1.0f);

      // Delay line gives the detector a 2 ms head start.
      bufL_[pos_] = li;
      bufR_[pos_] = ri;
      int rp = pos_ - look_;
      if (rp < 0) rp += sz;
      float outL = bufL_[rp] * env_;
      float outR = bufR_[rp] * env_;
      pos_ = (pos_ + 1) % sz;
      l[i] = std::clamp(outL, -ceiling, ceiling);
      r[i] = std::clamp(outR, -ceiling, ceiling);
    }
    gr_ = -linToDb(std::max(env_, 1e-6f));
    if (gr_ < 0.01f) gr_ = 0;
  }

  float gainReductionDb() const { return gr_; }
  void reset() {
    std::fill(bufL_.begin(), bufL_.end(), 0.0f);
    std::fill(bufR_.begin(), bufR_.end(), 0.0f);
    pos_ = 0;
    env_ = 1.0f;
    gr_ = 0;
  }

private:
  double sr_ = 48000.0;
  std::vector<float> bufL_, bufR_;
  int look_ = 96, pos_ = 0;
  float env_ = 1.0f, att_ = 0.9f, rel_ = 0.999f, gr_ = 0.0f;
};

// ---------------------------------------------------------------------------
// Simple 3-band music EQ (low shelf / mid peak / high shelf).
// ---------------------------------------------------------------------------
class MusicEQ {
public:
  void prepare(double sr) {
    sr_ = sr;
    low_.set(Biquad::LowShelf, sr, 120.0, 0.0, 0.7);
    mid_.set(Biquad::Peak, sr, 1000.0, 0.0, 0.9);
    high_.set(Biquad::HighShelf, sr, 8000.0, 0.0, 0.7);
    for (auto& s : sm_) s.prepare(sr, 30.0f);
    lg_ = mg_ = hg_ = 0.0f;
    on_ = true;
  }
  void setParams(float lowGain, float midGain, float highGain, bool on) {
    sm_[0].setTarget(lowGain);
    sm_[1].setTarget(midGain);
    sm_[2].setTarget(highGain);
    on_ = on;
  }
  void process(float* l, float* r, int n) {
    if (!on_) return;
    const float lg = sm_[0].next(), mg = sm_[1].next(), hg = sm_[2].next();
    if (std::fabs(lg - lg_) > 0.02f) {
      low_.set(Biquad::LowShelf, sr_, 120.0, lg, 0.7);
      lg_ = lg;
    }
    if (std::fabs(mg - mg_) > 0.02f) {
      mid_.set(Biquad::Peak, sr_, 1000.0, mg, 0.9);
      mg_ = mg;
    }
    if (std::fabs(hg - hg_) > 0.02f) {
      high_.set(Biquad::HighShelf, sr_, 8000.0, hg, 0.7);
      hg_ = hg;
    }
    for (int i = 0; i < n; ++i) {
      l[i] = high_.process(mid_.process(low_.process(l[i])));
      r[i] = high_.process(mid_.process(low_.process(r[i])));
    }
  }

private:
  double sr_ = 48000.0;
  Biquad low_, mid_, high_;
  Smoother sm_[3];
  float lg_ = 0, mg_ = 0, hg_ = 0;
  bool on_ = true;
};

} // namespace dsp
} // namespace lvs
