#pragma once
// ---------------------------------------------------------------------------
// Rendering and widget toolkit for the console. Everything is drawn with
// Direct2D/DirectWrite in device-independent pixels, so the interface scales
// cleanly from 1280x720 to 4K and stays touch friendly.
// ---------------------------------------------------------------------------
#include <d2d1.h>
#include <dwrite.h>
#include <windows.h>

#include <string>
#include <vector>

namespace lvs {
namespace ui {

inline D2D1_COLOR_F rgb(unsigned hex, float a = 1.0f) {
  return D2D1::ColorF(((hex >> 16) & 0xFF) / 255.0f, ((hex >> 8) & 0xFF) / 255.0f,
                      (hex & 0xFF) / 255.0f, a);
}

inline D2D1_COLOR_F rgba(unsigned hex, float a) { return rgb(hex, a); }

struct Theme {
  D2D1_COLOR_F bg = rgb(0x0E1013);
  D2D1_COLOR_F bgDeep = rgb(0x0A0C0E);
  D2D1_COLOR_F panel = rgb(0x171A1F);
  D2D1_COLOR_F panelTop = rgb(0x21252B);
  D2D1_COLOR_F panelBottom = rgb(0x15181C);
  D2D1_COLOR_F card = rgb(0x1B1F25);
  D2D1_COLOR_F cardTop = rgb(0x232830);
  D2D1_COLOR_F stroke = rgb(0x2E343D);
  D2D1_COLOR_F strokeSoft = rgb(0x242931);
  D2D1_COLOR_F text = rgb(0xE9EDF3);
  D2D1_COLOR_F dim = rgb(0x9AA3B0);
  D2D1_COLOR_F faint = rgb(0x6A7380);
  D2D1_COLOR_F accent = rgb(0xE5B96F);      // brushed brass
  D2D1_COLOR_F accentDeep = rgb(0xB98A3F);
  D2D1_COLOR_F accentSoft = rgba(0xE5B96F, 0.16f);
  D2D1_COLOR_F cyan = rgb(0x63D2D9);
  D2D1_COLOR_F green = rgb(0x66D19E);
  D2D1_COLOR_F amber = rgb(0xE8C25C);
  D2D1_COLOR_F red = rgb(0xE0605E);
  D2D1_COLOR_F ink = rgb(0x0A0B0D);
};

extern Theme T;

enum FontId {
  F_TINY = 0,
  F_LABEL,
  F_SMALL,
  F_VALUE,
  F_BASE,
  F_MED,
  F_TITLE,
  F_BIG,
  F_HUGE,
  F_COUNT
};

enum Align { AL_LEFT = 0, AL_CENTER = 1, AL_RIGHT = 2 };

class Gfx {
public:
  ~Gfx();
  bool create(HWND hwnd);
  void destroy();
  void resize(int pixelW, int pixelH, float dpi);
  void begin();
  void end();
  bool ready() const { return rt_ != nullptr; }

  void rect(D2D1_RECT_F r, D2D1_COLOR_F c);
  void roundRect(D2D1_RECT_F r, float radius, D2D1_COLOR_F c);
  void roundRectOutline(D2D1_RECT_F r, float radius, D2D1_COLOR_F c, float thickness = 1.0f);
  void vgrad(D2D1_RECT_F r, float radius, D2D1_COLOR_F top, D2D1_COLOR_F bottom);
  void hgrad(D2D1_RECT_F r, float radius, D2D1_COLOR_F left, D2D1_COLOR_F right);
  void shadow(D2D1_RECT_F r, float radius, float depth = 6.0f);
  void line(D2D1_POINT_2F a, D2D1_POINT_2F b, D2D1_COLOR_F c, float thickness = 1.0f);
  void arc(D2D1_POINT_2F center, float radius, float a0, float a1, D2D1_COLOR_F c,
           float thickness = 2.0f);
  void circle(D2D1_POINT_2F center, float radius, D2D1_COLOR_F c);
  void text(const std::wstring& s, D2D1_RECT_F r, D2D1_COLOR_F c, int font,
            int align = AL_LEFT, bool clip = false);
  void text(const char* s, D2D1_RECT_F r, D2D1_COLOR_F c, int font, int align = AL_LEFT);
  void text(const std::string& s, D2D1_RECT_F r, D2D1_COLOR_F c, int font,
            int align = AL_LEFT, bool clip = false);
  float textWidth(const std::wstring& s, int font);
  float textHeight(int font);

  void pushClip(D2D1_RECT_F r);
  void popClip();

  ID2D1RenderTarget* target() const { return rt_; }
  ID2D1Factory* factory() const { return factory_; }

  float width() const { return width_; }
  float height() const { return height_; }
  float dpi() const { return dpi_; }
  float scale() const { return dpi_ / 96.0f; }

private:
  void setBrush(D2D1_COLOR_F c);

  HWND hwnd_ = nullptr;
  ID2D1Factory* factory_ = nullptr;
  ID2D1HwndRenderTarget* rt_ = nullptr;
  ID2D1SolidColorBrush* brush_ = nullptr;
  IDWriteFactory* dw_ = nullptr;
  IDWriteTextFormat* fonts_[F_COUNT] = {};
  std::vector<ID2D1Layer*> layers_;
  std::vector<D2D1::Matrix3x2F> clips_;
  float width_ = 0, height_ = 0, dpi_ = 96.0f;
};

// --- Widget drawing ------------------------------------------------------
struct KnobStyle {
  bool accent = true;
  bool compact = false; // "small" is a macro in rpcndr.h
};

void drawKnob(Gfx& g, D2D1_RECT_F r, float norm, const std::string& label, const std::string& value,
              bool enabled, bool hover, KnobStyle style = {});
void drawFader(Gfx& g, D2D1_RECT_F r, float norm, const std::string& label,
               const std::string& value, bool accent, bool hover);
void drawButton(Gfx& g, D2D1_RECT_F r, const std::string& label, bool on, bool hover,
                bool danger = false);
void drawChip(Gfx& g, D2D1_RECT_F r, const std::string& label, bool on, bool hover,
              D2D1_COLOR_F accent);
void drawMeter(Gfx& g, D2D1_RECT_F r, float level, float peak, bool clip, bool horizontal = false);
void drawGRMeter(Gfx& g, D2D1_RECT_F r, float reductionDb, float maxDb);
void drawEqCurve(Gfx& g, D2D1_RECT_F r, const std::vector<float>& dbPerPixel, float rangeDb,
                 bool enabled);
void drawSpectrum(Gfx& g, D2D1_RECT_F r, const std::vector<float>& magnitudes);
void drawReverbTail(Gfx& g, D2D1_RECT_F r, float decay, float size, float damp);
void drawSelect(Gfx& g, D2D1_RECT_F r, const std::string& value, bool open, bool hover);

std::string spaced(const std::string& s);   // G A I N
std::string fmtDb(float v, int decimals = 1);
std::string fmtHz(float v);
std::string fmtMs(float v);
std::string fmtSec(float v);

} // namespace ui
} // namespace lvs
