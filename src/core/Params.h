#pragma once
// ---------------------------------------------------------------------------
// Central parameter table. Every control the user can touch maps to one entry
// here, so presets, undo, A/B and automation are all generic.
// ---------------------------------------------------------------------------
#include <atomic>
#include <cmath>
#include <string>

#define LVS_PARAM_LIST(X)                                                                        \
  /* --- Preamp ------------------------------------------------------------ */                     \
  X(Preamp, PreGain,     "pre.gain",   "GAIN",      -12.0f,   36.0f,    0.0f,  "dB")              \
  X(Preamp, PreDrive,    "pre.drive",  "DRIVE",       0.0f,    1.0f,    0.18f, "")                \
  X(Preamp, PreWarmth,   "pre.warmth", "WARMTH",      0.0f,    1.0f,    0.35f, "")                \
  X(Preamp, PreSat,      "pre.sat",    "SAT",         0.0f,    1.0f,    0.22f, "")                \
  X(Preamp, PreOut,      "pre.out",    "OUT",       -12.0f,   12.0f,    0.0f,  "dB")              \
  X(Preamp, PrePhase,    "pre.phase",  "PHASE",       0.0f,    1.0f,    0.0f,  "")                \
  X(Preamp, PreMode,     "pre.mode",   "MODE",        0.0f,    2.0f,    1.0f,  "")                \
  /* --- High pass --------------------------------------------------------- */                     \
  X(Hpf,    HpfFreq,     "hpf.freq",   "HPF",        20.0f,  400.0f,   90.0f,  "Hz")              \
  X(Hpf,    HpfOn,       "hpf.on",     "IN",          0.0f,    1.0f,    1.0f,  "")                \
  /* --- Four band British console EQ -------------------------------------- */                     \
  X(Eq,     EqOn,        "eq.on",      "EQ IN",       0.0f,    1.0f,    1.0f,  "")                \
  X(Eq,     EqLowGain,   "eq.low.g",   "LOW",       -18.0f,   18.0f,    0.0f,  "dB")              \
  X(Eq,     EqLowFreq,   "eq.low.f",   "LOW F",      30.0f,  300.0f,  110.0f,  "Hz")              \
  X(Eq,     EqLowQ,      "eq.low.q",   "LOW Q",       0.3f,    3.0f,    0.75f, "")                \
  X(Eq,     EqLowOn,     "eq.low.on",  "LOW IN",      0.0f,    1.0f,    1.0f,  "")                \
  X(Eq,     EqLoMidGain, "eq.lomid.g", "LO-MID",    -18.0f,   18.0f,    0.0f,  "dB")              \
  X(Eq,     EqLoMidFreq, "eq.lomid.f", "LO-MID F",  120.0f, 3000.0f,  420.0f,  "Hz")              \
  X(Eq,     EqLoMidQ,    "eq.lomid.q", "LO-MID Q",    0.3f,    8.0f,    1.10f, "")                \
  X(Eq,     EqLoMidOn,   "eq.lomid.on","LO-MID IN",   0.0f,    1.0f,    1.0f,  "")                \
  X(Eq,     EqHiMidGain, "eq.himid.g", "HI-MID",    -18.0f,   18.0f,    0.0f,  "dB")              \
  X(Eq,     EqHiMidFreq, "eq.himid.f", "HI-MID F",  700.0f, 9000.0f, 2800.0f,  "Hz")              \
  X(Eq,     EqHiMidQ,    "eq.himid.q", "HI-MID Q",    0.3f,    8.0f,    1.20f, "")                \
  X(Eq,     EqHiMidOn,   "eq.himid.on","HI-MID IN",   0.0f,    1.0f,    1.0f,  "")                \
  X(Eq,     EqHighGain,  "eq.high.g",  "HIGH",      -18.0f,   18.0f,    0.0f,  "dB")              \
  X(Eq,     EqHighFreq,  "eq.high.f",  "HIGH F",   2500.0f,16000.0f, 8000.0f,  "Hz")              \
  X(Eq,     EqHighQ,     "eq.high.q",  "HIGH Q",      0.3f,    3.0f,    0.75f, "")                \
  X(Eq,     EqHighOn,    "eq.high.on", "HIGH IN",     0.0f,    1.0f,    1.0f,  "")                \
  /* --- Vocal compressor -------------------------------------------------- */                     \
  X(Comp,   CompOn,      "comp.on",    "COMP IN",     0.0f,    1.0f,    1.0f,  "")                \
  X(Comp,   CompThresh,  "comp.thr",   "THRESH",    -60.0f,    0.0f,  -22.0f,  "dB")              \
  X(Comp,   CompRatio,   "comp.ratio", "RATIO",       1.0f,   20.0f,    3.5f,  ":1")              \
  X(Comp,   CompAttack,  "comp.att",   "ATTACK",      0.2f,  120.0f,   12.0f,  "ms")              \
  X(Comp,   CompRelease, "comp.rel",   "RELEASE",    10.0f, 2000.0f,  180.0f,  "ms")              \
  X(Comp,   CompKnee,    "comp.knee",  "KNEE",        0.0f,   24.0f,    9.0f,  "dB")              \
  X(Comp,   CompMakeup,  "comp.makeup","MAKEUP",    -12.0f,   24.0f,    4.0f,  "dB")              \
  X(Comp,   CompMix,     "comp.mix",   "MIX",         0.0f,    1.0f,    1.0f,  "")                \
  /* --- Reverb ------------------------------------------------------------ */                     \
  X(Reverb, RevOn,       "rev.on",     "REV IN",      0.0f,    1.0f,    1.0f,  "")                \
  X(Reverb, RevType,     "rev.type",   "TYPE",        0.0f,    7.0f,    2.0f,  "")                \
  X(Reverb, RevSend,     "rev.send",   "SEND",        0.0f,    1.0f,    0.50f, "")                \
  X(Reverb, RevWet,      "rev.wet",    "WET",         0.0f,    1.0f,    0.32f, "")                \
  X(Reverb, RevPredelay, "rev.predly", "PRE-DLY",     0.0f,  200.0f,   25.0f,  "ms")              \
  X(Reverb, RevDecay,    "rev.decay",  "DECAY",       0.3f,   12.0f,    2.40f, "s")               \
  X(Reverb, RevSize,     "rev.size",   "SIZE",        0.0f,    1.0f,    0.60f, "")                \
  X(Reverb, RevDamp,     "rev.damp",   "DAMP",        0.0f,    1.0f,    0.40f, "")                \
  X(Reverb, RevDiffusion,"rev.diff",   "DIFFUSE",     0.0f,    1.0f,    0.72f, "")                \
  X(Reverb, RevEarly,    "rev.early",  "EARLY",       0.0f,    1.0f,    0.35f, "")                \
  X(Reverb, RevHf,       "rev.hfdamp", "HF DAMP",     0.0f,    1.0f,    0.45f, "")                \
  X(Reverb, RevLf,       "rev.lfdamp", "LF DAMP",     0.0f,    1.0f,    0.30f, "")                \
  /* --- Mixer ------------------------------------------------------------- */                     \
  X(Mixer,  MicFader,    "mix.mic",    "MIC",         0.0f,    1.5f,    0.78f, "")                \
  X(Mixer,  MicMute,     "mix.mic.mute","MUTE",       0.0f,    1.0f,    0.0f,  "")                \
  X(Mixer,  MicSolo,     "mix.mic.solo","SOLO",       0.0f,    1.0f,    0.0f,  "")                \
  X(Mixer,  MicPan,      "mix.mic.pan", "PAN MIC",   -1.0f,    1.0f,    0.0f,  "")                \
  X(Mixer,  MusicFader,  "mix.music",  "MUSIC",       0.0f,    1.0f,    0.62f, "")                \
  X(Mixer,  MusicMute,   "mix.music.mute","MUTE",     0.0f,    1.0f,    0.0f,  "")                \
  X(Mixer,  MusicSolo,   "mix.music.solo","SOLO",     0.0f,    1.0f,    0.0f,  "")                \
  X(Mixer,  MusicPan,    "mix.music.pan","PAN MUS",  -1.0f,    1.0f,    0.0f,  "")                \
  X(Mixer,  MasterFader, "mix.master", "MASTER",      0.0f,    1.5f,    0.80f, "")                \
  X(Mixer,  MasterMute,  "mix.master.mute","MUTE",    0.0f,    1.0f,    0.0f,  "")                \
  X(Mixer,  MasterBalance,"mix.balance","BALANCE",   -1.0f,    1.0f,    0.0f,  "")                \
  X(Mixer,  LimiterOn,   "mix.limiter","LIMITER",     0.0f,    1.0f,    1.0f,  "")                \
  X(Mixer,  MasterCompOn,"mix.mcomp",  "BUS COMP",    0.0f,    1.0f,    0.0f,  "")                \
  X(Mixer,  MasterCompThr,"mix.mcomp.thr","THRESH", -40.0f,    0.0f,  -12.0f,  "dB")              \
  X(Mixer,  MasterCompRatio,"mix.mcomp.ratio","RATIO",1.0f,  10.0f,    2.5f,  ":1")               \
  /* --- Music EQ ---------------------------------------------------------- */                     \
  X(MusEq,  MusEqOn,     "museq.on",   "EQ IN",       0.0f,    1.0f,    1.0f,  "")                \
  X(MusEq,  MusEqLow,    "museq.low",  "LOW",       -12.0f,   12.0f,    0.0f,  "dB")              \
  X(MusEq,  MusEqMid,    "museq.mid",  "MID",       -12.0f,   12.0f,    0.0f,  "dB")              \
  X(MusEq,  MusEqHigh,   "museq.high", "HIGH",      -12.0f,   12.0f,    0.0f,  "dB")

namespace lvs {

enum class P : int {
#define X(group, id, name, label, lo, hi, def, unit) id,
  LVS_PARAM_LIST(X)
#undef X
      Count
};

struct ParamInfo {
  const char* group;
  const char* id;
  const char* label;
  float min, max, def;
  const char* unit;
};

extern const ParamInfo kParams[(int)P::Count];
extern const int kParamCount;

inline int pi(P p) { return (int)p; }
const char* ParamName(P p);

// Lock-free parameter store. UI threads write, the audio thread reads.
class ParamStore {
public:
  ParamStore() {
    for (int i = 0; i < kParamCount; ++i) v_[i].store(kParams[i].def, std::memory_order_relaxed);
  }
  float get(P p) const { return v_[(int)p].load(std::memory_order_relaxed); }
  bool getBool(P p) const { return get(p) > 0.5f; }
  void set(P p, float value) {
    const ParamInfo& info = kParams[(int)p];
    if (!(value == value)) return; // NaN guard
    if (value < info.min) value = info.min;
    if (value > info.max) value = info.max;
    v_[(int)p].store(value, std::memory_order_relaxed);
  }
  void setNorm(P p, float n) {
    const ParamInfo& info = kParams[(int)p];
    set(p, info.min + (info.max - info.min) * n);
  }
  float norm(P p) const {
    const ParamInfo& info = kParams[(int)p];
    return (get(p) - info.min) / (info.max - info.min);
  }
  void resetDefaults() {
    for (int i = 0; i < kParamCount; ++i) v_[i].store(kParams[i].def, std::memory_order_relaxed);
  }
  // Bulk snapshot (used by presets and A/B).
  void capture(float* out) const {
    for (int i = 0; i < kParamCount; ++i) out[i] = v_[i].load(std::memory_order_relaxed);
  }
  void apply(const float* in) {
    for (int i = 0; i < kParamCount; ++i) {
      if (in[i] == in[i]) v_[i].store(in[i], std::memory_order_relaxed);
    }
  }

private:
  std::atomic<float> v_[(int)P::Count];
};

// dB helpers
inline float linToDb(float x) { return 20.0f * std::log10(std::max(x, 1e-9f)); }
inline float dbToLin(float db) { return std::pow(10.0f, db / 20.0f); }

} // namespace lvs
