#pragma once
// ---------------------------------------------------------------------------
// SOLO WORSHIPER - application shell.
//
// App owns the window, the audio engine, presets, settings and the YouTube
// host. Console.cpp owns everything about the interface.
// ---------------------------------------------------------------------------
#include <windows.h>

#include <string>
#include <vector>

#include "audio/Engine.h"
#include "audio/Wasapi.h"
#include "core/Presets.h"
#include "core/Queue.h"
#include "core/Settings.h"
#include "ui/Ui.h"
#include "web/PlaylistFetch.h"
#include "web/YouTubeHost.h"

namespace lvs {

// Interface actions that are not parameters.
enum Action {
  A_NONE = 0,
  A_PRESET_PREV,
  A_PRESET_NEXT,
  A_SAVE_PRESET,
  A_AB,
  A_SETTINGS,
  A_CLOSE_OVERLAY,
  A_CAT_VOCAL,
  A_CAT_REVERB,
  A_CAT_EQ,
  A_YT_PLAY,
  A_YT_BACK,
  A_YT_FWD,
  A_YT_FULLSCREEN,
  A_YT_LOAD,
  A_YT_SEEK,
  A_YT_AUTOPLAY_TOGGLE,
  A_REC_TOGGLE,
  A_REC_FOLDER,
  A_REC_FORMAT,
  A_REC_SOURCE,
  A_DEVICE_APPLY,
  A_DEVICE_ASIO_PANEL,
  A_DEVICE_MEASURE,
  A_DEVICE_EXCLUSIVE,
  A_SELECT_ASIO,
  A_SELECT_CAPTURE,
  A_SELECT_RENDER,
  A_SELECT_LOOPBACK,
  A_SELECT_RATE,
  A_SELECT_BUFFER,
  A_SELECT_INPUT_CH,
  A_SELECT_OUT_L,
  A_SELECT_OUT_R,
  A_SELECT_KARAOKE_CUT,
  A_SAVE_CONFIRM,
  A_SAVE_CANCEL,
  A_DELETE_PRESET,
  A_URL_FIELD,
  A_NAME_FIELD,
  A_PASTE,
  A_EQ_PRESET,
  A_REV_TYPE,
  A_REV_PRESET,
  A_MUSIC_ROUTE,
  A_TEMPO_DOWN,
  A_TEMPO_UP,
  A_TEMPO_RESET,
  A_KARAOKE,
  A_QUEUE_TOGGLE,
  A_QUEUE_CLOSE,
  A_QUEUE_PREV,
  A_QUEUE_NEXT,
  A_CAPTURE_ENABLE,
  A_LIMITER_RESET,
  A_HEADPHONE_INFO,
  A_COUNT
};

constexpr int kActionBase = 1000;
constexpr int kParamActionBase = 2000; // param n inside a popup list
constexpr int kQueueBase = 12000;      // kQueueBase + row index in the queue list

struct Hit {
  int id = 0; // parameter index (0..kParamCount-1) or kActionBase + Action
  D2D1_RECT_F rect = {};
  int kind = 0; // 0 knob, 1 vertical fader, 2 button, 3 select, 4 horizontal slider, 5 meter
  int extra = 0;
};

struct Popup {
  bool open = false;
  int owner = 0;
  D2D1_RECT_F anchor = {};
  std::vector<std::string> items;
  std::vector<int> ids; // action ids or param indices
  int selected = 0;
  float scroll = 0.0f; // pixels scrolled when the list is longer than the window allows
};

class App {
public:
  static App* instance();
  bool init(HINSTANCE hinst);
  // Called from WM_NCCREATE, before CreateWindowExW returns.
  void attachWindow(HWND h) { hwnd_ = h; }
  int run();

  LRESULT handleMessage(UINT msg, WPARAM wp, LPARAM lp);

  // --- interface (Console.cpp) ---
  void paint();
  void layout();
  void onPointerDown(float x, float y);
  void onPointerMove(float x, float y);
  void onPointerUp();
  void onWheel(float x, float y, float delta);
  std::wstring clipboardText();
  void pasteIntoField();
  void drawTopBar(D2D1_RECT_F r);
  void drawMicPanel(D2D1_RECT_F r);
  void drawCenter(D2D1_RECT_F r);
  void drawMaster(D2D1_RECT_F r);
  void drawStatusBar(D2D1_RECT_F r);
  void drawOverlays();
  void drawSettingsPanel(D2D1_RECT_F r);
  void drawSavePanel(D2D1_RECT_F r);
  void drawPopup();
  void drawQueuePanel();

  // --- shared helpers ---
  void addHit(int id, D2D1_RECT_F r, int kind, int extra = 0);
  const Hit* hitAt(float x, float y) const;
  void runAction(int actionId, int value = 0);
  void applyParamDrag(int param, float startNorm, float dyPixels);
  std::string paramText(int param) const;
  std::string paramLabel(int param) const;
  bool paramEnabled(int param) const;

  // --- app logic ---
  void startEngine();
  void restartEngine();
  void loadPreset(int index);
  void selectCategory(int cat);
  void beginSavePreset();
  void confirmSavePreset();
  void toggleAB();
  void toggleRecording();
  void openRecordFolder();
  void openSettings();
  void closeOverlay();
  void applyDeviceConfig();
  // Endpoint enumeration is expensive (one IAudioClient activation per device),
  // so the lists are cached and refreshed on demand instead of per painted frame.
  void refreshDevices(bool force = false);
  void buildSelectPopup(int owner);
  void selectItem(int id);
  void beginPlaylistLoad(const std::string& listId, const std::string& startVideoId);
  void playQueueIndex(int index);
  void queueStep(int delta);
  void tick();
  void setMusicDirect(bool on, const std::wstring* note);
  // -1 / +1 step through the embed's playback rates, 0 puts it back to 1x.
  void stepPlaybackRate(int dir);
  // Bus monitoring is only honest when karaoke and console output are different
  // endpoints; otherwise the console's copy would double the song.
  bool musicBusPossible() const;
  void syncWindowToOverlay();
  void updateYouTubeBounds();
  bool isVideoFullscreen() const { return videoFullscreen_; }

  // state accessors for Console.cpp
  ParamStore& params() { return engine_.params(); }
  const ParamStore& params() const { return engine_.params(); }
  EngineStats stats() const { return engine_.stats(); }
  PresetManager& presets() { return presets_; }
  YouTubeHost& youtube() { return youtube_; }
  const AppSettings& settings() const { return settings_; }
  AppSettings& settings() { return settings_; }

private:
  static LRESULT CALLBACK WndProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

  HINSTANCE hinst_ = nullptr;
  HWND hwnd_ = nullptr;
  ui::Gfx gfx_;
  Engine engine_;
  PresetManager presets_;
  AppSettings settings_;
  YouTubeHost youtube_;

  bool running_ = true;
  bool needsRelayout_ = true;
  bool videoFullscreen_ = false;
  bool overlaySettings_ = false;
  bool overlaySave_ = false;
  bool overlayHelp_ = false;
  bool showPopup_ = false;
  Popup popup_;

  // Cached endpoint lists. Enumeration must never run from the paint path.
  std::vector<au::Endpoint> captureDevices_;
  std::vector<au::Endpoint> renderDevices_;
  std::wstring defaultRenderId_;
  double devicesLoadedAt_ = -1.0;

  std::wstring status_;
  std::wstring errorText_;
  double errorUntil_ = 0.0;

  // parameter drag state
  int dragId_ = -1;
  float dragStartY_ = 0, dragStartX_ = 0;
  float dragStartNorm_ = 0;
  int dragKind_ = -1;

  // meter ballistics in the UI thread
  float meterL_ = 0, meterR_ = 0, peakL_ = 0, peakR_ = 0, peakHoldTimeL_ = 0, peakHoldTimeR_ = 0;
  float micMeter_ = 0, musMeter_ = 0;
  double peakLTime_ = 0, peakRTime_ = 0;

  // spectrum
  std::vector<float> spectrum_;

  // A/B
  std::vector<float> slotA_, slotB_;
  bool usingA_ = true;

  // text editing
  int editField_ = A_NONE;
  std::wstring editText_;
  size_t caret_ = 0;

  // scrolling
  float leftScroll_ = 0, leftContentHeight_ = 0;

  // recording
  bool recording_ = false;
  double recordStart_ = 0;

  // preset category
  int category_ = 0; // 0 vocal, 1 reverb, 2 eq

  // Shared play queue: YouTube playlists now, local files with the file library.
  Queue queue_;
  PlaylistJob playlistJob_;
  bool overlayQueue_ = false;
  float queueScroll_ = 0.0f, queueContentH_ = 0.0f, queueViewH_ = 0.0f;
  std::string pendingPlaylistStart_;
  std::string lastEndedId_;

  // Song tempo as last chosen; the embed applies it on every track it loads.
  double playbackRate_ = 1.0;

  // Embedded-player Windows session volume (music fader) state
  bool sessionDirect_ = false;
  double lastSessionPush_ = 0.0;
  int lastSessionPct_ = -1;
  bool lastSessionMute_ = false;
  bool sessionDucked_ = false;
  std::wstring lastDuckNote_;
  int sessionHit_ = 0, sessionMiss_ = 0;

  // performance
  double lastTick_ = 0, startTime_ = 0;

  // layout rects (DIPs)
  D2D1_RECT_F rcTop_{}, rcLeft_{}, rcCenter_{}, rcRight_{}, rcBottom_{}, rcVideo_{};
  D2D1_RECT_F pendingAnchor_{};
  std::vector<Hit> hits_;
};

} // namespace lvs
