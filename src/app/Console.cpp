// ---------------------------------------------------------------------------
// The mixing console interface: layout, drawing and hit regions.
// ---------------------------------------------------------------------------
#include "app/App.h"

#include "core/Json.h"
#include "dsp/Effects.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

using namespace lvs;
using namespace lvs::ui;

namespace lvs {

namespace {

D2D1_RECT_F R(float l, float t, float r, float b) { return D2D1::RectF(l, t, r, b); }
D2D1_RECT_F inset(D2D1_RECT_F r, float d) {
  return D2D1::RectF(r.left + d, r.top + d, r.right - d, r.bottom - d);
}
float width(D2D1_RECT_F r) { return r.right - r.left; }
float height(D2D1_RECT_F r) { return r.bottom - r.top; }

std::wstring W(const std::string& s) {
  if (s.empty()) return L"";
  int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring out((size_t)len, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), len);
  return out;
}

std::string timecode(double seconds) {
  if (!(seconds > 0.0)) seconds = 0.0;
  const int total = (int)seconds;
  char buf[32];
  snprintf(buf, sizeof(buf), "%d:%02d", total / 60, total % 60);
  return buf;
}

const P kEqBands[4][3] = {
    {P::EqLowFreq, P::EqLowGain, P::EqLowQ},
    {P::EqLoMidFreq, P::EqLoMidGain, P::EqLoMidQ},
    {P::EqHiMidFreq, P::EqHiMidGain, P::EqHiMidQ},
    {P::EqHighFreq, P::EqHighGain, P::EqHighQ},
};
const P kEqEnable[4] = {P::EqLowOn, P::EqLoMidOn, P::EqHiMidOn, P::EqHighOn};
const char* kEqNames[4] = {"LOW", "LO-MID", "HI-MID", "HIGH"};

std::vector<float> buildEqCurve(const ParamStore& p, int points) {
  std::vector<float> out((size_t)points, 0.0f);
  if (!p.getBool(P::EqOn)) return out;
  const double sr = 48000.0;
  dsp::Biquad bands[4];
  const dsp::Biquad::Type types[4] = {dsp::Biquad::LowShelf, dsp::Biquad::Peak, dsp::Biquad::Peak,
                                      dsp::Biquad::HighShelf};
  const P gainIds[4] = {P::EqLowGain, P::EqLoMidGain, P::EqHiMidGain, P::EqHighGain};
  for (int b = 0; b < 4; ++b) {
    if (!p.getBool(kEqEnable[b])) continue;
    bands[b].set(types[b], sr, p.get(kEqBands[b][0]), p.get(gainIds[b]), p.get(kEqBands[b][2]));
  }
  dsp::Biquad hpf;
  hpf.set(dsp::Biquad::HighPass, sr, p.get(P::HpfFreq), 0.0, 0.707);
  const double fMin = 20.0, fMax = 20000.0;
  for (int i = 0; i < points; ++i) {
    const double t = (double)i / (points - 1);
    const double f = fMin * std::pow(fMax / fMin, t);
    float db = 0.0f;
    for (int b = 0; b < 4; ++b)
      if (p.getBool(kEqEnable[b])) db += bands[b].magnitudeDb(f);
    if (p.getBool(P::HpfOn)) db += hpf.magnitudeDb(f);
    out[(size_t)i] = db;
  }
  return out;
}

} // namespace

// ---------------------------------------------------------------------------
void App::layout() {
  const float w = gfx_.width();
  const float h = gfx_.height();
  const float topH = 58.0f;
  const float botH = 28.0f;
  const float gap = 10.0f;

  rcTop_ = R(0, 0, w, topH);
  rcBottom_ = R(0, h - botH, w, h);

  const float bodyTop = topH + gap;
  const float bodyBottom = h - botH - gap;
  const float leftW = std::clamp(w * 0.285f, 360.0f, 472.0f);
  const float rightW = std::clamp(w * 0.225f, 292.0f, 372.0f);
  rcLeft_ = R(gap, bodyTop, gap + leftW, bodyBottom);
  rcRight_ = R(w - gap - rightW, bodyTop, w - gap, bodyBottom);
  rcCenter_ = R(rcLeft_.right + gap, bodyTop, rcRight_.left - gap, bodyBottom);
  needsRelayout_ = false;
}

void App::paint() {
  gfx_.begin();
  if (!gfx_.ready()) {
    gfx_.end();
    return;
  }
  layout();
  hits_.clear();

  const float w = gfx_.width(), h = gfx_.height();
  gfx_.vgrad(R(0, 0, w, h), 0.0f, T.bgDeep, T.bg);
  // Faint console sheen behind the panels.
  gfx_.vgrad(R(0, 0, w, 260), 0.0f, rgba(0x2A3038, 0.55f), rgba(0x0E1013, 0.0f));

  if (videoFullscreen_) {
    // The embedded player covers the window; a slim transport bar remains.
    rcVideo_ = R(0, 0, w, h);
    youtube_.setVisible(true);
    updateYouTubeBounds();
    drawStatusBar(rcBottom_);
    drawOverlays();
    gfx_.end();
    return;
  }

  drawTopBar(rcTop_);
  drawMicPanel(rcLeft_);
  drawCenter(rcCenter_);
  drawMaster(rcRight_);
  drawStatusBar(rcBottom_);
  drawOverlays();
  gfx_.end();
  syncWindowToOverlay();
}

// ---------------------------------------------------------------------------
void App::drawTopBar(D2D1_RECT_F r) {
  gfx_.vgrad(r, 0.0f, rgba(0x232830, 1.0f), rgba(0x14171B, 1.0f));
  gfx_.line(D2D1::Point2F(r.left, r.bottom - 0.5f), D2D1::Point2F(r.right, r.bottom - 0.5f),
            rgba(0x3A414B, 0.9f), 1.0f);

  // Logo mark
  const float markX = 16.0f, markY = 14.0f, markS = 30.0f;
  gfx_.shadow(R(markX, markY, markX + markS, markY + markS), 7.0f, 4.0f);
  gfx_.vgrad(R(markX, markY, markX + markS, markY + markS), 7.0f, T.accent, T.accentDeep);
  for (int i = 0; i < 3; ++i) {
    const float bh = 8.0f + i * 5.0f;
    const float bx = markX + 8.0f + i * 6.0f;
    gfx_.roundRect(R(bx, markY + markS - 8 - bh, bx + 3.0f, markY + markS - 8), 1.5f, T.ink);
  }
  gfx_.text("SOLO WORSHIPER", R(markX + markS + 10, 10, markX + markS + 210, 32), T.text,
            F_MED, AL_LEFT);
  gfx_.text(ui::spaced("WORSHIP VOCAL & KARAOKE CONSOLE"),
            R(markX + markS + 11, 31, markX + markS + 240, 47), T.faint, F_TINY, AL_LEFT);

  // Preset bar
  const float presetX = 250.0f;
  const float catW = 58.0f;
  const char* cats[3] = {"VOCAL", "REVERB", "EQ"};
  for (int i = 0; i < 3; ++i) {
    D2D1_RECT_F cr = R(presetX + i * (catW + 4), 9, presetX + i * (catW + 4) + catW, 29);
    const bool on = category_ == i;
    drawChip(gfx_, cr, cats[i], on, false, T.accent);
    addHit(kActionBase + (A_CAT_VOCAL + i), cr, 2);
  }

  // Current preset selector
  const int idx = presets_.findByName(settings_.lastPreset);
  const Preset* p = presets_.at(idx >= 0 ? idx : 0);
  std::string presetName = p ? p->name : "Default";
  const float selX = presetX + 3 * (catW + 4) + 6;
  const float selW = 240.0f;
  D2D1_RECT_F prevR = R(selX, 34, selX + 26, 54);
  D2D1_RECT_F nameR = R(selX + 30, 34, selX + 30 + selW, 54);
  D2D1_RECT_F nextR = R(nameR.right + 4, 34, nameR.right + 30, 54);
  drawButton(gfx_, prevR, "<", false, false);
  drawSelect(gfx_, nameR, presetName, false, false);
  drawButton(gfx_, nextR, ">", false, false);
  addHit(kActionBase + A_PRESET_PREV, prevR, 2);
  addHit(kActionBase + A_PRESET_NEXT, nameR, 2);
  addHit(kActionBase + A_PRESET_NEXT, nextR, 2);

  D2D1_RECT_F saveR = R(nextR.right + 8, 34, nextR.right + 68, 54);
  drawButton(gfx_, saveR, "SAVE", false, false);
  addHit(kActionBase + A_SAVE_PRESET, saveR, 2);

  D2D1_RECT_F abR = R(saveR.right + 6, 34, saveR.right + 48, 54);
  drawButton(gfx_, abR, usingA_ ? "A" : "B", true, false);
  addHit(kActionBase + A_AB, abR, 2);

  D2D1_RECT_F delR = R(abR.right + 6, 34, abR.right + 58, 54);
  drawButton(gfx_, delR, "DEL", false, false, true);
  addHit(kActionBase + A_DELETE_PRESET, delR, 2);

  // Status chips on the right
  EngineStats st = stats();
  auto chip = [&](float rightEdge, const std::string& text, D2D1_COLOR_F accent, bool filled) {
    const float w = gfx_.textWidth(W(text), F_TINY) + 20.0f;
    D2D1_RECT_F cr = R(rightEdge - w, 12, rightEdge, 32);
    if (filled) {
      gfx_.roundRect(cr, 10.0f, accent);
      gfx_.text(text, cr, T.ink, F_TINY, AL_CENTER);
    } else {
      gfx_.roundRect(cr, 10.0f, rgba(0x0F1114, 0.7f));
      gfx_.roundRectOutline(cr, 10.0f, T.strokeSoft, 1.0f);
      gfx_.text(text, cr, accent, F_TINY, AL_CENTER);
    }
    return cr;
  };

  float rightEdge = r.right - 16.0f;
  D2D1_RECT_F gear = R(rightEdge - 34, 30, rightEdge, 54);
  gfx_.roundRect(gear, 5.0f, T.card);
  gfx_.roundRectOutline(gear, 5.0f, T.strokeSoft, 1.0f);
  gfx_.circle(D2D1::Point2F((gear.left + gear.right) * 0.5f, (gear.top + gear.bottom) * 0.5f), 6.0f,
              T.dim);
  gfx_.circle(D2D1::Point2F((gear.left + gear.right) * 0.5f, (gear.top + gear.bottom) * 0.5f), 2.4f,
              T.card);
  addHit(kActionBase + A_SETTINGS, gear, 2);

  char buf[64];
  snprintf(buf, sizeof(buf), "%.1f ms", st.roundTripMs);
  rightEdge = chip(rightEdge - 42, buf, st.roundTripMs < 12.0f ? T.green : T.amber, false).left - 6;
  snprintf(buf, sizeof(buf), "%d smp", st.bufferFrames ? st.bufferFrames : 128);
  rightEdge = chip(rightEdge, buf, T.cyan, false).left - 6;
  snprintf(buf, sizeof(buf), "%.1f kHz", (st.sampleRate ? st.sampleRate : 48000) / 1000.0f);
  rightEdge = chip(rightEdge, buf, T.dim, false).left - 6;
  chip(rightEdge, st.asio ? "ASIO" : (st.exclusive ? "WASAPI EXCL" : "WASAPI"), T.accent, true);
}

// ---------------------------------------------------------------------------
void App::drawMicPanel(D2D1_RECT_F outer) {
  gfx_.shadow(outer, 8.0f, 8.0f);
  gfx_.vgrad(outer, 8.0f, T.panelTop, T.panelBottom);
  gfx_.roundRectOutline(outer, 8.0f, T.stroke, 1.0f);

  D2D1_RECT_F inner = inset(outer, 12.0f);
  gfx_.text("MICROPHONE CHANNEL", R(inner.left, inner.top, inner.right, inner.top + 18), T.text,
            F_LABEL, AL_LEFT);
  gfx_.text("GOST PREAMP / BRITISH EQ / VOCAL COMP / LEXICON REVERB",
            R(inner.left, inner.top + 17, inner.right, inner.top + 30), T.faint, F_TINY, AL_LEFT);

  // Input select
  D2D1_RECT_F inSelR = R(inner.left, inner.top + 34, inner.left + 132, inner.top + 56);
  char label[64];
  snprintf(label, sizeof(label), "INPUT %d", settings_.engine.captureChannel + 1);
  drawSelect(gfx_, inSelR, label, false, false);
  addHit(kActionBase + A_SELECT_INPUT_CH, inSelR, 3);

  D2D1_RECT_F phaseR = R(inSelR.right + 6, inner.top + 34, inSelR.right + 62, inner.top + 56);
  drawButton(gfx_, phaseR, "PHASE", params().getBool(P::PrePhase), false);
  addHit((int)P::PrePhase, phaseR, 2);

  D2D1_RECT_F modeR = R(phaseR.right + 6, inner.top + 34, inner.right, inner.top + 56);
  std::string mode = paramText((int)P::PreMode);
  drawSelect(gfx_, modeR, mode, false, false);
  addHit((int)P::PreMode, modeR, 3);

  // Input meter
  D2D1_RECT_F meterR = R(inner.left, inner.top + 62, inner.right, inner.top + 70);
  drawMeter(gfx_, meterR, micMeter_, micMeter_, false, true);
  gfx_.text("-60", R(meterR.left, meterR.top - 10, meterR.left + 24, meterR.top), T.faint, F_TINY,
            AL_LEFT);
  gfx_.text("0 dBFS", R(meterR.right - 44, meterR.top - 10, meterR.right, meterR.top), T.faint,
            F_TINY, AL_RIGHT);
  if (stats().inputPeak > 0.99f) {
    gfx_.text("CLIP", R(meterR.right - 92, meterR.top - 10, meterR.right - 48, meterR.top), T.red,
              F_TINY, AL_RIGHT, true);
  }

  // Scrollable channel strip
  const float contentTop = inner.top + 80.0f;
  const float contentBottom = outer.bottom - 10.0f;
  const float viewH = contentBottom - contentTop;
  gfx_.pushClip(R(outer.left + 2, contentTop, outer.right - 2, contentBottom));

  float y = contentTop - leftScroll_;

  auto section = [&](const char* title, float h, const char* rightLabel, int toggleParam) -> D2D1_RECT_F {
    D2D1_RECT_F sr = R(inner.left, y, inner.right, y + h);
    gfx_.roundRect(sr, 6.0f, rgba(0x121519, 0.65f));
    gfx_.roundRectOutline(sr, 6.0f, T.strokeSoft, 1.0f);
    gfx_.text(ui::spaced(title), R(sr.left + 10, sr.top + 5, sr.right - 80, sr.top + 18), T.dim,
              F_TINY, AL_LEFT);
    if (toggleParam >= 0) {
      D2D1_RECT_F tr = R(sr.right - 44, sr.top + 4, sr.right - 8, sr.top + 20);
      const bool on = params().getBool((P)toggleParam);
      drawChip(gfx_, tr, rightLabel ? rightLabel : "IN", on, false,
               toggleParam == (int)P::EqOn ? T.accent : T.cyan);
      addHit(toggleParam, tr, 2);
    }
    return sr;
  };

  // --- Preamp ------------------------------------------------------------
  {
    D2D1_RECT_F sr = section("PREAMP", 96.0f, nullptr, -1);
    const float kw = (width(sr) - 20.0f) / 5.0f;
    const P ids[5] = {P::PreGain, P::PreDrive, P::PreWarmth, P::PreSat, P::PreOut};
    for (int i = 0; i < 5; ++i) {
      D2D1_RECT_F kr = R(sr.left + 10 + i * kw, sr.top + 22, sr.left + 10 + (i + 1) * kw,
                         sr.top + 92);
      drawKnob(gfx_, kr, params().norm(ids[i]), paramLabel((int)ids[i]), paramText((int)ids[i]),
               true, false);
      addHit((int)ids[i], kr, 0);
    }
    y += 104.0f;
  }

  // --- High pass + EQ ----------------------------------------------------
  {
    D2D1_RECT_F sr = section("BRITISH CONSOLE EQ", 318.0f, "IN", (int)P::EqOn);
    // HPF knob on the left, curve on the right of the header row
    D2D1_RECT_F hpfR = R(sr.left + 8, sr.top + 20, sr.left + 74, sr.top + 88);
    drawKnob(gfx_, hpfR, params().norm(P::HpfFreq), "HPF", paramText((int)P::HpfFreq),
             params().getBool(P::HpfOn), false);
    addHit((int)P::HpfFreq, hpfR, 0);
    D2D1_RECT_F hpfOn = R(sr.left + 12, sr.top + 88, sr.left + 70, sr.top + 104);
    drawChip(gfx_, hpfOn, "HPF IN", params().getBool(P::HpfOn), false, T.cyan);
    addHit((int)P::HpfOn, hpfOn, 2);

    D2D1_RECT_F curveR = R(sr.left + 80, sr.top + 20, sr.right - 10, sr.top + 104);
    const auto curve = buildEqCurve(params(), 150);
    drawEqCurve(gfx_, curveR, curve, 18.0f, params().getBool(P::EqOn));
    gfx_.text("20", R(curveR.left, curveR.bottom - 12, curveR.left + 20, curveR.bottom),
              rgba(0x6A7380, 0.9f), F_TINY, AL_LEFT);
    gfx_.text("1k", R(curveR.left + width(curveR) * 0.45f, curveR.bottom - 12,
                      curveR.left + width(curveR) * 0.45f + 20, curveR.bottom),
              rgba(0x6A7380, 0.9f), F_TINY, AL_LEFT);
    gfx_.text("20k", R(curveR.right - 24, curveR.bottom - 12, curveR.right, curveR.bottom),
              rgba(0x6A7380, 0.9f), F_TINY, AL_RIGHT);

    // Band EQ preset
    D2D1_RECT_F presetR = R(sr.right - 116, sr.top + 4, sr.right - 50, sr.top + 20);
    drawChip(gfx_, presetR, "EQ PRESET", false, false, T.accent);
    addHit(kActionBase + A_EQ_PRESET, presetR, 2);

    // Four bands
    const float bandTop = sr.top + 110.0f;
    const float colW = (width(sr) - 20.0f) / 4.0f;
    for (int b = 0; b < 4; ++b) {
      const float cx = sr.left + 10 + b * colW;
      const bool on = params().getBool(kEqEnable[b]);
      D2D1_RECT_F dot = R(cx + 2, bandTop + 2, cx + colW - 4, bandTop + 18);
      drawChip(gfx_, dot, kEqNames[b], on, false, T.accent);
      addHit((int)kEqEnable[b], dot, 2);

      for (int k = 0; k < 3; ++k) {
        const P id = kEqBands[b][k];
        D2D1_RECT_F kr = R(cx, bandTop + 20 + k * 58.0f, cx + colW - 4,
                           bandTop + 20 + k * 58.0f + 56.0f);
        drawKnob(gfx_, kr, params().norm(id), k == 0 ? "FREQ" : (k == 1 ? "GAIN" : "Q"),
                 paramText((int)id), paramEnabled((int)id), false, {true, true});
        addHit((int)id, kr, 0);
      }
    }
    y += 326.0f;
  }

  // --- Compressor --------------------------------------------------------
  {
    D2D1_RECT_F sr = section("VOCAL COMPRESSOR", 132.0f, "IN", (int)P::CompOn);
    const P ids[7] = {P::CompThresh, P::CompRatio, P::CompAttack,
                      P::CompRelease, P::CompKnee, P::CompMakeup, P::CompMix};
    const float kw = (width(sr) - 20.0f) / 7.0f;
    for (int i = 0; i < 7; ++i) {
      D2D1_RECT_F kr = R(sr.left + 10 + i * kw, sr.top + 22, sr.left + 10 + (i + 1) * kw,
                         sr.top + 86);
      drawKnob(gfx_, kr, params().norm(ids[i]), paramLabel((int)ids[i]), paramText((int)ids[i]),
               params().getBool(P::CompOn), false, {true, true});
      addHit((int)ids[i], kr, 0);
    }
    D2D1_RECT_F grR = R(sr.left + 10, sr.top + 96, sr.right - 10, sr.top + 110);
    drawGRMeter(gfx_, grR, stats().compGr, 20.0f);
    char grText[48];
    snprintf(grText, sizeof(grText), "-%.1f dB GR", stats().compGr);
    gfx_.text(grText, R(sr.left + 10, sr.top + 110, sr.right - 10, sr.top + 124), T.faint, F_TINY,
              AL_RIGHT);
    y += 140.0f;
  }

  // --- Reverb ------------------------------------------------------------
  {
    D2D1_RECT_F sr = section("LEXICON REVERB", 196.0f, "IN", (int)P::RevOn);
    D2D1_RECT_F typeR = R(sr.left + 10, sr.top + 20, sr.left + 132, sr.top + 40);
    drawSelect(gfx_, typeR, paramText((int)P::RevType), false, false);
    addHit(kActionBase + A_REV_TYPE, typeR, 3);

    D2D1_RECT_F revPresetR = R(sr.right - 168, sr.top + 4, sr.right - 50, sr.top + 20);
    drawChip(gfx_, revPresetR, "REV PRESET", false, false, T.accent);
    addHit(kActionBase + A_REV_PRESET, revPresetR, 2);

    D2D1_RECT_F tailR = R(typeR.right + 8, sr.top + 20, sr.right - 10, sr.top + 78);
    drawReverbTail(gfx_, tailR, params().get(P::RevDecay), params().get(P::RevSize),
                   params().get(P::RevDamp));

    const P ids[6] = {P::RevSend,     P::RevWet,  P::RevPredelay,
                      P::RevDecay,    P::RevSize, P::RevDiffusion};
    const float kw = (width(sr) - 20.0f) / 6.0f;
    for (int i = 0; i < 6; ++i) {
      D2D1_RECT_F kr = R(sr.left + 10 + i * kw, sr.top + 86, sr.left + 10 + (i + 1) * kw,
                         sr.top + 150);
      drawKnob(gfx_, kr, params().norm(ids[i]), paramLabel((int)ids[i]), paramText((int)ids[i]),
               params().getBool(P::RevOn), false, {false, true});
      addHit((int)ids[i], kr, 0);
    }
    const P ids2[3] = {P::RevEarly, P::RevHf, P::RevLf};
    const float kw2 = (width(sr) - 20.0f) / 3.0f;
    for (int i = 0; i < 3; ++i) {
      D2D1_RECT_F kr = R(sr.left + 10 + i * kw2, sr.top + 150, sr.left + 10 + (i + 1) * kw2,
                         sr.top + 190);
      drawKnob(gfx_, kr, params().norm(ids2[i]), paramLabel((int)ids2[i]),
               paramText((int)ids2[i]), params().getBool(P::RevOn), false, {false, true});
      addHit((int)ids2[i], kr, 0);
    }
    y += 204.0f;
  }

  y += 6.0f;
  leftContentHeight_ = y - (contentTop - leftScroll_);
  gfx_.popClip();

  // Scroll affordance
  if (leftContentHeight_ > viewH) {
    const float frac = viewH / leftContentHeight_;
    const float thumbH = std::max(28.0f, viewH * frac);
    const float maxScroll = std::max(1.0f, leftContentHeight_ - viewH);
    const float t = std::clamp(leftScroll_ / maxScroll, 0.0f, 1.0f);
    const float ty = contentTop + (viewH - thumbH) * t;
    gfx_.roundRect(R(outer.right - 5, contentTop, outer.right - 1, contentBottom), 2.0f,
                   rgba(0x000000, 0.3f));
    gfx_.roundRect(R(outer.right - 5, ty, outer.right - 1, ty + thumbH), 2.0f,
                   rgba(0xE5B96F, 0.5f));
  }
  leftScroll_ = std::clamp(leftScroll_, 0.0f,
                           std::max(0.0f, leftContentHeight_ - viewH));
}

// ---------------------------------------------------------------------------
void App::drawCenter(D2D1_RECT_F outer) {
  gfx_.shadow(outer, 8.0f, 8.0f);
  gfx_.vgrad(outer, 8.0f, T.panelTop, T.panelBottom);
  gfx_.roundRectOutline(outer, 8.0f, T.stroke, 1.0f);

  D2D1_RECT_F inner = inset(outer, 12.0f);
  gfx_.text("KARAOKE PLAYER", R(inner.left, inner.top, inner.left + 200, inner.top + 18), T.text,
            F_LABEL, AL_LEFT);
  gfx_.text("OFFICIAL YOUTUBE EMBED - FADER CONTROLS THE WINDOWS PLAYER SESSION",
            R(inner.left, inner.top + 17, inner.right, inner.top + 30), T.faint, F_TINY, AL_LEFT);

  // Music mute / solo
  D2D1_RECT_F muteR = R(inner.right - 108, inner.top, inner.right - 58, inner.top + 20);
  D2D1_RECT_F soloR = R(inner.right - 52, inner.top, inner.right, inner.top + 20);
  drawChip(gfx_, muteR, "MUTE", params().getBool(P::MusicMute), false, T.red);
  drawChip(gfx_, soloR, "SOLO", params().getBool(P::MusicSolo), false, T.cyan);
  addHit((int)P::MusicMute, muteR, 2);
  addHit((int)P::MusicSolo, soloR, 2);

  // URL row
  D2D1_RECT_F urlR = R(inner.left, inner.top + 34, inner.right - 146, inner.top + 58);
  const bool urlActive = editField_ == A_URL_FIELD;
  gfx_.roundRect(urlR, 4.0f, urlActive ? rgba(0x1E242B, 1.0f) : T.bgDeep);
  gfx_.roundRectOutline(urlR, 4.0f, urlActive ? T.accent : T.strokeSoft, 1.0f);
  if (urlActive) {
    const std::wstring shown = editText_;
    gfx_.text(shown, R(urlR.left + 8, urlR.top, urlR.right - 8, urlR.bottom), T.text, F_SMALL,
              AL_LEFT, true);
    const float caretX = urlR.left + 8 + gfx_.textWidth(shown.substr(0, caret_), F_SMALL) + 1;
    gfx_.line(D2D1::Point2F(caretX, urlR.top + 6), D2D1::Point2F(caretX, urlR.bottom - 6), T.accent,
              1.5f);
  } else {
    const std::wstring shown =
        settings_.lastUrl.empty()
            ? std::wstring(L"Paste a YouTube video or playlist link")
            : W(settings_.lastUrl.rfind("http", 0) == 0
                    ? settings_.lastUrl
                    : "youtube.com/watch?v=" + settings_.lastUrl);
    gfx_.text(shown, R(urlR.left + 8, urlR.top, urlR.right - 8, urlR.bottom),
              settings_.lastUrl.empty() ? T.faint : T.text, F_SMALL, AL_LEFT, true);
  }
  addHit(kActionBase + A_URL_FIELD, urlR, 5);

  D2D1_RECT_F pasteR = R(inner.right - 140, inner.top + 34, inner.right - 78, inner.top + 58);
  drawButton(gfx_, pasteR, "PASTE", false, false);
  addHit(kActionBase + A_PASTE, pasteR, 2);

  D2D1_RECT_F loadR = R(inner.right - 70, inner.top + 34, inner.right, inner.top + 58);
  drawButton(gfx_, loadR, "LOAD", true, false);
  addHit(kActionBase + A_YT_LOAD, loadR, 2);

  // Video area (16:9)
  const float availW = width(inner);
  const float chromeH = 34.0f + 24.0f + 40.0f + 96.0f + 24.0f;
  float videoW = availW;
  float maxVideoH = height(inner) - chromeH;
  if (maxVideoH < 120.0f) maxVideoH = 120.0f;
  float videoH = videoW * 9.0f / 16.0f;
  if (videoH > maxVideoH) {
    videoH = maxVideoH;
    videoW = videoH * 16.0f / 9.0f;
  }
  const float vx = inner.left + (availW - videoW) * 0.5f;
  const float vy = inner.top + 66.0f;
  D2D1_RECT_F videoR = R(vx, vy, vx + videoW, vy + videoH);
  rcVideo_ = videoR;
  gfx_.roundRect(videoR, 6.0f, rgba(0x08090B, 1.0f));
  gfx_.roundRectOutline(videoR, 6.0f, T.strokeSoft, 1.0f);
  if (!youtube_.loaded()) {
    gfx_.text("NO VIDEO LOADED", R(videoR.left, videoR.top + videoH * 0.5f - 24, videoR.right,
                                 videoR.top + videoH * 0.5f),
              rgba(0x5A6270, 1.0f), F_MED, AL_CENTER);
    gfx_.text("Paste a YouTube URL above and press LOAD. Playback uses the official embed; "
              "the audio is mixed in through Windows loopback capture.",
              R(videoR.left + 30, videoR.top + videoH * 0.5f, videoR.right - 30,
                videoR.top + videoH * 0.5f + 44),
              T.faint, F_SMALL, AL_CENTER);
  }

  // Transport
  const float ty = videoR.bottom + 10.0f;
  D2D1_RECT_F playR = R(inner.left, ty, inner.left + 62, ty + 28);
  D2D1_RECT_F prevR = R(playR.right + 4, ty, playR.right + 56, ty + 28);
  D2D1_RECT_F nextR = R(prevR.right + 4, ty, prevR.right + 56, ty + 28);
  D2D1_RECT_F backR = R(nextR.right + 8, ty, nextR.right + 60, ty + 28);
  D2D1_RECT_F fwdR = R(backR.right + 4, ty, backR.right + 56, ty + 28);
  D2D1_RECT_F queueR = R(inner.right - 154, ty, inner.right - 82, ty + 28);
  D2D1_RECT_F fullR = R(inner.right - 76, ty, inner.right, ty + 28);
  const bool playing = youtube_.playing();
  drawButton(gfx_, playR, playing ? "PAUSE" : "PLAY", playing, false);
  drawButton(gfx_, prevR, "PREV", false, false);
  drawButton(gfx_, nextR, "NEXT", false, false);
  drawButton(gfx_, backR, "-10s", false, false);
  drawButton(gfx_, fwdR, "+10s", false, false);
  drawButton(gfx_, fullR, "FULL", false, false);
  char queueLabel[40];
  if (queue_.empty())
    snprintf(queueLabel, sizeof(queueLabel), "PLAYLIST");
  else
    snprintf(queueLabel, sizeof(queueLabel), "PLAYLIST %d/%d", queue_.current() + 1,
             queue_.size());
  drawChip(gfx_, queueR, queueLabel, overlayQueue_, false, T.accent);
  addHit(kActionBase + A_YT_PLAY, playR, 2);
  addHit(kActionBase + A_QUEUE_PREV, prevR, 2);
  addHit(kActionBase + A_QUEUE_NEXT, nextR, 2);
  addHit(kActionBase + A_YT_BACK, backR, 2);
  addHit(kActionBase + A_YT_FWD, fwdR, 2);
  addHit(kActionBase + A_QUEUE_TOGGLE, queueR, 2);
  addHit(kActionBase + A_YT_FULLSCREEN, fullR, 2);

  // Seek bar
  const float seekX0 = fwdR.right + 12;
  // Stop before the playlist chip: the bar is drawn after the chip and would cover it.
  const float seekX1 = queueR.left - 10;
  D2D1_RECT_F seekR = R(seekX0, ty + 10, seekX1, ty + 18);
  const double dur = youtube_.duration();
  const double pos = youtube_.position();
  if (seekR.right > seekR.left) {
    gfx_.roundRect(seekR, 4.0f, T.bgDeep);
    if (dur > 0.5) {
      const float f = (float)std::clamp(pos / dur, 0.0, 1.0);
      gfx_.roundRect(R(seekR.left, seekR.top, seekR.left + width(seekR) * f, seekR.bottom), 4.0f,
                     T.accent);
      gfx_.circle(D2D1::Point2F(seekR.left + width(seekR) * f, (seekR.top + seekR.bottom) * 0.5f),
                  4.0f, T.text);
    }
    addHit(kActionBase + A_YT_SEEK, R(seekR.left, seekR.top - 8, seekR.right, seekR.bottom + 8), 4);
    char tb[64];
    snprintf(tb, sizeof(tb), "%s / %s", timecode(pos).c_str(), timecode(dur).c_str());
    gfx_.text(tb, R(seekR.left, seekR.bottom + 2, seekR.left + 120, seekR.bottom + 16), T.dim,
              F_TINY, AL_LEFT);
  }

  // Track title and what plays after it
  std::string titleText = youtube_.title();
  if (titleText.empty()) titleText = "No track loaded";
  gfx_.text(W(titleText), R(inner.left, ty + 32, inner.right - 300, ty + 50), T.text, F_SMALL,
            AL_LEFT, true);
  {
    std::string nextText;
    if (queue_.empty()) {
      nextText = youtube_.hasPlaylist() ? "EMBED PLAYLIST" : "NO PLAYLIST";
    } else {
      const QueueItem* nxt = queue_.at(queue_.current() + 1);
      nextText = nxt ? ("UP NEXT  " + nxt->title) : "END OF PLAYLIST";
    }
    gfx_.text(W(nextText), R(inner.left + 360, ty + 32, inner.right, ty + 50), T.faint, F_TINY,
              AL_RIGHT, true);
  }

  // Music channel strip
  D2D1_RECT_F stripR = R(inner.left, ty + 58, inner.right, inner.bottom);
  gfx_.roundRect(stripR, 6.0f, rgba(0x121519, 0.7f));
  gfx_.roundRectOutline(stripR, 6.0f, T.strokeSoft, 1.0f);
  gfx_.text(ui::spaced("MUSIC CHANNEL"), R(stripR.left + 10, stripR.top + 5, stripR.left + 112,
                                           stripR.top + 18),
            T.dim, F_TINY, AL_LEFT);

  // Routing is a real state, not a decoration. The chip shows what is selected,
  // the caption says what that means for the EQ/pan next to it.
  const bool busPossible = musicBusPossible();
  const bool busSelected = settings_.musicThroughBus && busPossible;
  const bool musicDirect = engine_.musicDirect();
  D2D1_RECT_F routeR = R(stripR.left + 112, stripR.top + 2, stripR.left + 256, stripR.top + 20);
  drawChip(gfx_, routeR, busSelected ? "MUSIC -> BUS" : "MUSIC -> WINDOWS",
           busSelected || musicDirect, false, busSelected ? T.amber : T.cyan);
  addHit(kActionBase + A_MUSIC_ROUTE, routeR, 2);

  D2D1_RECT_F musMeter = R(stripR.left + 10, stripR.top + 26, stripR.right - 210, stripR.top + 34);
  drawMeter(gfx_, musMeter, musMeter_, musMeter_, false, true);

  // Music fader (horizontal, absolute)
  D2D1_RECT_F musFader = R(stripR.left + 10, stripR.top + 44, stripR.right - 210, stripR.top + 62);
  gfx_.roundRect(musFader, 5.0f, T.bgDeep);
  gfx_.roundRectOutline(musFader, 5.0f, T.strokeSoft, 1.0f);
  {
    const float f = params().norm(P::MusicFader);
    gfx_.roundRect(R(musFader.left + 2, musFader.top + 2,
                     musFader.left + 2 + (width(musFader) - 4) * f, musFader.bottom - 2),
                   4.0f, T.cyan);
    const float knobX = musFader.left + 2 + (width(musFader) - 4) * f;
    gfx_.roundRect(R(knobX - 5, musFader.top + 1, knobX + 5, musFader.bottom - 1), 3.0f, T.text);
  }
  addHit((int)P::MusicFader, musFader, 4);
  gfx_.text("MUSIC LEVEL " + paramText((int)P::MusicFader),
            R(stripR.left + 10, stripR.top + 64, stripR.left + 200, stripR.top + 78), T.faint,
            F_TINY, AL_LEFT);
  gfx_.text(busSelected ? "EQ + PAN LIVE IN MONITOR AND RECORDING"
                        : (musicDirect ? "EQ + PAN APPLY TO THE RECORDING ONLY"
                                       : "EQ + PAN -> RECORDING WHEN THE PLAYER IS ACTIVE"),
            R(stripR.left + 200, stripR.top + 64, stripR.right - 210, stripR.top + 78),
            busSelected ? T.cyan : (musicDirect ? T.amber : T.faint), F_TINY, AL_RIGHT, true);

  // Music EQ
  const float eqX = stripR.right - 196;
  const P musEq[3] = {P::MusEqLow, P::MusEqMid, P::MusEqHigh};
  const float kw = 62.0f;
  for (int i = 0; i < 3; ++i) {
    D2D1_RECT_F kr = R(eqX + i * kw, stripR.top + 20, eqX + (i + 1) * kw, stripR.top + 78);
    drawKnob(gfx_, kr, params().norm(musEq[i]), paramLabel((int)musEq[i]),
             paramText((int)musEq[i]), params().getBool(P::MusEqOn), false, {false, true});
    addHit((int)musEq[i], kr, 0);
  }
  D2D1_RECT_F musEqOn = R(eqX - 64, stripR.top + 86, eqX - 6, stripR.top + 104);
  drawChip(gfx_, musEqOn, "EQ IN", params().getBool(P::MusEqOn), false, T.cyan);
  if (!busSelected) {
    gfx_.text("REC ONLY", R(eqX - 168, stripR.top + 86, eqX - 74, stripR.top + 104), T.amber,
              F_TINY, AL_RIGHT, true);
  }
  addHit((int)P::MusEqOn, musEqOn, 2);
}

// ---------------------------------------------------------------------------
void App::drawMaster(D2D1_RECT_F outer) {
  gfx_.shadow(outer, 8.0f, 8.0f);
  gfx_.vgrad(outer, 8.0f, T.panelTop, T.panelBottom);
  gfx_.roundRectOutline(outer, 8.0f, T.stroke, 1.0f);

  D2D1_RECT_F inner = inset(outer, 12.0f);
  gfx_.text("MASTER MIXER", R(inner.left, inner.top, inner.right, inner.top + 18), T.text,
            F_LABEL, AL_LEFT);
  gfx_.text("STEREO BUS / OUTPUT ROUTING", R(inner.left, inner.top + 17, inner.right,
                                            inner.top + 30),
            T.faint, F_TINY, AL_LEFT);

  // Faders
  const float faderTop = inner.top + 44.0f;
  const float faderH = std::clamp(height(inner) * 0.30f, 150.0f, 236.0f);
  const float faderW = (width(inner) - 40.0f) / 3.0f;
  const P faders[3] = {P::MicFader, P::MusicFader, P::MasterFader};
  const bool accents[3] = {true, false, true};
  for (int i = 0; i < 3; ++i) {
    D2D1_RECT_F fr = R(inner.left + i * faderW, faderTop, inner.left + (i + 1) * faderW,
                       faderTop + faderH);
    drawFader(gfx_, fr, params().norm(faders[i]), paramLabel((int)faders[i]),
              paramText((int)faders[i]), accents[i], false);
    addHit((int)faders[i], fr, 1);
  }

  // Stereo meters beside the faders
  const float meterTop = faderTop + 14.0f;
  const float meterH = faderH - 28.0f;
  D2D1_RECT_F mlR = R(inner.right - 34, meterTop, inner.right - 20, meterTop + meterH);
  D2D1_RECT_F mrR = R(inner.right - 18, meterTop, inner.right - 4, meterTop + meterH);
  drawMeter(gfx_, mlR, meterL_, peakL_, peakL_ > 0.985f, false);
  drawMeter(gfx_, mrR, meterR_, peakR_, peakR_ > 0.985f, false);
  gfx_.text("L", R(mlR.left - 2, mlR.top - 12, mlR.right + 2, mlR.top), T.faint, F_TINY, AL_CENTER);
  gfx_.text("R", R(mrR.left - 2, mrR.top - 12, mrR.right + 2, mrR.top), T.faint, F_TINY, AL_CENTER);

  float y = faderTop + faderH + 14.0f;

  // Mute / solo row for mic and music
  D2D1_RECT_F micMute = R(inner.left, y, inner.left + faderW * 0.5f - 3, y + 20);
  D2D1_RECT_F micSolo = R(inner.left + faderW * 0.5f + 1, y, inner.left + faderW - 4, y + 20);
  drawChip(gfx_, micMute, "MUTE", params().getBool(P::MicMute), false, T.red);
  drawChip(gfx_, micSolo, "SOLO", params().getBool(P::MicSolo), false, T.accent);
  addHit((int)P::MicMute, micMute, 2);
  addHit((int)P::MicSolo, micSolo, 2);

  D2D1_RECT_F musMute = R(inner.left + faderW, y, inner.left + faderW * 1.5f - 3, y + 20);
  D2D1_RECT_F musSolo = R(inner.left + faderW * 1.5f + 1, y, inner.left + faderW * 2 - 4, y + 20);
  drawChip(gfx_, musMute, "MUTE", params().getBool(P::MusicMute), false, T.red);
  drawChip(gfx_, musSolo, "SOLO", params().getBool(P::MusicSolo), false, T.cyan);
  addHit((int)P::MusicMute, musMute, 2);
  addHit((int)P::MusicSolo, musSolo, 2);

  D2D1_RECT_F masterMute = R(inner.left + faderW * 2, y, inner.right - 40, y + 20);
  drawChip(gfx_, masterMute, "MUTE", params().getBool(P::MasterMute), false, T.red);
  addHit((int)P::MasterMute, masterMute, 2);

  y += 30.0f;

  // Balance + limiter
  D2D1_RECT_F balR = R(inner.left, y, inner.left + 66, y + 62);
  drawKnob(gfx_, balR, params().norm(P::MasterBalance), "BALANCE",
           paramText((int)P::MasterBalance), true, false, {true, true});
  addHit((int)P::MasterBalance, balR, 0);

  D2D1_RECT_F limR = R(inner.left + 72, y + 2, inner.left + 150, y + 24);
  drawChip(gfx_, limR, "LIMITER", params().getBool(P::LimiterOn), false, T.accent);
  addHit((int)P::LimiterOn, limR, 2);

  D2D1_RECT_F limGr = R(inner.left + 72, y + 30, inner.right - 4, y + 42);
  drawGRMeter(gfx_, limGr, stats().limiterGr, 12.0f);
  char limText[48];
  snprintf(limText, sizeof(limText), "LIMIT %.1f dB", stats().limiterGr);
  gfx_.text(limText, R(inner.left + 72, y + 44, inner.right - 4, y + 58), T.faint, F_TINY,
            AL_LEFT);

  y += 80.0f;

  // Bus compressor
  D2D1_RECT_F bcR = R(inner.left, y, inner.right, y + 24);
  drawChip(gfx_, R(bcR.left, bcR.top, bcR.left + 96, bcR.top + 22), "BUS COMP",
           params().getBool(P::MasterCompOn), false, T.accent);
  addHit((int)P::MasterCompOn, R(bcR.left, bcR.top, bcR.left + 96, bcR.top + 22), 2);
  gfx_.text(paramText((int)P::MasterCompThr) + "  " + paramText((int)P::MasterCompRatio),
            R(bcR.left + 104, bcR.top, bcR.right, bcR.top + 22), T.dim, F_TINY, AL_RIGHT);

  y += 32.0f;

  // Routing
  gfx_.text(ui::spaced("OUTPUT ROUTING"), R(inner.left, y, inner.right, y + 14), T.faint, F_TINY,
            AL_LEFT);
  y += 16.0f;
  char pair[64];
  snprintf(pair, sizeof(pair), "MAIN OUT  %d/%d", settings_.engine.outLeft + 1,
           settings_.engine.outRight + 1);
  D2D1_RECT_F mainR = R(inner.left, y, inner.left + width(inner) * 0.52f, y + 22);
  drawSelect(gfx_, mainR, pair, false, false);
  addHit(kActionBase + A_SELECT_OUT_L, mainR, 3);

  // The second selector drives the right output channel; it used to show a
  // fixed "PHONES" label, so a change there was invisible.
  char pairR[32];
  snprintf(pairR, sizeof(pairR), "OUT %d", settings_.engine.outRight + 1);
  D2D1_RECT_F monR = R(mainR.right + 6, y, inner.right, y + 22);
  drawSelect(gfx_, monR, pairR, false, false);
  addHit(kActionBase + A_SELECT_OUT_R, monR, 3);

  y += 28.0f;

  // Recording
  D2D1_RECT_F recCard = R(inner.left, y, inner.right, inner.bottom);
  gfx_.roundRect(recCard, 6.0f, rgba(0x121519, 0.7f));
  gfx_.roundRectOutline(recCard, 6.0f, T.strokeSoft, 1.0f);
  gfx_.text(ui::spaced("RECORDING"), R(recCard.left + 10, recCard.top + 5, recCard.right,
                                       recCard.top + 18),
            T.dim, F_TINY, AL_LEFT);

  D2D1_RECT_F recBtn = R(recCard.left + 10, recCard.top + 22, recCard.left + 74, recCard.top + 44);
  drawButton(gfx_, recBtn, recording_ ? "STOP" : "REC", recording_, false, recording_);
  addHit(kActionBase + A_REC_TOGGLE, recBtn, 2);

  char recInfo[96];
  if (recording_) {
    const double secs = stats().frames ? engine_.recordingSeconds() : 0.0;
    snprintf(recInfo, sizeof(recInfo), "%s  %d-bit", timecode(secs).c_str(),
             settings_.recordBits);
  } else {
    snprintf(recInfo, sizeof(recInfo), "%d-bit WAV", settings_.recordBits);
  }
  gfx_.text(recInfo, R(recBtn.right + 8, recCard.top + 22, recCard.right - 10, recCard.top + 44),
            recording_ ? T.red : T.dim, F_SMALL, AL_LEFT);

  D2D1_RECT_F fmtR = R(recCard.left + 10, recCard.top + 50, recCard.left + 78, recCard.top + 70);
  D2D1_RECT_F srcR = R(fmtR.right + 6, recCard.top + 50, fmtR.right + 118, recCard.top + 70);
  drawChip(gfx_, fmtR, settings_.recordBits == 24 ? "24-BIT" : "32F", false, false, T.accent);
  drawChip(gfx_, srcR, settings_.recordVocal ? "VOCAL" : "MASTER", false, false, T.accent);
  addHit(kActionBase + A_REC_FORMAT, fmtR, 2);
  addHit(kActionBase + A_REC_SOURCE, srcR, 2);

  D2D1_RECT_F folderR = R(recCard.left + 10, recCard.top + 76, recCard.right - 10,
                          recCard.top + 96);
  drawButton(gfx_, folderR, "OPEN RECORDINGS FOLDER", false, false);
  addHit(kActionBase + A_REC_FOLDER, folderR, 2);

  D2D1_RECT_F folder2 = R(recCard.left + 10, recCard.top + 100, recCard.right - 10,
                          recCard.top + 118);
  gfx_.text(W(json::Value::toUtf8(settings_.recordingFolder)), folder2, T.faint, F_TINY, AL_LEFT,
            true);
}

// ---------------------------------------------------------------------------
void App::drawStatusBar(D2D1_RECT_F r) {
  gfx_.vgrad(r, 0.0f, rgba(0x14171B, 1.0f), rgba(0x0C0E11, 1.0f));
  gfx_.line(D2D1::Point2F(r.left, r.top + 0.5f), D2D1::Point2F(r.right, r.top + 0.5f),
            rgba(0x2E343D, 1.0f), 1.0f);

  EngineStats st = stats();
  const bool ok = st.running && (st.renderOpen || st.asio);
  const D2D1_COLOR_F dot = ok ? T.green : T.red;
  gfx_.circle(D2D1::Point2F(r.left + 16, (r.top + r.bottom) * 0.5f), 4.0f, dot);

  char buf[256];
  std::wstring device = W("AUDIO READY");
  if (errorText_.size()) device = errorText_;
  else if (st.asio) device = L"ASIO / FOCUSRITE";
  else if (st.renderOpen) device = L"WASAPI";
  snprintf(buf, sizeof(buf), "  %d Hz  |  %d smp  |  %.1f ms  |  CPU %.0f%%  |  dropouts %d", 
           st.sampleRate ? st.sampleRate : 48000, st.bufferFrames, st.roundTripMs,
           st.cpuLoad * 100.0f, st.dropouts);
  gfx_.text(device + W(buf), R(r.left + 26, r.top, r.right - 300, r.bottom),
            errorText_.size() ? T.red : T.dim, F_TINY, AL_LEFT, true);

  std::wstring right = recording_ ? L"● RECORDING" : L"READY";
  gfx_.text(right, R(r.right - 260, r.top, r.right - 12, r.bottom),
            recording_ ? T.red : rgba(0x6A7380, 1.0f), F_TINY, AL_RIGHT);

  // Where the music level actually lives, and the feedback caveat when the
  // karaoke is captured from the very device the microphone monitors on.
  if (engine_.musicDirect()) {
    gfx_.text("MUSIC FADER -> WINDOWS PLAYER SESSION", R(r.right - 620, r.top, r.right - 270, r.bottom),
              T.cyan, F_TINY, AL_RIGHT, true);
  } else if (settings_.engine.captureEnabled && !settings_.engine.loopbackId.empty() &&
             settings_.engine.loopbackId == settings_.engine.renderId) {
    gfx_.text("FEEDBACK RISK: karaoke is captured from the output device in use. Use headphones.",
              R(r.right - 660, r.top, r.right - 270, r.bottom), T.amber, F_TINY, AL_RIGHT, true);
  }
}

// ---------------------------------------------------------------------------
void App::drawPopup() {
  if (!popup_.open || popup_.items.empty()) return;
  const float itemH = 24.0f;
  const float contentH = itemH * popup_.items.size() + 8.0f;
  // A machine can report a dozen or more endpoints; the list must fit on screen
  // and stay scrollable, otherwise later entries can never be picked.
  const float maxH = std::min(340.0f, std::max(140.0f, gfx_.height() - 140.0f));
  const float listH = std::min(maxH, contentH);
  D2D1_RECT_F anchor = popup_.anchor;
  // Device names are long ("Microphone (High Definition Audio Device)"), so the
  // list is wider than the compact parameter menus used to need.
  const float listW = std::min(360.0f, gfx_.width() - 40.0f);
  float lx = anchor.left;
  float ly = anchor.bottom + 4;
  if (lx + listW > gfx_.width() - 8) lx = gfx_.width() - listW - 8;
  if (ly + listH > gfx_.height() - 40) ly = anchor.top - listH - 4;
  if (ly < 8) ly = 8;
  D2D1_RECT_F listR = R(lx, ly, lx + listW, ly + listH);
  const float viewTop = listR.top + 4;
  const float viewBottom = listR.bottom - 4;
  const float maxScroll = std::max(0.0f, contentH - 8.0f - (viewBottom - viewTop));
  popup_.scroll = std::clamp(popup_.scroll, 0.0f, maxScroll);
  gfx_.shadow(listR, 6.0f, 10.0f);
  gfx_.roundRect(listR, 6.0f, T.panelTop);
  gfx_.roundRectOutline(listR, 6.0f, T.stroke, 1.0f);
  float iy = viewTop - popup_.scroll;
  for (size_t i = 0; i < popup_.items.size(); ++i) {
    D2D1_RECT_F ir = R(listR.left + 4, iy, listR.right - 4, iy + itemH);
    if (ir.bottom > viewTop && ir.top < viewBottom) {
      const bool sel = (int)i == popup_.selected;
      if (sel) gfx_.roundRect(ir, 4.0f, rgba(0xE5B96F, 0.16f));
      gfx_.text(W(popup_.items[i]), R(ir.left + 8, ir.top, ir.right - 8, ir.bottom),
                sel ? T.accent : T.text, F_SMALL, AL_LEFT, true);
      // Only fully visible rows are clickable, so a hit never targets a
      // half-clipped entry.
      if (ir.top >= viewTop && ir.bottom <= viewBottom) addHit(9000 + (int)i, ir, 6, (int)i);
    }
    iy += itemH;
  }
  if (maxScroll > 0.0f) {
    const float trackH = viewBottom - viewTop;
    const float thumbH = std::max(24.0f, trackH * trackH / (contentH - 8.0f));
    const float t = maxScroll > 0.0f ? popup_.scroll / maxScroll : 0.0f;
    gfx_.roundRect(R(listR.right - 5, viewTop + t * (trackH - thumbH), listR.right - 2,
                     viewTop + t * (trackH - thumbH) + thumbH),
                   1.5f, rgba(0xE5B96F, 0.5f));
  }
}

// ---------------------------------------------------------------------------
// The shared play queue: one ordered list for playlists and local files.
// ---------------------------------------------------------------------------
void App::drawQueuePanel() {
  gfx_.rect(R(0, 0, gfx_.width(), gfx_.height()), rgba(0x000000, 0.55f));
  const float pw = std::min(720.0f, gfx_.width() - 80.0f);
  const float ph = std::min(620.0f, gfx_.height() - 80.0f);
  D2D1_RECT_F panel = R((gfx_.width() - pw) * 0.5f, (gfx_.height() - ph) * 0.5f,
                        (gfx_.width() + pw) * 0.5f, (gfx_.height() + ph) * 0.5f);
  gfx_.shadow(panel, 10.0f, 18.0f);
  gfx_.vgrad(panel, 10.0f, T.panelTop, T.panelBottom);
  gfx_.roundRectOutline(panel, 10.0f, T.stroke, 1.0f);

  D2D1_RECT_F in = inset(panel, 18.0f);
  gfx_.text("PLAYLIST", R(in.left, in.top, in.right - 80, in.top + 22), T.text, F_TITLE, AL_LEFT);
  std::wstring subtitle;
  if (queue_.empty()) {
    subtitle = youtube_.hasPlaylist() ? L"Listing unavailable - the YouTube embed plays its own order"
                                      : L"No playlist loaded";
  } else {
    subtitle = queue_.name() + L"  -  " + std::to_wstring(queue_.current() + 1) + L" of " +
               std::to_wstring(queue_.size());
  }
  gfx_.text(subtitle, R(in.left, in.top + 24, in.right - 80, in.top + 40), T.faint, F_TINY, AL_LEFT,
            true);

  D2D1_RECT_F closeR = R(in.right - 72, in.top, in.right, in.top + 22);
  drawButton(gfx_, closeR, "CLOSE", false, false);
  addHit(kActionBase + A_QUEUE_CLOSE, closeR, 2);

  const float listTop = in.top + 46.0f;
  const float listBottom = in.bottom - 48.0f;
  queueViewH_ = listBottom - listTop;
  const float rowH = 30.0f;
  queueContentH_ = rowH * (float)std::max(1, queue_.size());

  gfx_.pushClip(R(in.left - 4, listTop, in.right + 6, listBottom));
  float y = listTop - queueScroll_;
  for (int i = 0; i < queue_.size(); ++i) {
    const QueueItem* item = queue_.at(i);
    if (!item) continue;
    D2D1_RECT_F row = R(in.left, y, in.right, y + rowH - 3);
    const bool cur = i == queue_.current();
    gfx_.roundRect(row, 5.0f, cur ? rgba(0xE5B96F, 0.16f) : rgba(0x0F1114, 0.65f));
    if (cur) gfx_.roundRect(R(row.left, row.top, row.left + 3, row.bottom), 1.5f, T.accent);
    char idx[16];
    snprintf(idx, sizeof(idx), "%d", i + 1);
    gfx_.text(idx, R(row.left + 10, row.top, row.left + 40, row.bottom), cur ? T.accent : T.faint,
              F_TINY, AL_LEFT);
    std::string title = item->title;
    if (item->kind == QueueItem::Kind::LocalFile && title.empty()) {
      title = json::Value::toUtf8(item->path);
    }
    gfx_.text(W(title), R(row.left + 44, row.top, row.right - 92, row.bottom),
              cur ? T.text : T.dim, F_SMALL, AL_LEFT, true);
    if (!item->duration.empty()) {
      gfx_.text(item->duration, R(row.right - 88, row.top, row.right - 12, row.bottom),
                cur ? T.accent : T.faint, F_TINY, AL_RIGHT);
    }
    if (row.bottom > listTop && row.top < listBottom) addHit(kQueueBase + i, row, 7);
    y += rowH;
  }
  gfx_.popClip();

  if (queueContentH_ > queueViewH_) {
    const float frac = queueViewH_ / queueContentH_;
    const float thumbH = std::max(26.0f, queueViewH_ * frac);
    const float maxScroll = std::max(1.0f, queueContentH_ - queueViewH_);
    const float t = std::clamp(queueScroll_ / maxScroll, 0.0f, 1.0f);
    const float thumbY = listTop + (queueViewH_ - thumbH) * t;
    gfx_.roundRect(R(in.right + 1, listTop, in.right + 5, listBottom), 2.0f, rgba(0x000000, 0.3f));
    gfx_.roundRect(R(in.right + 1, thumbY, in.right + 5, thumbY + thumbH), 2.0f,
                   rgba(0xE5B96F, 0.6f));
  }

  D2D1_RECT_F prevR = R(in.left, in.bottom - 32, in.left + 96, in.bottom);
  D2D1_RECT_F nextR = R(prevR.right + 8, in.bottom - 32, prevR.right + 104, in.bottom);
  drawButton(gfx_, prevR, "PREVIOUS", false, false);
  drawButton(gfx_, nextR, "NEXT", false, false);
  addHit(kActionBase + A_QUEUE_PREV, prevR, 2);
  addHit(kActionBase + A_QUEUE_NEXT, nextR, 2);
  gfx_.text("Click a song to jump to it.", R(nextR.right + 12, in.bottom - 32, in.right, in.bottom),
            T.faint, F_TINY, AL_RIGHT, true);

  queueScroll_ = std::clamp(queueScroll_, 0.0f, std::max(0.0f, queueContentH_ - queueViewH_));
}

void App::drawOverlays() {
  if (overlayQueue_) drawQueuePanel();

  if (overlaySettings_) {
    gfx_.rect(R(0, 0, gfx_.width(), gfx_.height()), rgba(0x000000, 0.55f));
    const float pw = std::min(880.0f, gfx_.width() - 80.0f);
    const float ph = std::min(640.0f, gfx_.height() - 80.0f);
    D2D1_RECT_F panel = R((gfx_.width() - pw) * 0.5f, (gfx_.height() - ph) * 0.5f,
                          (gfx_.width() + pw) * 0.5f, (gfx_.height() + ph) * 0.5f);
    gfx_.shadow(panel, 10.0f, 18.0f);
    gfx_.vgrad(panel, 10.0f, T.panelTop, T.panelBottom);
    gfx_.roundRectOutline(panel, 10.0f, T.stroke, 1.0f);
    drawSettingsPanel(panel);
  }

  if (overlaySave_) {
    gfx_.rect(R(0, 0, gfx_.width(), gfx_.height()), rgba(0x000000, 0.55f));
    D2D1_RECT_F panel = R((gfx_.width() - 440) * 0.5f, (gfx_.height() - 190) * 0.5f,
                          (gfx_.width() + 440) * 0.5f, (gfx_.height() + 190) * 0.5f);
    gfx_.shadow(panel, 10.0f, 18.0f);
    gfx_.vgrad(panel, 10.0f, T.panelTop, T.panelBottom);
    gfx_.roundRectOutline(panel, 10.0f, T.stroke, 1.0f);
    drawSavePanel(panel);
  }

  if (errorText_.size()) {
    const float w = std::min(gfx_.width() - 80.0f, 620.0f);
    D2D1_RECT_F toast = R((gfx_.width() - w) * 0.5f, gfx_.height() - 92, (gfx_.width() + w) * 0.5f,
                          gfx_.height() - 56);
    gfx_.roundRect(toast, 8.0f, rgba(0x3A1F1F, 0.97f));
    gfx_.roundRectOutline(toast, 8.0f, T.red, 1.0f);
    gfx_.text(errorText_, R(toast.left + 14, toast.top, toast.right - 14, toast.bottom), T.text,
              F_SMALL, AL_CENTER, true);
  } else if (status_.size()) {
    const float w = std::min(gfx_.width() - 80.0f, 620.0f);
    D2D1_RECT_F toast = R((gfx_.width() - w) * 0.5f, gfx_.height() - 92, (gfx_.width() + w) * 0.5f,
                          gfx_.height() - 56);
    gfx_.roundRect(toast, 8.0f, rgba(0x1D2429, 0.96f));
    gfx_.roundRectOutline(toast, 8.0f, T.strokeSoft, 1.0f);
    gfx_.text(status_, R(toast.left + 14, toast.top, toast.right - 14, toast.bottom), T.dim,
              F_SMALL, AL_CENTER, true);
  }

  // Last, so a dropdown is drawn on top of the panel that opened it: hitAt()
  // returns the topmost widget, and the panel used to swallow every click.
  drawPopup();
}

// ---------------------------------------------------------------------------
void App::drawSettingsPanel(D2D1_RECT_F panel) {
  D2D1_RECT_F in = inset(panel, 20.0f);
  gfx_.text("AUDIO SETUP", R(in.left, in.top, in.right, in.top + 22), T.text, F_TITLE, AL_LEFT);
  gfx_.text("Choose the hardware path, sample rate and buffer. Changes are applied on APPLY.",
            R(in.left, in.top + 24, in.right, in.top + 40), T.faint, F_TINY, AL_LEFT);

  D2D1_RECT_F closeR = R(in.right - 28, in.top - 2, in.right, in.top + 20);
  drawButton(gfx_, closeR, "X", false, false);
  addHit(kActionBase + A_CLOSE_OVERLAY, closeR, 2);

  float y = in.top + 56.0f;
  const float colW = (width(in) - 24.0f) * 0.5f;

  auto label = [&](const char* text, float x, float yy, float w) {
    gfx_.text(ui::spaced(text), R(x, yy, x + w, yy + 14), T.faint, F_TINY, AL_LEFT);
  };
  auto row = [&](const char* title, int action, const std::string& value, float x, float yy,
                 float w) {
    label(title, x, yy, w);
    D2D1_RECT_F sr = R(x, yy + 16, x + w, yy + 40);
    drawSelect(gfx_, sr, value, false, false);
    addHit(kActionBase + action, sr, 3);
    return sr;
  };

  // Left column: audio path
  std::string asioName = "WASAPI (Windows audio)";
  if (settings_.engine.useAsio) {
    for (auto& d : Engine::ListAsioDrivers())
      if (d.second == settings_.engine.asioClsid) asioName = json::Value::toUtf8(d.first);
  }
  row("AUDIO DRIVER", A_SELECT_ASIO, asioName, in.left, y, colW);

  // These lists are cached by App::refreshDevices: enumerating endpoints here
  // would activate an IAudioClient per device on every painted frame and
  // freeze the window.
  auto stateTag = [](const au::Endpoint& e) -> std::string {
    if (e.state & DEVICE_STATE_ACTIVE) return {};
    return (e.state & DEVICE_STATE_DISABLED) ? " (disabled)" : " (not plugged in)";
  };

  std::string inputName = "(none)";
  for (auto& e : captureDevices_)
    if (e.id == settings_.engine.captureId)
      inputName = json::Value::toUtf8(e.name) + stateTag(e);
  row("WASAPI INPUT", A_SELECT_CAPTURE, inputName, in.left, y + 52.0f, colW);

  std::string outputName = "(default)";
  for (auto& e : renderDevices_)
    if (e.id == settings_.engine.renderId)
      outputName = json::Value::toUtf8(e.name) + stateTag(e);
  row("WASAPI OUTPUT", A_SELECT_RENDER, outputName, in.left, y + 104.0f, colW);

  std::string loopName = "(none)";
  const std::wstring defaultRender = defaultRenderId_;
  for (auto& e : renderDevices_) {
    if (settings_.engine.loopbackFollowDefault && e.id == defaultRender)
      loopName = json::Value::toUtf8(e.name) + "  (default)";
    else if (!settings_.engine.loopbackFollowDefault && e.id == settings_.engine.loopbackId)
      loopName = json::Value::toUtf8(e.name);
  }
  row("KARAOKE CAPTURE (LOOPBACK)", A_SELECT_LOOPBACK, loopName, in.left, y + 156.0f, colW);

  row("SAMPLE RATE", A_SELECT_RATE, std::to_string(settings_.engine.sampleRate) + " Hz", in.left,
      y + 208.0f, colW);

  // Right column: buffer, channel mapping
  row("BUFFER SIZE", A_SELECT_BUFFER, std::to_string(settings_.engine.bufferFrames) + " samples",
      in.left + colW + 24.0f, y, colW);
  row("MICROPHONE INPUT", A_SELECT_INPUT_CH,
      "Input " + std::to_string(settings_.engine.captureChannel + 1), in.left + colW + 24.0f,
      y + 52.0f, colW);
  row("MAIN OUTPUT", A_SELECT_OUT_L,
      "Ch " + std::to_string(settings_.engine.outLeft + 1) + " / " +
          std::to_string(settings_.engine.outRight + 1),
      in.left + colW + 24.0f, y + 104.0f, colW);

  D2D1_RECT_F exR = R(in.left + colW + 24.0f, y + 156.0f, in.left + colW + 24.0f + 180.0f,
                      y + 178.0f);
  drawChip(gfx_, exR, "WASAPI EXCLUSIVE", settings_.engine.exclusive, false, T.accent);
  addHit(kActionBase + A_DEVICE_EXCLUSIVE, exR, 2);

  D2D1_RECT_F capR = R(exR.left, y + 186.0f, exR.left + 180.0f, y + 208.0f);
  drawChip(gfx_, capR, "MIC INPUT ENABLED", settings_.engine.captureEnabled, false, T.cyan);
  addHit(kActionBase + A_CAPTURE_ENABLE, capR, 2);

  // Capability readout
  EngineStats st = stats();
  char info[512];
  snprintf(info, sizeof(info),
           "RUNNING: %s   %d Hz   %d frames   in %.1f ms / out %.1f ms   %s\n"
           "BUFFER-LATENCY %.1f ms   STREAM %.1f ms   DEVICE CHANNELS %d   DROPOUTS %d   %s",
           st.asio ? "ASIO" : (st.exclusive ? "WASAPI EXCLUSIVE" : "WASAPI SHARED"), st.sampleRate,
           st.bufferFrames, st.asio ? st.asioInputLatencyMs : st.streamLatencyMs,
           st.asio ? st.asioOutputLatencyMs : st.streamLatencyMs,
           st.running ? "ENGINE ACTIVE" : "ENGINE STOPPED", st.bufferLatencyMs, st.streamLatencyMs,
           st.deviceChannels, st.dropouts, st.captureOpen ? "MIC OPEN" : "MIC CLOSED");
  gfx_.roundRect(R(in.left, y + 236.0f, in.right, y + 286.0f), 6.0f, rgba(0x0E1114, 0.9f));
  gfx_.text(info, R(in.left + 10, y + 240.0f, in.right - 10, y + 282.0f), T.dim, F_TINY, AL_LEFT);

  D2D1_RECT_F applyR = R(in.left, in.bottom - 40.0f, in.left + 190.0f, in.bottom - 12.0f);
  drawButton(gfx_, applyR, "APPLY & RESTART AUDIO", true, false);
  addHit(kActionBase + A_DEVICE_APPLY, applyR, 2);

  D2D1_RECT_F measureR = R(applyR.right + 10, in.bottom - 40.0f, applyR.right + 170.0f,
                           in.bottom - 12.0f);
  drawButton(gfx_, measureR, "MEASURE LATENCY", false, false);
  addHit(kActionBase + A_DEVICE_MEASURE, measureR, 2);

  D2D1_RECT_F panelR = R(measureR.right + 10, in.bottom - 40.0f, measureR.right + 190.0f,
                         in.bottom - 12.0f);
  drawButton(gfx_, panelR, "ASIO CONTROL PANEL", false, false);
  addHit(kActionBase + A_DEVICE_ASIO_PANEL, panelR, 2);
}

void App::drawSavePanel(D2D1_RECT_F panel) {
  D2D1_RECT_F in = inset(panel, 20.0f);
  gfx_.text("SAVE PRESET", R(in.left, in.top, in.right, in.top + 22), T.text, F_TITLE, AL_LEFT);
  const char* catName = category_ == 1 ? "REVERB" : (category_ == 2 ? "EQ" : "VOCAL");
  gfx_.text(std::string("Category: ") + catName +
                "   -   saves the current channel settings as a user preset",
            R(in.left, in.top + 24, in.right, in.top + 40), T.faint, F_TINY, AL_LEFT);

  D2D1_RECT_F field = R(in.left, in.top + 54, in.right, in.top + 82);
  gfx_.roundRect(field, 4.0f, T.bgDeep);
  gfx_.roundRectOutline(field, 4.0f, T.accent, 1.0f);
  gfx_.text(editText_, R(field.left + 8, field.top, field.right - 8, field.bottom), T.text,
            F_SMALL, AL_LEFT, true);
  const float caretX = field.left + 8 + gfx_.textWidth(editText_.substr(0, caret_), F_SMALL) + 1;
  gfx_.line(D2D1::Point2F(caretX, field.top + 6), D2D1::Point2F(caretX, field.bottom - 6), T.accent,
            1.5f);
  addHit(kActionBase + A_NAME_FIELD, field, 5);

  D2D1_RECT_F ok = R(in.left, in.bottom - 40, in.left + 150, in.bottom - 12);
  D2D1_RECT_F cancel = R(ok.right + 10, in.bottom - 40, ok.right + 130, in.bottom - 12);
  drawButton(gfx_, ok, "SAVE PRESET", true, false);
  drawButton(gfx_, cancel, "CANCEL", false, false);
  addHit(kActionBase + A_SAVE_CONFIRM, ok, 2);
  addHit(kActionBase + A_SAVE_CANCEL, cancel, 2);
}

} // namespace lvs
