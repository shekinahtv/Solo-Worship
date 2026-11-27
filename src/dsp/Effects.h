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
  // Block-rate use: advance one sample worth per frame in the block and return
  // the settled value. Preparing a smoother in milliseconds and then calling
  // next() only once per block made every control move ~128x too slowly, which
  // is why the EQ and reverb knobs felt dead.
  float nextBlock(int n) {
    if (n <= 0) return current_;
    for (int i = 0; i < n; ++i) next();
    return current_;
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
    const float gain = dbToLin(s_[0].nextBlock(n));
    const float outg = dbToLin(s_[4].nextBlock(n));
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
    const float hp = hpFreq_.nextBlock(n);
    if (std::fabs(hp - lastHpf_) > 0.5f) {
      hpf_.set(Biquad::HighPass, sr_, hp, 0.0, 0.707);
      lastHpf_ = hp;
    }
    for (int b = 0; b < 4; ++b) {
      const float gg = g_[b].nextBlock(n);
      const float ff = f_[b].nextBlock(n);
      const float qq = q_[b].nextBlock(n);
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

      const float target = curveDb(envDb_, thr, ratio);
      // gainDb_ is <= 0, in dB.
      gainDb_ = target < gainDb_ ? target + attCoef_ * (gainDb_ - target)
                                 : target + relCoef_ * (gainDb_ - target);

      const float g = dbToLin(gainDb_ + makeup_);
      const float wet = x * g;
      buf[i] = x * (1.0f - mix_) + wet * mix_;
    }
    gr_ = -gainDb_;
  }

  // Linked stereo: one detector for both channels, so the phantom centre of
  // the master bus does not wander while the gain rides.
  void processLinked(float* l, float* r, int n) {
    if (!on_) {
      gr_ *= 0.9f;
      return;
    }
    for (int i = 0; i < n; ++i) {
      const float thr = s_[0].next();
      const float ratio = std::max(1.0f, s_[1].next());
      const float level = std::max(std::fabs(l[i]), std::fabs(r[i])) + 1e-9f;
      const float lvlDb = 20.0f * std::log10(level);
      envDb_ = lvlDb > envDb_ ? lvlDb + attCoef_ * (envDb_ - lvlDb)
                              : lvlDb + relCoef_ * (envDb_ - lvlDb);
      const float target = curveDb(envDb_, thr, ratio);
      gainDb_ = target < gainDb_ ? target + attCoef_ * (gainDb_ - target)
                                 : target + relCoef_ * (gainDb_ - target);
      const float g = dbToLin(gainDb_ + makeup_);
      const float dl = l[i] * (1.0f - mix_ + g * mix_);
      const float dr = r[i] * (1.0f - mix_ + g * mix_);
      l[i] = dl;
      r[i] = dr;
    }
    gr_ = -gainDb_;
  }

  float gainReductionDb() const { return gr_; }
  void reset() {
    envDb_ = gainDb_ = gr_ = 0.0f;
  }

private:
  // Static curve with soft knee, computed in dB. Returns the gain in dB (<= 0).
  float curveDb(float envDb, float thr, float ratio) const {
    const float halfKnee = knee_ * 0.5f;
    const float over = envDb - thr;
    float target = 0.0f;
    if (over > halfKnee) {
      target = -(over - over / ratio);
    } else if (over > -halfKnee && knee_ > 0.01f) {
      const float t = over + halfKnee;
      target = -((1.0f / ratio - 1.0f) * t * t) / (2.0f * knee_);
    }
    return target;
  }

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
  float sizeScale;   // scales the comb lengths
  float decayScale;  // scales the requested RT60
  float damp;        // extra tone darkening for the type
  float early;       // early-reflection weight
  float predelayMs;  // the type's own pre-delay starting point
  float modDepth;    // comb modulation depth in samples at 48 kHz
};

class Reverb {
public:
  static constexpr int kCombs = 8;
  static constexpr int kDiff = 4;

  static const char* typeName(int t) {
    static const char* names[8] = {"HALL",       "CONCERT HALL", "PLATE",     "VOCAL PLATE",
                                   "ROOM",       "CHAPEL",       "CATHEDRAL", "AMBIENT"};
    return names[std::clamp(t, 0, 7)];
  }

  void prepare(double sr, int /*maxBlock*/) {
    sr_ = sr;
    // Type presets: room size, ring time, tone, early energy and how much the
    // tail breathes. These are what actually differentiate HALL from CHAPEL.
    static const RevTypePreset presets[8] = {
        {1.00f, 1.00f, 0.42f, 0.30f, 22.0f, 3.0f}, // Hall
        {1.30f, 1.45f, 0.36f, 0.26f, 32.0f, 3.6f}, // Concert hall
        {0.55f, 0.62f, 0.26f, 0.44f, 8.0f, 1.6f},  // Plate
        {0.62f, 0.70f, 0.22f, 0.46f, 18.0f, 1.8f}, // Vocal plate
        {0.34f, 0.34f, 0.50f, 0.62f, 5.0f, 1.2f},  // Room
        {0.78f, 0.85f, 0.40f, 0.40f, 20.0f, 2.4f}, // Chapel
        {1.55f, 1.75f, 0.28f, 0.22f, 40.0f, 4.2f}, // Cathedral
        {1.90f, 2.60f, 0.22f, 0.16f, 55.0f, 5.0f}, // Ambient
    };
    presets_ = presets;

    const double maxLen = 0.30 * sr_; // room for size * type scale
    for (int i = 0; i < kCombs; ++i) {
      comb_[i].buf.assign((size_t)maxLen, 0.0f);
      comb_[i].pos = 0;
      comb_[i].lp = comb_[i].hp = 0.0f;
    }
    for (int i = 0; i < kDiff; ++i) {
      diff_[i].buf.assign((size_t)(0.03 * sr_), 0.0f);
      diff_[i].pos = 0;
    }
    for (int i = 0; i < 2; ++i) {
      outDiffL_[i].buf.assign((size_t)(0.012 * sr_), 0.0f);
      outDiffL_[i].pos = 0;
      outDiffR_[i].buf.assign((size_t)(0.012 * sr_), 0.0f);
      outDiffR_[i].pos = 0;
    }
    pre_.assign((size_t)(0.21 * sr_), 0.0f);
    prePos_ = 0;
    early_.assign((size_t)(0.12 * sr_), 0.0f);
    earlyPos_ = 0;

    for (auto& s : sm_) s.prepare(sr, 60.0f);
    wetSm_.prepare(sr, 40.0f);
    modPhase_ = 0.0f;
    modPhase2_ = 0.0f;
    dcX_ = dcYL_ = dcX2_ = dcYR_ = 0.0f;
    lastType_ = -1;
    initialized_ = false;
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
    if (!initialized_) {
      initialized_ = true;
      for (auto& s : sm_) s.jumpTo(s.target());
      wetSm_.jumpTo(wetSm_.target());
      lastType_ = type_;
      update(1); // seat the delay lengths before the first sample runs
    }
  }

  // Called once per block with that block's length, so every control is
  // smoothed in real time (one sample step per frame) instead of per block.
  void update(int n) {
    const RevTypePreset& pr = presets_[std::clamp(type_, 0, 7)];
    if (type_ != lastType_) {
      lastType_ = type_;
      // A new type brings its own pre-delay starting point.
      sm_[0].setTarget(pr.predelayMs);
    }
    predelayMs_ = sm_[0].nextBlock(n);
    decay_ = sm_[1].nextBlock(n);
    size_ = sm_[2].nextBlock(n);
    damp_ = sm_[3].nextBlock(n);
    diffuse_ = sm_[4].nextBlock(n);
    earlyAmt_ = sm_[5].nextBlock(n);
    hf_ = sm_[6].nextBlock(n);
    lf_ = sm_[7].nextBlock(n);

    static const float baseMs[kCombs] = {29.7f, 37.1f, 41.1f, 43.7f,
                                         47.9f, 53.3f, 59.9f, 67.3f};
    const float scale = (0.45f + size_ * 0.85f) * pr.sizeScale;
    const float rt60 = std::max(0.08f, decay_ * pr.decayScale);
    // HF DAMP darkens the feedback loop; the type adds its own colour.
    const float dark = std::clamp(hf_ * 0.75f + damp_ * 0.22f + pr.damp * 0.24f, 0.0f, 1.0f);
    for (int i = 0; i < kCombs; ++i) {
      const float len = std::clamp(baseMs[i] * scale * 0.001f * (float)sr_, 16.0f,
                                   (float)comb_[i].buf.size() - 8.0f);
      comb_[i].len = len;
      comb_[i].fb = std::min(0.9995f, std::pow(10.0f, -3.0f * (len / (float)sr_) / rt60));
      // HF DAMP is a low-pass inside the loop. LF DAMP only touches the band
      // below ~150 Hz: subtracting a unity low-pass across the whole signal
      // took the vocal's own body out of every pass, which is what made the
      // tail die a few hundred ms after the note.
      // lpA is a one-pole coefficient: SMALLER means more smoothing, so the
      // damping control has to lower it. It used to raise it, which made HF
      // DAMP at 100% the brightest setting.
      comb_[i].lpA = 0.88f - dark * 0.74f;
      comb_[i].lfA = 0.002f + lf_ * 0.020f;
      comb_[i].lfAmt = lf_ * 0.85f;
    }
    static const float diffMs[kDiff] = {4.77f, 6.31f, 8.53f, 11.37f};
    for (int i = 0; i < kDiff; ++i) {
      diff_[i].len = std::clamp(diffMs[i] * (0.75f + size_ * 0.60f) * 0.001f * (float)sr_,
                                4.0f, (float)diff_[i].buf.size() - 4.0f);
      diff_[i].g = std::clamp(diffuse_, 0.0f, 1.0f) * 0.72f;
    }
    for (int i = 0; i < 2; ++i) {
      outDiffL_[i].len = std::clamp((0.0019f + 0.0011f * (float)i) * (float)sr_, 4.0f,
                                    (float)outDiffL_[i].buf.size() - 4.0f);
      outDiffL_[i].g = 0.6f;
      outDiffR_[i].len = std::clamp((0.0021f + 0.0013f * (float)i) * (float)sr_, 4.0f,
                                    (float)outDiffR_[i].buf.size() - 4.0f);
      outDiffR_[i].g = 0.6f;
    }
    // Early reflections move with the room size and the type.
    static const float erMs[6] = {7.3f, 11.9f, 17.3f, 23.1f, 31.7f, 43.1f};
    const float erScale = (0.60f + size_ * 0.70f) * pr.sizeScale;
    for (int i = 0; i < 6; ++i) {
      erLen_[i] = std::clamp(erMs[i] * erScale * 0.001f * (float)sr_, 2.0f,
                             (float)early_.size() - 4.0f);
    }
    modDepth_ = pr.modDepth * (0.55f + size_ * 0.75f);
    earlyGain_ = pr.early;
  }

  // Fractional read from a delay line, so the modulation actually glides
  // instead of dropping its integer part and staying static.
  static float readLine(const std::vector<float>& buf, int pos, float delay) {
    const int sz = (int)buf.size();
    float rp = (float)pos - delay;
    while (rp < 0.0f) rp += (float)sz;
    int i0 = (int)rp;
    if (i0 >= sz) i0 = sz - 1;
    const float frac = rp - (float)i0;
    const int i1 = (i0 + 1) % sz;
    return buf[(size_t)i0] * (1.0f - frac) + buf[(size_t)i1] * frac;
  }

  // Returns wet L/R for one block: pre-delay, early reflections, diffusion,
  // modulated damped combs, then per-channel output diffusion.
  void process(const float* in, float* outL, float* outR, int n) {
    if (!enabled_) {
      std::memset(outL, 0, sizeof(float) * n);
      std::memset(outR, 0, sizeof(float) * n);
      return;
    }
    update(n);

    // WET is a return level: 100% is an obviously wet but still musical tail.
    const float wet = wetSm_.nextBlock(n) * 2.0f;
    const int preSize = (int)pre_.size();
    const int preSamples = std::clamp((int)(predelayMs_ * 0.001f * (float)sr_), 0, preSize - 2);
    const float erAmt = std::clamp(earlyAmt_ * (0.55f + earlyGain_), 0.0f, 1.4f);
    const float modInc = 2.0f * kPi * 0.55f / (float)sr_;
    const float modInc2 = 2.0f * kPi * 0.83f / (float)sr_;
    static const float erSign[6] = {0.62f, -0.52f, 0.44f, -0.36f, 0.30f, -0.24f};
    static const float combPan[kCombs] = {-1.0f, 1.0f,  -0.55f, 0.55f,
                                          -0.20f, 0.20f, -0.85f, 0.85f};
    const bool diffOn = diffuse_ > 0.001f;

    for (int i = 0; i < n; ++i) {
      // Pre-delay line.
      const int rp = (prePos_ - preSamples + (int)pre_.size()) % (int)pre_.size();
      float x = pre_[rp];
      pre_[prePos_] = in[i];
      prePos_ = (prePos_ + 1) % preSize;

      // Early reflections: sparse, sign-alternating taps that follow the room
      // size. EARLY blends these in against the diffuse tail.
      const int es = (int)early_.size();
      float er = 0.0f;
      for (int k = 0; k < 6; ++k) {
        int r = earlyPos_ - (int)erLen_[k];
        if (r < 0) r += es;
        er += early_[r] * erSign[k];
      }
      early_[earlyPos_] = x;
      earlyPos_ = (earlyPos_ + 1) % es;
      er = std::clamp(er, -1.2f, 1.2f);

      // Input diffusion.
      float d = x;
      if (diffOn) {
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

      // Modulated, damped parallel combs. Two slow LFOs spread across the
      // bank give the tail movement without a separate chorus.
      modPhase_ += modInc;
      if (modPhase_ > 6.2831853f) modPhase_ -= 6.2831853f;
      modPhase2_ += modInc2;
      if (modPhase2_ > 6.2831853f) modPhase2_ -= 6.2831853f;
      const float m1 = std::sin(modPhase_);
      const float m2 = std::sin(modPhase2_);

      float accL = 0.0f, accR = 0.0f;
      for (int c = 0; c < kCombs; ++c) {
        auto& cb = comb_[c];
        const int sz = (int)cb.buf.size();
        const float lfo = (c & 1) ? m2 : m1;
        const float delay = cb.len + modDepth_ * lfo * (0.70f + 0.15f * (float)(c % 4));
        const float y = readLine(cb.buf, cb.pos, delay);

        // Damping inside the feedback loop: HF via a low-pass, LF via a
        // high-pass, both driven by the HF/LF DAMP controls.
        cb.lp += cb.lpA * (y - cb.lp);
        cb.hp += cb.lfA * (cb.lp - cb.hp);
        const float filtered = cb.lp - cb.hp * cb.lfAmt;

        cb.buf[cb.pos] = d + filtered * cb.fb;
        cb.pos = (cb.pos + 1) % sz;

        accL += filtered * (1.0f - 0.5f * combPan[c]);
        accR += filtered * (1.0f + 0.5f * combPan[c]);
      }
      accL *= 0.125f;
      accR *= 0.125f;

      // Output diffusion, one pair per channel: sharing one pair collapsed
      // the stereo image into mono.
      for (int k = 0; k < 2; ++k) {
        auto& a = outDiffL_[k];
        const int sz = (int)a.buf.size();
        int r = a.pos - (int)a.len;
        if (r < 0) r += sz;
        const float bo = a.buf[r];
        const float y = bo - a.g * accL;
        a.buf[a.pos] = accL + a.g * y;
        a.pos = (a.pos + 1) % sz;
        accL = y;
      }
      for (int k = 0; k < 2; ++k) {
        auto& a = outDiffR_[k];
        const int sz = (int)a.buf.size();
        int r = a.pos - (int)a.len;
        if (r < 0) r += sz;
        const float bo = a.buf[r];
        const float y = bo - a.g * accR;
        a.buf[a.pos] = accR + a.g * y;
        a.pos = (a.pos + 1) % sz;
        accR = y;
      }

      // DC blocker: keeps a long decay from walking away at the bottom end.
      const float wetL = accL + er * erAmt;
      const float wetR = accR + er * erAmt * 0.9f;
      const float hpL = wetL - dcX_ + 0.9995f * dcYL_;
      dcX_ = wetL;
      dcYL_ = hpL;
      const float hpR = wetR - dcX2_ + 0.9995f * dcYR_;
      dcX2_ = wetR;
      dcYR_ = hpR;

      outL[i] = hpL * wet;
      outR[i] = hpR * wet;
    }
  }

  void setEarlyMix(float v) { sm_[5].setTarget(v); }
  void setEarlyMixImmediate(float v) { sm_[5].jumpTo(v); }

  void reset() {
    for (auto& c : comb_) {
      std::fill(c.buf.begin(), c.buf.end(), 0.0f);
      c.lp = c.hp = 0.0f;
      c.pos = 0;
    }
    for (auto& a : diff_)
      std::fill(a.buf.begin(), a.buf.end(), 0.0f);
    for (auto& a : outDiffL_)
      std::fill(a.buf.begin(), a.buf.end(), 0.0f);
    for (auto& a : outDiffR_)
      std::fill(a.buf.begin(), a.buf.end(), 0.0f);
    std::fill(pre_.begin(), pre_.end(), 0.0f);
    std::fill(early_.begin(), early_.end(), 0.0f);
    prePos_ = earlyPos_ = 0;
    modPhase_ = modPhase2_ = 0.0f;
    dcX_ = dcYL_ = dcX2_ = dcYR_ = 0.0f;
  }

  int type() const { return type_; }

private:
  struct Comb {
    std::vector<float> buf;
    int pos = 0;
    float len = 100, fb = 0.8f, lpA = 0.35f, lfA = 0.02f, lfAmt = 0.0f;
    float lp = 0, hp = 0;
  };
  struct Allpass {
    std::vector<float> buf;
    int pos = 0;
    float len = 100, g = 0.6f;
  };

  double sr_ = 48000.0;
  Comb comb_[kCombs];
  Allpass diff_[kDiff], outDiffL_[2], outDiffR_[2];
  std::vector<float> pre_, early_;
  int prePos_ = 0, earlyPos_ = 0;
  Smoother sm_[8];
  const RevTypePreset* presets_ = nullptr;
  int type_ = 2, lastType_ = -1;
  bool enabled_ = true;
  bool initialized_ = false;
  Smoother wetSm_;
  float earlyGain_ = 0.35f;
  float predelayMs_ = 25.0f, decay_ = 2.4f, size_ = 0.6f, damp_ = 0.4f, diffuse_ = 0.72f;
  float earlyAmt_ = 0.35f, hf_ = 0.45f, lf_ = 0.30f;
  float modDepth_ = 3.0f, modPhase_ = 0.0f, modPhase2_ = 0.0f;
  float erLen_[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
  float dcX_ = 0, dcYL_ = 0, dcX2_ = 0, dcYR_ = 0;
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
    const float lg = sm_[0].nextBlock(n), mg = sm_[1].nextBlock(n), hg = sm_[2].nextBlock(n);
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

// ---------------------------------------------------------------------------
// Karaoke vocal cut.
//
// Almost every published mix puts the lead vocal in the phantom centre, so the
// centre component of the stereo pair carries it. The filter keeps the centre
// band where a voice lives (165 Hz - 7 kHz), then subtracts that band from both
// channels: the vocal cancels, while bass and cymbals - which are not equally
// present in L and R - survive and keep the track sounding like music instead
// of a hollow side-only signal.
// ---------------------------------------------------------------------------
class VocalCut {
public:
  static constexpr int kFft = 1024;
  static constexpr int kHop = 256;

  // The processor delays the song by one frame; the recording alignment is
  // advanced by the same amount so the voice still lands with the beat.
  int latencyFrames() const { return kFft; }

  void prepare(double sr) {
    sr_ = sr > 0.0 ? sr : 48000.0;
    for (int i = 0; i < kFft; ++i) {
      win_[i] = 0.5f - 0.5f * std::cos(2.0f * kPi * (float)i / (float)(kFft - 1));
      cosTab_[i] = std::cos(2.0f * kPi * (float)i / (float)kFft);
    }
    reset();
  }

  void setOn(bool on, float strength) {
    on_ = on;
    strength_ = std::clamp(strength, 0.0f, 1.0f);
  }

  void process(float* l, float* r, int n) {
    const bool active = on_ && strength_ > 0.001f;
    if (!active) {
      if (wasOn_) {
        reset();
        wasOn_ = false;
      }
      return;
    }
    wasOn_ = true;
    for (int i = 0; i < n; ++i) {
      inL_[w_] = l[i];
      inR_[w_] = r[i];
      l[i] = qL_[qRead_];
      r[i] = qR_[qRead_];
      qL_[qRead_] = 0.0f;
      qR_[qRead_] = 0.0f;
      w_ = (w_ + 1) & (kFft - 1);
      qRead_ = (qRead_ + 1) & (2 * kFft - 1);
      if (++sinceHop_ >= kHop) {
        sinceHop_ = 0;
        frame();
      }
    }
  }

  void reset() {
    for (int i = 0; i < kFft; ++i) {
      inL_[i] = inR_[i] = 0.0f;
    }
    for (int i = 0; i < 2 * kFft; ++i) {
      qL_[i] = qR_[i] = 0.0f;
    }
    w_ = 0;
    qRead_ = 0;
    qWrite_ = kFft;
    sinceHop_ = 0;
  }

private:
  void fft(float* re, float* im, bool inverse = false) {
    for (int i = 1, j = 0; i < kFft; ++i) {
      int bit = kFft >> 1;
      for (; j & bit; bit >>= 1) j ^= bit;
      j ^= bit;
      if (i < j) {
        std::swap(re[i], re[j]);
        std::swap(im[i], im[j]);
      }
    }
    for (int len = 2; len <= kFft; len <<= 1) {
      const int step = kFft / len;
      const int half = len >> 1;
      for (int base = 0; base < kFft; base += len) {
        for (int k = 0; k < half; ++k) {
          const int tw = (k * step) & (kFft - 1);
          const float wr = cosTab_[tw];
          const float sinv = -cosTab_[(tw + kFft / 4) & (kFft - 1)];
          const float wi = inverse ? -sinv : sinv;
          const int a = base + k;
          const int b = a + half;
          const float tr = wr * re[b] - wi * im[b];
          const float ti = wr * im[b] + wi * re[b];
          re[b] = re[a] - tr;
          im[b] = im[a] - ti;
          re[a] += tr;
          im[a] += ti;
        }
      }
    }
    if (inverse) {
      const float s = 1.0f / (float)kFft;
      for (int i = 0; i < kFft; ++i) {
        re[i] *= s;
        im[i] *= s;
      }
    }
  }

  void frame() {
    for (int i = 0; i < kFft; ++i) {
      const int idx = (w_ + i) & (kFft - 1);
      reL_[i] = inL_[idx] * win_[i];
      imL_[i] = 0.0f;
      reR_[i] = inR_[idx] * win_[i];
      imR_[i] = 0.0f;
    }
    fft(reL_, imL_);
    fft(reR_, imR_);
    for (int k = 0; k <= kFft / 2; ++k) {
      const float lr = reL_[k], li = imL_[k];
      const float rr = reR_[k], ri = imR_[k];
      const float mr = 0.5f * (lr + rr), mi = 0.5f * (li + ri);
      const float sr = 0.5f * (lr - rr), si = 0.5f * (li - ri);
      const float mm = std::sqrt(mr * mr + mi * mi);
      const float ss = std::sqrt(sr * sr + si * si);
      // 1.0 when the bin sits dead centre, 0.5 when the two channels are
      // unrelated. Only the centre-dominant part of the mix is removed, which
      // is what keeps the drums, bass and reverb tails alive.
      const float c = mm / (mm + ss + 1e-9f);
      // Only the vocal band is touched: a kick and a bass line are centred too,
      // so without this the low end would vanish along with the voice. Cymbals
      // and air stay as well, since they are centred far more often than they
      // are vocal.
      // At full strength the centre goes whatever it holds (the classic karaoke
      // subtraction); the further the slider comes down, the more it only takes
      // what is clearly centre-dominant, so backing off is a real option.
      const float coherence = std::clamp(2.0f * c - 1.0f, 0.0f, 1.0f);
      const float blend = coherence * (1.0f - strength_) + strength_;
      const float weight = blend * strength_ * bandGain(k);
      const float cr = mr * weight, ci = mi * weight;
      const float ylr = lr - cr, yli = li - ci;
      const float yrr = rr - cr, yri = ri - ci;
      if (k == 0 || k == kFft / 2) {
        reL_[k] = ylr;
        imL_[k] = 0.0f;
        reR_[k] = yrr;
        imR_[k] = 0.0f;
      } else {
        reL_[k] = ylr;
        imL_[k] = yli;
        reR_[k] = yrr;
        imR_[k] = yri;
        const int m = kFft - k;
        reL_[m] = ylr;
        imL_[m] = -yli;
        reR_[m] = yrr;
        imR_[m] = -yri;
      }
    }
    fft(reL_, imL_, true);
    fft(reR_, imR_, true);
    // Hann analysis + Hann synthesis at 75% overlap sums to 1.5.
    const float norm = 1.0f / 1.5f;
    for (int i = 0; i < kFft; ++i) {
      const int idx = (qWrite_ + i) & (2 * kFft - 1);
      qL_[idx] += reL_[i] * win_[i] * norm;
      qR_[idx] += reR_[i] * win_[i] * norm;
    }
    qWrite_ = (qWrite_ + kHop) & (2 * kFft - 1);
  }

  // 0 below 180 Hz, full from 320 Hz, fading out above 7 kHz.
  float bandGain(int k) const {
    const float f = (float)k * (float)sr_ / (float)kFft;
    if (f < 180.0f || f > 9500.0f) return 0.0f;
    if (f < 320.0f) return (f - 180.0f) / 140.0f;
    if (f > 7000.0f) return (9500.0f - f) / 2500.0f;
    return 1.0f;
  }

  double sr_ = 48000.0;
  float win_[kFft] = {};
  float cosTab_[kFft] = {};
  float inL_[kFft] = {};
  float inR_[kFft] = {};
  float qL_[2 * kFft] = {};
  float qR_[2 * kFft] = {};
  float reL_[kFft] = {}, imL_[kFft] = {};
  float reR_[kFft] = {}, imR_[kFft] = {};
  int w_ = 0, qRead_ = 0, qWrite_ = kFft, sinceHop_ = 0;
  float strength_ = 0.85f;
  bool on_ = false;
  bool wasOn_ = false;
};

// ---------------------------------------------------------------------------
// Vocal echo / delay: fractional tap, feedback with a tone control in the
// loop, and a cross-feed that turns plain repeats into a ping-pong.
// ---------------------------------------------------------------------------
class Echo {
public:
  void prepare(double sr) {
    sr_ = sr;
    size_ = std::max(1, (int)(1.35 * sr)); // 1.35 s maximum delay
    bufL_.assign((size_t)size_, 0.0f);
    bufR_.assign((size_t)size_, 0.0f);
    pos_ = 0;
    lpL_ = lpR_ = 0.0f;
    for (auto& s : sm_) s.prepare(sr, 60.0f);
    timeMs_ = 320.0f;
    fb_ = 0.32f;
    tone_ = 0.45f;
    spread_ = 0.6f;
    on_ = false;
    dirty_ = false;
    sm_[0].jumpTo(timeMs_);
    sm_[1].jumpTo(fb_);
    sm_[2].jumpTo(0.0f);
  }

  void setParams(bool on, float timeMs, float fb, float mix, float tone, float spread) {
    on_ = on;
    sm_[0].setTarget(std::clamp(timeMs, 40.0f, 1300.0f));
    sm_[1].setTarget(std::clamp(fb, 0.0f, 0.92f));
    sm_[2].setTarget(std::clamp(mix, 0.0f, 1.0f));
    tone_ = std::clamp(tone, 0.0f, 1.0f);
    spread_ = std::clamp(spread, 0.0f, 1.0f);
  }

  // Send-style: writes the wet repeats only, so the caller keeps the dry vocal.
  void process(const float* in, float* outL, float* outR, int n) {
    const float mix = sm_[2].nextBlock(n);
    if (!on_ || mix <= 0.0005f) {
      if (dirty_) {
        reset();
        dirty_ = false;
      }
      std::memset(outL, 0, sizeof(float) * (size_t)n);
      std::memset(outR, 0, sizeof(float) * (size_t)n);
      return;
    }
    dirty_ = true;
    const float timeMs = sm_[0].nextBlock(n);
    const float fb = sm_[1].nextBlock(n);

    const float dl = std::clamp(timeMs * 0.001f * (float)sr_, 8.0f, (float)size_ - 8.0f);
    const float dr = std::clamp(dl * (1.0f + 0.18f * spread_), 8.0f, (float)size_ - 8.0f);
    const float cross = spread_ * 0.30f;
    // Tone in the feedback path: 0 = dark repeats, 1 = bright.
    const float toneCoef = 0.03f + tone_ * 0.85f;

    for (int i = 0; i < n; ++i) {
      float rl = (float)pos_ - dl;
      while (rl < 0.0f) rl += (float)size_;
      int i0 = (int)rl;
      if (i0 >= size_) i0 = size_ - 1;
      const float frac = rl - (float)i0;
      const int i1 = (i0 + 1) % size_;
      const float tapL = bufL_[(size_t)i0] * (1.0f - frac) + bufL_[(size_t)i1] * frac;

      float rr = (float)pos_ - dr;
      while (rr < 0.0f) rr += (float)size_;
      int j0 = (int)rr;
      if (j0 >= size_) j0 = size_ - 1;
      const float frac2 = rr - (float)j0;
      const int j1 = (j0 + 1) % size_;
      const float tapR = bufR_[(size_t)j0] * (1.0f - frac2) + bufR_[(size_t)j1] * frac2;

      lpL_ += toneCoef * (tapL - lpL_);
      lpR_ += toneCoef * (tapR - lpR_);

      const float x = in[i];
      bufL_[(size_t)pos_] = x + (lpL_ * (1.0f - cross) + lpR_ * cross) * fb;
      bufR_[(size_t)pos_] = x + (lpR_ * (1.0f - cross) + lpL_ * cross) * fb;
      pos_ = (pos_ + 1) % size_;

      outL[i] = tapL * mix * 1.4f;
      outR[i] = tapR * mix * 1.4f;
    }
  }

  void reset() {
    if (!bufL_.empty()) std::fill(bufL_.begin(), bufL_.end(), 0.0f);
    if (!bufR_.empty()) std::fill(bufR_.begin(), bufR_.end(), 0.0f);
    pos_ = 0;
    lpL_ = lpR_ = 0.0f;
  }

private:
  std::vector<float> bufL_, bufR_;
  int size_ = 1, pos_ = 0;
  double sr_ = 48000.0;
  Smoother sm_[3];
  float timeMs_ = 320.0f, fb_ = 0.32f, tone_ = 0.45f, spread_ = 0.6f;
  float lpL_ = 0, lpR_ = 0;
  bool on_ = false, dirty_ = false;
};

} // namespace dsp
} // namespace lvs
