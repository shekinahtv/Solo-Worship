#include "ui/Ui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

namespace lvs {
namespace ui {

Theme T;

namespace {
const int kFontSizes[F_COUNT] = {9, 10, 11, 12, 13, 15, 18, 22, 32};
const char* kBaseFont = "Segoe UI";

D2D1_POINT_2F polar(D2D1_POINT_2F c, float radius, float angle) {
  return D2D1::Point2F(c.x + std::cos(angle) * radius, c.y + std::sin(angle) * radius);
}

std::wstring widen(const char* s) {
  if (!s || !*s) return L"";
  const int srcLen = (int)strlen(s);
  int len = MultiByteToWideChar(CP_UTF8, 0, s, srcLen, nullptr, 0);
  if (len <= 0) return L"";
  std::wstring out((size_t)len, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s, srcLen, out.data(), len);
  return out;
}
} // namespace

std::string spaced(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    out += s[i];
    if (i + 1 < s.size()) out += ' ';
  }
  return out;
}

std::string fmtDb(float v, int decimals) {
  char buf[32];
  if (v <= -90.0f) return "-inf";
  snprintf(buf, sizeof(buf), "%+.*f", decimals, v);
  return buf;
}

std::string fmtHz(float v) {
  char buf[32];
  if (v >= 1000.0f)
    snprintf(buf, sizeof(buf), "%.1fk", v / 1000.0f);
  else
    snprintf(buf, sizeof(buf), "%.0f", v);
  return buf;
}

std::string fmtMs(float v) {
  char buf[32];
  if (v >= 100.0f)
    snprintf(buf, sizeof(buf), "%.0fms", v);
  else
    snprintf(buf, sizeof(buf), "%.1fms", v);
  return buf;
}

std::string fmtSec(float v) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%.2fs", v);
  return buf;
}

Gfx::~Gfx() { destroy(); }

bool Gfx::create(HWND hwnd) {
  hwnd_ = hwnd;
  if (!factory_) {
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory_))) return false;
  }
  if (!dw_) {
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   (IUnknown**)&dw_)))
      return false;
  }
  if (rt_) return true;

  RECT rc;
  GetClientRect(hwnd_, &rc);
  const UINT dpi = GetDpiForWindow(hwnd_);
  dpi_ = dpi ? (float)dpi : 96.0f;
  const D2D1_SIZE_U px = D2D1::SizeU(std::max(1L, rc.right - rc.left),
                                     std::max(1L, rc.bottom - rc.top));
  D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties();
  props.dpiX = dpi_;
  props.dpiY = dpi_;
  props.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE);
  D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps =
      D2D1::HwndRenderTargetProperties(hwnd_, px, D2D1_PRESENT_OPTIONS_NONE);
  if (FAILED(factory_->CreateHwndRenderTarget(props, hwndProps, &rt_))) return false;
  rt_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
  rt_->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &brush_);
  width_ = px.width * 96.0f / dpi_;
  height_ = px.height * 96.0f / dpi_;

  if (!fonts_[0]) {
    for (int i = 0; i < F_COUNT; ++i) {
      const DWRITE_FONT_WEIGHT weight =
          (i == F_LABEL || i == F_MED || i == F_TITLE || i == F_BIG) ? DWRITE_FONT_WEIGHT_SEMI_BOLD
                                                                    : DWRITE_FONT_WEIGHT_NORMAL;
      dw_->CreateTextFormat(widen(kBaseFont).c_str(), nullptr, weight,
                            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                            (float)kFontSizes[i], L"en-us", &fonts_[i]);
      if (fonts_[i]) {
        fonts_[i]->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        fonts_[i]->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
      }
    }
    // Letter-spaced small caps labels get their own tracking via manual spaces.
    if (fonts_[F_LABEL]) fonts_[F_LABEL]->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
  }
  return true;
}

void Gfx::destroy() {
  for (auto& f : fonts_)
    if (f) {
      f->Release();
      f = nullptr;
    }
  for (auto* l : layers_)
    if (l) l->Release();
  layers_.clear();
  clips_.clear();
  if (brush_) {
    brush_->Release();
    brush_ = nullptr;
  }
  if (rt_) {
    rt_->Release();
    rt_ = nullptr;
  }
  if (dw_) {
    dw_->Release();
    dw_ = nullptr;
  }
  if (factory_) {
    factory_->Release();
    factory_ = nullptr;
  }
}

void Gfx::resize(int pixelW, int pixelH, float dpi) {
  if (dpi < 48.0f) dpi = 96.0f;
  dpi_ = dpi;
  width_ = pixelW * 96.0f / dpi_;
  height_ = pixelH * 96.0f / dpi_;
  if (rt_) {
    rt_->SetDpi(dpi_, dpi_);
    rt_->Resize(D2D1::SizeU((UINT32)std::max(1, pixelW), (UINT32)std::max(1, pixelH)));
  }
}

void Gfx::setBrush(D2D1_COLOR_F c) {
  if (brush_) brush_->SetColor(c);
}

void Gfx::begin() {
  if (!rt_) {
    if (!create(hwnd_)) return;
  }
  rt_->BeginDraw();
  rt_->SetTransform(D2D1::Matrix3x2F::Identity());
  rt_->Clear(T.bg);
  clips_.clear();
}

void Gfx::end() {
  if (!rt_) return;
  HRESULT hr = rt_->EndDraw();
  if (hr == D2DERR_RECREATE_TARGET) {
    for (auto& f : fonts_)
      if (f) {
        f->Release();
        f = nullptr;
      }
    if (brush_) {
      brush_->Release();
      brush_ = nullptr;
    }
    rt_->Release();
    rt_ = nullptr;
  }
}

void Gfx::rect(D2D1_RECT_F r, D2D1_COLOR_F c) {
  if (!rt_) return;
  setBrush(c);
  rt_->FillRectangle(r, brush_);
}

void Gfx::roundRect(D2D1_RECT_F r, float radius, D2D1_COLOR_F c) {
  if (!rt_) return;
  setBrush(c);
  rt_->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), brush_);
}

void Gfx::roundRectOutline(D2D1_RECT_F r, float radius, D2D1_COLOR_F c, float thickness) {
  if (!rt_) return;
  setBrush(c);
  rt_->DrawRoundedRectangle(D2D1::RoundedRect(r, radius, radius), brush_, thickness);
}

void Gfx::vgrad(D2D1_RECT_F r, float radius, D2D1_COLOR_F top, D2D1_COLOR_F bottom) {
  if (!rt_) return;
  D2D1_GRADIENT_STOP stops[2] = {{0.0f, top}, {1.0f, bottom}};
  ID2D1GradientStopCollection* coll = nullptr;
  if (FAILED(rt_->CreateGradientStopCollection(stops, 2, &coll)) || !coll) return;
  ID2D1LinearGradientBrush* gb = nullptr;
  D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES props;
  props.startPoint = D2D1::Point2F(r.left, r.top);
  props.endPoint = D2D1::Point2F(r.left, r.bottom);
  if (SUCCEEDED(rt_->CreateLinearGradientBrush(props, coll, &gb)) && gb) {
    rt_->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), gb);
    gb->Release();
  }
  coll->Release();
}

void Gfx::hgrad(D2D1_RECT_F r, float radius, D2D1_COLOR_F left, D2D1_COLOR_F right) {
  if (!rt_) return;
  D2D1_GRADIENT_STOP stops[2] = {{0.0f, left}, {1.0f, right}};
  ID2D1GradientStopCollection* coll = nullptr;
  if (FAILED(rt_->CreateGradientStopCollection(stops, 2, &coll)) || !coll) return;
  ID2D1LinearGradientBrush* gb = nullptr;
  D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES props;
  props.startPoint = D2D1::Point2F(r.left, r.top);
  props.endPoint = D2D1::Point2F(r.right, r.top);
  if (SUCCEEDED(rt_->CreateLinearGradientBrush(props, coll, &gb)) && gb) {
    rt_->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), gb);
    gb->Release();
  }
  coll->Release();
}

void Gfx::shadow(D2D1_RECT_F r, float radius, float depth) {
  for (int i = 3; i >= 1; --i) {
    const float d = depth * (float)i / 3.0f;
    D2D1_RECT_F rr = D2D1::RectF(r.left - d * 0.4f, r.top + d * 0.5f, r.right + d * 0.4f,
                                 r.bottom + d * 0.9f);
    roundRect(rr, radius + d * 0.4f, D2D1::ColorF(0, 0, 0, 0.16f));
  }
}

void Gfx::line(D2D1_POINT_2F a, D2D1_POINT_2F b, D2D1_COLOR_F c, float thickness) {
  if (!rt_) return;
  setBrush(c);
  rt_->DrawLine(a, b, brush_, thickness);
}

void Gfx::arc(D2D1_POINT_2F center, float radius, float a0, float a1, D2D1_COLOR_F c,
              float thickness) {
  if (!rt_ || std::fabs(a1 - a0) < 0.001f) return;
  D2D1_POINT_2F p0 = polar(center, radius, a0);
  D2D1_POINT_2F p1 = polar(center, radius, a1);
  const bool large = std::fabs(a1 - a0) > 3.14159265f;
  ID2D1PathGeometry* geo = nullptr;
  if (FAILED(factory_->CreatePathGeometry(&geo)) || !geo) return;
  ID2D1GeometrySink* sink = nullptr;
  geo->Open(&sink);
  if (sink) {
    sink->BeginFigure(p0, D2D1_FIGURE_BEGIN_HOLLOW);
    D2D1_ARC_SEGMENT seg;
    seg.point = p1;
    seg.size = D2D1::SizeF(radius, radius);
    seg.rotationAngle = 0.0f;
    seg.sweepDirection = a1 > a0 ? D2D1_SWEEP_DIRECTION_CLOCKWISE
                                 : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE;
    seg.arcSize = large ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;
    sink->AddArc(seg);
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    sink->Release();
    setBrush(c);
    rt_->DrawGeometry(geo, brush_, thickness);
  }
  geo->Release();
}

void Gfx::circle(D2D1_POINT_2F center, float radius, D2D1_COLOR_F c) {
  if (!rt_) return;
  setBrush(c);
  rt_->FillEllipse(D2D1::Ellipse(center, radius, radius), brush_);
}

void Gfx::text(const std::wstring& s, D2D1_RECT_F r, D2D1_COLOR_F c, int font, int align,
               bool clip) {
  if (!rt_ || font < 0 || font >= F_COUNT || !fonts_[font] || s.empty()) return;
  const DWRITE_TEXT_ALIGNMENT a = align == AL_CENTER   ? DWRITE_TEXT_ALIGNMENT_CENTER
                                  : align == AL_RIGHT ? DWRITE_TEXT_ALIGNMENT_TRAILING
                                                      : DWRITE_TEXT_ALIGNMENT_LEADING;
  fonts_[font]->SetTextAlignment(a);
  setBrush(c);
  rt_->DrawTextW(s.c_str(), (UINT32)s.size(), fonts_[font], r, brush_,
                 clip ? D2D1_DRAW_TEXT_OPTIONS_CLIP : D2D1_DRAW_TEXT_OPTIONS_NONE);
}

void Gfx::text(const char* s, D2D1_RECT_F r, D2D1_COLOR_F c, int font, int align) {
  text(widen(s ? s : ""), r, c, font, align);
}

void Gfx::text(const std::string& s, D2D1_RECT_F r, D2D1_COLOR_F c, int font, int align,
               bool clip) {
  text(widen(s.c_str()), r, c, font, align, clip);
}

float Gfx::textWidth(const std::wstring& s, int font) {
  if (!dw_ || font < 0 || font >= F_COUNT || !fonts_[font] || s.empty()) return 0.0f;
  IDWriteTextLayout* layout = nullptr;
  float w = 0;
  if (SUCCEEDED(dw_->CreateTextLayout(s.c_str(), (UINT32)s.size(), fonts_[font], 4096.0f,
                                      100.0f, &layout)) &&
      layout) {
    DWRITE_TEXT_METRICS m;
    if (SUCCEEDED(layout->GetMetrics(&m))) w = m.widthIncludingTrailingWhitespace;
    layout->Release();
  }
  return w;
}

float Gfx::textHeight(int font) {
  return (font >= 0 && font < F_COUNT) ? (float)kFontSizes[font] * 1.45f : 14.0f;
}

void Gfx::pushClip(D2D1_RECT_F r) {
  if (!rt_) return;
  rt_->PushAxisAlignedClip(r, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
  clips_.push_back(D2D1::Matrix3x2F::Identity());
}

void Gfx::popClip() {
  if (!rt_ || clips_.empty()) return;
  rt_->PopAxisAlignedClip();
  clips_.pop_back();
}

// ---------------------------------------------------------------------------
// Widgets
// ---------------------------------------------------------------------------
void drawKnob(Gfx& g, D2D1_RECT_F r, float norm, const std::string& label,
              const std::string& value, bool enabled, bool hover, KnobStyle style) {
  norm = std::clamp(norm, 0.0f, 1.0f);
  const float w = r.right - r.left;
  const float h = r.bottom - r.top;
  const float labelH = 13.0f;
  const float valueH = style.compact ? 12.0f : 13.0f;
  const float dialSpace = std::max(16.0f, std::min(w, h - labelH - valueH));
  const float radius = std::clamp(dialSpace * 0.5f - 3.0f, 7.0f, 30.0f);
  const D2D1_POINT_2F c = D2D1::Point2F((r.left + r.right) * 0.5f,
                                        r.top + valueH + dialSpace * 0.5f);

  const float a0 = -2.35619449f; // -135 deg
  const float a1 = 2.35619449f;  // +135 deg
  const float av = a0 + (a1 - a0) * norm;

  // Dial body
  g.circle(D2D1::Point2F(c.x, c.y + 1.5f), radius + 1.0f, D2D1::ColorF(0, 0, 0, 0.35f));
  g.vgrad(D2D1::RectF(c.x - radius, c.y - radius, c.x + radius, c.y + radius), radius,
          hover ? T.panelTop : T.cardTop, T.panelBottom);
  g.roundRectOutline(D2D1::RectF(c.x - radius, c.y - radius, c.x + radius, c.y + radius), radius,
                     hover ? T.stroke : T.strokeSoft, 1.0f);

  // Scale ticks
  for (int i = 0; i <= 10; ++i) {
    const float a = a0 + (a1 - a0) * (i / 10.0f);
    const bool major = (i % 5 == 0);
    const float inner = radius - (major ? 5.0f : 3.0f);
    g.line(polar(c, inner, a), polar(c, radius - 0.5f, a), rgba(0x8A93A0, major ? 0.75f : 0.3f),
           1.0f);
  }

  // Arc track and value
  const float arcR = radius + 2.5f;
  g.arc(c, arcR, a0, a1, rgba(0x000000, 0.5f), 3.0f);
  if (enabled) {
    const D2D1_COLOR_F col = style.accent ? T.accent : T.cyan;
    g.arc(c, arcR, a0, av, col, 3.0f);
  } else {
    g.arc(c, arcR, a0, av, rgba(0x5A6270, 0.7f), 3.0f);
  }

  // Pointer
  const D2D1_POINT_2F tip = polar(c, radius * 0.72f, av);
  const D2D1_POINT_2F base = polar(c, radius * 0.24f, av);
  g.line(base, tip, enabled ? T.text : T.faint, 2.0f);
  g.circle(c, 2.0f, enabled ? rgba(0x000000, 0.6f) : T.faint);

  // Text
  g.text(value, D2D1::RectF(r.left - 4, r.top, r.right + 4, r.top + valueH),
         enabled ? T.text : T.faint, style.compact ? F_TINY : F_SMALL, AL_CENTER);
  g.text(label, D2D1::RectF(r.left - 4, r.bottom - labelH, r.right + 4, r.bottom),
         enabled ? T.dim : rgba(0x5A6270, 0.9f), F_TINY, AL_CENTER);
}

void drawFader(Gfx& g, D2D1_RECT_F r, float norm, const std::string& label,
               const std::string& value, bool accent, bool hover) {
  norm = std::clamp(norm, 0.0f, 1.0f);
  const float w = r.right - r.left;
  const float labelH = 14.0f;
  const float valueH = 14.0f;
  const float trackW = 10.0f;
  const float cx = (r.left + r.right) * 0.5f;
  const float top = r.top + valueH + 6.0f;
  const float bottom = r.bottom - labelH - 10.0f;
  const float trackH = bottom - top;

  // Value readout
  g.text(value, D2D1::RectF(r.left - 6, r.top, r.right + 6, r.top + valueH), T.text, F_SMALL,
         AL_CENTER);

  // Track
  g.roundRect(D2D1::RectF(cx - trackW * 0.5f, top, cx + trackW * 0.5f, bottom), trackW * 0.5f,
              T.bgDeep);
  g.roundRectOutline(D2D1::RectF(cx - trackW * 0.5f, top, cx + trackW * 0.5f, bottom),
                     trackW * 0.5f, T.strokeSoft, 1.0f);


  const float knobY = bottom - trackH * norm;
  const D2D1_COLOR_F fill = accent ? T.accent : T.cyan;
  if (knobY < bottom - 1) {
    g.roundRect(D2D1::RectF(cx - trackW * 0.5f + 2, knobY, cx + trackW * 0.5f - 2, bottom - 1), 2.5f,
                fill);
  }

  // Scale ticks
  for (int i = 0; i <= 8; ++i) {
    const float y = bottom - trackH * (i / 8.0f);
    const bool major = (i % 2 == 0);
    g.line(D2D1::Point2F(cx + trackW * 0.5f + 3, y),
           D2D1::Point2F(cx + trackW * 0.5f + (major ? 9 : 6), y),
           rgba(0x8A93A0, major ? 0.55f : 0.28f), 1.0f);
  }

  // Cap
  const float capH = 20.0f, capW = std::max(26.0f, w - 10.0f);
  D2D1_RECT_F cap = D2D1::RectF(cx - capW * 0.5f, knobY - capH * 0.5f, cx + capW * 0.5f,
                                knobY + capH * 0.5f);
  g.shadow(cap, 3.0f, 4.0f);
  g.vgrad(cap, 3.0f, hover ? T.panelTop : T.cardTop, T.panelBottom);
  g.roundRectOutline(cap, 3.0f, hover ? T.stroke : T.strokeSoft, 1.0f);
  g.line(D2D1::Point2F(cap.left + 5, knobY), D2D1::Point2F(cap.right - 5, knobY),
         accent ? T.accent : T.cyan, 1.6f);

  g.text(label, D2D1::RectF(r.left - 6, r.bottom - labelH, r.right + 6, r.bottom),
         T.dim, F_TINY, AL_CENTER);
}

void drawButton(Gfx& g, D2D1_RECT_F r, const std::string& label, bool on, bool hover,
                bool danger) {
  D2D1_COLOR_F base = on ? (danger ? T.red : T.accentDeep) : (hover ? T.panelTop : T.card);
  if (on && !danger) base = T.accentDeep;
  g.roundRect(r, 4.0f, base);
  g.roundRectOutline(r, 4.0f, on ? (danger ? T.red : T.accent) : (hover ? T.stroke : T.strokeSoft),
                     1.0f);
  const D2D1_COLOR_F txt = on ? T.ink : (hover ? T.text : T.dim);
  g.text(label, r, txt, F_LABEL, AL_CENTER);
}

void drawChip(Gfx& g, D2D1_RECT_F r, const std::string& label, bool on, bool hover,
              D2D1_COLOR_F accent) {
  if (on) {
    g.roundRect(r, 11.0f, accent);
    g.text(label, r, T.ink, F_TINY, AL_CENTER);
  } else {
    g.roundRect(r, 11.0f, hover ? rgba(0x2A3038, 1.0f) : rgba(0x202429, 1.0f));
    g.roundRectOutline(r, 11.0f, T.strokeSoft, 1.0f);
    g.text(label, r, hover ? T.text : T.dim, F_TINY, AL_CENTER);
  }
}

void drawMeter(Gfx& g, D2D1_RECT_F r, float level, float peak, bool clip, bool horizontal) {
  g.roundRect(r, 2.0f, T.bgDeep);
  g.roundRectOutline(r, 2.0f, T.strokeSoft, 1.0f);

  const D2D1_RECT_F inner = D2D1::RectF(r.left + 2, r.top + 2, r.right - 2, r.bottom - 2);
  const float len = horizontal ? (inner.right - inner.left) : (inner.bottom - inner.top);
  auto colorAt = [](float v) {
    if (v > 0.92f) return T.red;
    if (v > 0.78f) return T.amber;
    return T.green;
  };
  if (horizontal) {
    const float w = len * std::clamp(level, 0.0f, 1.0f);
    if (w > 0.5f) {
      D2D1_RECT_F f = D2D1::RectF(inner.left, inner.top, inner.left + w, inner.bottom);
      g.hgrad(f, 1.0f, T.green, colorAt(std::clamp(level, 0.0f, 1.0f)));
    }
    const float px = inner.left + len * std::clamp(peak, 0.0f, 1.0f);
    g.line(D2D1::Point2F(px, inner.top), D2D1::Point2F(px, inner.bottom),
           peak > 0.01f ? rgba(0xFFFFFF, 0.75f) : D2D1::ColorF(0, 0, 0, 0), 1.5f);
  } else {
    const float h = len * std::clamp(level, 0.0f, 1.0f);
    if (h > 0.5f) {
      D2D1_RECT_F f = D2D1::RectF(inner.left, inner.bottom - h, inner.right, inner.bottom);
      D2D1_GRADIENT_STOP stops[3] = {{0.0f, T.green}, {0.72f, T.amber}, {1.0f, T.red}};
      ID2D1GradientStopCollection* coll = nullptr;
      if (SUCCEEDED(g.target()->CreateGradientStopCollection(stops, 3, &coll)) && coll) {
        ID2D1LinearGradientBrush* gb = nullptr;
        D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES props;
        props.startPoint = D2D1::Point2F(inner.left, inner.bottom);
        props.endPoint = D2D1::Point2F(inner.left, inner.top);
        if (SUCCEEDED(g.target()->CreateLinearGradientBrush(props, coll, &gb)) && gb) {
          g.target()->FillRoundedRectangle(D2D1::RoundedRect(f, 1.0f, 1.0f), gb);
          gb->Release();
        }
        coll->Release();
      }
    }
    const float py = inner.bottom - len * std::clamp(peak, 0.0f, 1.0f);
    g.line(D2D1::Point2F(inner.left, py), D2D1::Point2F(inner.right, py),
           peak > 0.01f ? rgba(0xFFFFFF, 0.8f) : D2D1::ColorF(0, 0, 0, 0), 1.5f);
  }
  if (clip) g.circle(D2D1::Point2F(r.right - 5, r.top + 5), 3.0f, T.red);
}

void drawGRMeter(Gfx& g, D2D1_RECT_F r, float reductionDb, float maxDb) {
  g.roundRect(r, 2.0f, T.bgDeep);
  g.roundRectOutline(r, 2.0f, T.strokeSoft, 1.0f);
  const float frac = std::clamp(reductionDb / std::max(0.5f, maxDb), 0.0f, 1.0f);
  const D2D1_RECT_F inner = D2D1::RectF(r.left + 2, r.top + 2, r.right - 2, r.bottom - 2);
  // Gain reduction grows from the right, the way console meters read.
  const float w = (inner.right - inner.left) * frac;
  if (w > 0.5f) {
    g.roundRect(D2D1::RectF(inner.right - w, inner.top, inner.right, inner.bottom), 1.5f,
                T.accent);
  }
  for (int i = 1; i < 5; ++i) {
    const float x = inner.left + (inner.right - inner.left) * (i / 5.0f);
    g.line(D2D1::Point2F(x, inner.top), D2D1::Point2F(x, inner.top + 3), rgba(0x8A93A0, 0.4f),
           1.0f);
  }
}

void drawEqCurve(Gfx& g, D2D1_RECT_F r, const std::vector<float>& dbPerPixel, float rangeDb,
                 bool enabled) {
  g.roundRect(r, 4.0f, T.bgDeep);
  for (int i = 1; i < 8; ++i) {
    const float x = r.left + (r.right - r.left) * (i / 8.0f);
    g.line(D2D1::Point2F(x, r.top + 3), D2D1::Point2F(x, r.bottom - 3), rgba(0x2A3038, 1.0f),
           1.0f);
  }
  for (int i = 1; i < 4; ++i) {
    const float y = r.top + (r.bottom - r.top) * (i / 4.0f);
    g.line(D2D1::Point2F(r.left + 2, y), D2D1::Point2F(r.right - 2, y), rgba(0x2A3038, 1.0f),
           1.0f);
  }
  const float zeroY = (r.top + r.bottom) * 0.5f;
  g.line(D2D1::Point2F(r.left + 2, zeroY), D2D1::Point2F(r.right - 2, zeroY),
         rgba(0x4A525E, 1.0f), 1.0f);

  if (dbPerPixel.empty()) return;
  const float h = (r.bottom - r.top) * 0.5f;
  const float step = (r.right - r.left - 4) / std::max<size_t>(1, dbPerPixel.size() - 1);
  std::vector<D2D1_POINT_2F> pts;
  pts.reserve(dbPerPixel.size());
  for (size_t i = 0; i < dbPerPixel.size(); ++i) {
    const float db = std::clamp(dbPerPixel[i], -rangeDb, rangeDb);
    pts.push_back(D2D1::Point2F(r.left + 2 + step * (float)i, zeroY - h * (db / rangeDb)));
  }
  // Filled area under the curve
  if (g.target() && pts.size() > 1) {
    ID2D1PathGeometry* geo = nullptr;
    if (SUCCEEDED(g.factory()->CreatePathGeometry(&geo)) && geo) {
      ID2D1GeometrySink* sink = nullptr;
      geo->Open(&sink);
      if (sink) {
        sink->BeginFigure(D2D1::Point2F(pts.front().x, r.bottom - 2), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(pts.front());
        for (size_t i = 1; i < pts.size(); ++i) sink->AddLine(pts[i]);
        sink->AddLine(D2D1::Point2F(pts.back().x, r.bottom - 2));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        sink->Release();
        D2D1_GRADIENT_STOP stops[2] = {{0.0f, rgba(0xE5B96F, enabled ? 0.28f : 0.10f)},
                                       {1.0f, rgba(0xE5B96F, 0.01f)}};
        ID2D1GradientStopCollection* coll = nullptr;
        if (SUCCEEDED(g.target()->CreateGradientStopCollection(stops, 2, &coll)) && coll) {
          ID2D1LinearGradientBrush* gb = nullptr;
          D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES p;
          p.startPoint = D2D1::Point2F(r.left, r.top);
          p.endPoint = D2D1::Point2F(r.left, r.bottom);
          if (SUCCEEDED(g.target()->CreateLinearGradientBrush(p, coll, &gb)) && gb) {
            g.target()->FillGeometry(geo, gb);
            gb->Release();
          }
          coll->Release();
        }
      }
      geo->Release();
    }
    for (size_t i = 1; i < pts.size(); ++i)
      g.line(pts[i - 1], pts[i], enabled ? T.accent : T.faint, 2.0f);
  }
}

void drawSpectrum(Gfx& g, D2D1_RECT_F r, const std::vector<float>& mags) {
  if (mags.empty()) return;
  const float w = r.right - r.left;
  const float barW = w / (float)mags.size();
  for (size_t i = 0; i < mags.size(); ++i) {
    const float v = std::clamp(mags[i], 0.0f, 1.0f);
    const float h = (r.bottom - r.top) * v;
    if (h < 0.5f) continue;
    D2D1_RECT_F bar = D2D1::RectF(r.left + barW * (float)i + 0.5f, r.bottom - h,
                                  r.left + barW * (float)i + barW - 0.5f, r.bottom);
    const D2D1_COLOR_F col = v > 0.85f ? T.accent : rgba(0x63D2D9, 0.75f);
    g.roundRect(bar, 1.0f, col);
  }
}

void drawReverbTail(Gfx& g, D2D1_RECT_F r, float decay, float size, float damp) {
  g.roundRect(r, 4.0f, T.bgDeep);
  const int points = 120;
  std::vector<D2D1_POINT_2F> pts;
  pts.reserve(points);
  const float span = std::max(0.4f, decay);
  for (int i = 0; i < points; ++i) {
    const float t = span * (float)i / (points - 1);
    float env = std::exp(-3.0f * t / span);
    env *= 0.75f + 0.25f * std::exp(-t * (1.0f + damp * 6.0f));
    const float x = r.left + (r.right - r.left) * ((float)i / (points - 1));
    const float y = r.bottom - 4 - (r.bottom - r.top - 8) * env * (0.55f + size * 0.45f);
    pts.push_back(D2D1::Point2F(x, y));
  }
  for (size_t i = 1; i < pts.size(); ++i)
    g.line(pts[i - 1], pts[i], rgba(0x63D2D9, 0.85f), 1.6f);
  g.text("TAIL", D2D1::RectF(r.left + 6, r.top + 3, r.right - 6, r.top + 15),
         rgba(0x63D2D9, 0.5f), F_TINY, AL_LEFT);
}

void drawSelect(Gfx& g, D2D1_RECT_F r, const std::string& value, bool open, bool hover) {
  g.roundRect(r, 4.0f, open ? T.panelTop : (hover ? rgba(0x242A31, 1.0f) : T.card));
  g.roundRectOutline(r, 4.0f, open ? T.accent : T.strokeSoft, 1.0f);
  g.text(value, D2D1::RectF(r.left + 8, r.top, r.right - 18, r.bottom), T.text, F_SMALL, AL_LEFT);
  const float cx = r.right - 10;
  const float cy = (r.top + r.bottom) * 0.5f;
  g.line(D2D1::Point2F(cx - 4, cy - 2), D2D1::Point2F(cx, cy + 2), T.dim, 1.4f);
  g.line(D2D1::Point2F(cx, cy + 2), D2D1::Point2F(cx + 4, cy - 2), T.dim, 1.4f);
}

} // namespace ui
} // namespace lvs
