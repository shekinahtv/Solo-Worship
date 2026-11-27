#include "app/App.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <windowsx.h>

#include <cstdio>
#include <cstring>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

#include "audio/SessionVolume.h"
#include "core/Json.h"
#include "dsp/Effects.h"

namespace lvs {

namespace {
App* g_app = nullptr;

// Startup diagnostics: %APPDATA%\SoloWorshiper\startup.log
void logLine(const char* text) {
  static std::wstring path = AppPaths::appDataDir() + L"\\startup.log";
  FILE* f = _wfopen(path.c_str(), L"ab");
  if (!f) return;
  SYSTEMTIME st;
  GetLocalTime(&st);
  fprintf(f, "[%02d:%02d:%02d.%03d] %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
          text);
  fclose(f);
}

double nowSeconds() {
  LARGE_INTEGER f, c;
  QueryPerformanceFrequency(&f);
  QueryPerformanceCounter(&c);
  return (double)c.QuadPart / (double)f.QuadPart;
}

std::wstring timestampName() {
  SYSTEMTIME st;
  GetLocalTime(&st);
  wchar_t buf[64];
  swprintf(buf, 64, L"LVS_%04d%02d%02d_%02d%02d%02d.wav", st.wYear, st.wMonth, st.wDay, st.wHour,
           st.wMinute, st.wSecond);
  return buf;
}

std::wstring widen(const std::string& s) {
  if (s.empty()) return {};
  int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring out((size_t)len, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), len);
  return out;
}

std::string narrow(const std::wstring& w) { return json::Value::toUtf8(w); }
} // namespace

App* App::instance() { return g_app; }

LRESULT CALLBACK App::WndProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_NCCREATE) {
    auto* cs = (CREATESTRUCTW*)lp;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
    if (auto* a = (App*)cs->lpCreateParams) a->attachWindow(hwnd);
  }
  App* app = (App*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
  if (app) return app->handleMessage(msg, wp, lp);
  return DefWindowProcW(hwnd, msg, wp, lp);
}

bool App::init(HINSTANCE hinst) {
  hinst_ = hinst;
  g_app = this;
  logLine("init: begin");

  settings_ = SettingsStore::load();
  logLine("init: settings loaded");
  presets_.init();
  logLine("init: presets loaded");

  WNDCLASSEXW wc = {sizeof(wc)};
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = &App::WndProcStatic;
  wc.hInstance = hinst;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = nullptr;
  wc.lpszClassName = L"SoloWorshiperWindow";
  wc.hIcon = (HICON)LoadImageW(hinst, MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0,
                               LR_DEFAULTSIZE | LR_SHARED);
  if (!wc.hIcon) wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
  wc.hIconSm = (HICON)LoadImageW(hinst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                 GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                                 LR_SHARED);
  if (!RegisterClassExW(&wc)) {
    logLine("init: RegisterClassExW failed");
    return false;
  }
  logLine("init: window class registered");

  const int screenW = GetSystemMetrics(SM_CXSCREEN);
  const int screenH = GetSystemMetrics(SM_CYSCREEN);
  int winW = std::min(screenW - 80, 1720);
  int winH = std::min(screenH - 80, 1000);
  if (winW < 1280) winW = std::min(screenW, 1280);
  if (winH < 720) winH = std::min(screenH, 720);

  hwnd_ = CreateWindowExW(0, wc.lpszClassName, L"SOLO WORSHIPER", WS_OVERLAPPEDWINDOW,
                          (screenW - winW) / 2, (screenH - winH) / 2, winW, winH, nullptr, nullptr,
                          hinst, this);
  if (!hwnd_) {
    char buf[96];
    snprintf(buf, sizeof(buf), "init: CreateWindowExW failed (err %lu)", (unsigned long)GetLastError());
    logLine(buf);
    return false;
  }
  logLine("init: window created");

  // Dark title bar where the OS supports it.
  BOOL dark = TRUE;
  DwmSetWindowAttribute(hwnd_, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));

  if (!gfx_.create(hwnd_)) {
    logLine("init: Direct2D render target creation failed");
    return false;
  }
  logLine("init: Direct2D ready");
  RECT rc;
  GetClientRect(hwnd_, &rc);
  const UINT dpi = GetDpiForWindow(hwnd_);
  gfx_.resize(rc.right - rc.left, rc.bottom - rc.top, dpi ? (float)dpi : 96.0f);

  // --- Audio -------------------------------------------------------------
  EngineConfig cfg = settings_.engine;
  cfg.useAsio = cfg.asioClsid.empty() ? false : cfg.useAsio;
  if (cfg.asioClsid.empty() && cfg.useAsio) {
    cfg.asioClsid = Engine::SuggestAsioClsid();
  }
  if (cfg.useAsio && cfg.asioClsid.empty()) cfg.useAsio = false;
  if (!cfg.useAsio && cfg.renderId.empty()) cfg.renderId = Engine::SuggestRenderEndpoint();
  if (!cfg.useAsio && cfg.captureId.empty()) cfg.captureId = Engine::SuggestCaptureEndpoint();
  if (cfg.loopbackId.empty()) cfg.loopbackId = Engine::SuggestLoopbackEndpoint();
  engine_.setConfig(cfg);

  // Restore the last preset before audio starts so the first sample is right.
  int presetIndex = presets_.findByName(settings_.lastPreset);
  if (presetIndex < 0) presetIndex = 0;
  if (presetIndex < presets_.count()) presets_.apply(presetIndex, engine_.params());
  category_ = settings_.presetCategory;
  slotA_.resize(kParamCount);
  slotB_.resize(kParamCount);
  // Alignment is automatic; the trim is only a manual override and it used to
  // carry over a value dialled in against the old guess, so it is cleared once.
  if (!settings_.syncTrimMigrated) {
    settings_.recSyncMs = 0.0f;
    settings_.syncTrimMigrated = true;
    SettingsStore::save(settings_);
  }
  // Restored after the preset so a dialled-in record alignment and the karaoke
  // switch survive a restart.
  engine_.params().set(P::RecSync, settings_.recSyncMs);
  engine_.params().set(P::KaraokeOn, settings_.karaoke ? 1.0f : 0.0f);
  playbackRate_ = settings_.playbackRate > 0.0 ? settings_.playbackRate : 1.0;
  youtube_.setPlaybackRate(playbackRate_);
  engine_.params().capture(slotA_.data());
  engine_.params().capture(slotB_.data());

  refreshDevices(true);

  // Start the embed before the audio engine: the browser process takes a few
  // seconds to spin up, and doing it first is what keeps the player from sitting
  // black while everything else is already running.
  const std::string startupId = YouTubeHost::extractVideoId(settings_.lastUrl);
  if (!startupId.empty()) youtube_.loadVideo(startupId, false);
  youtube_.create(hwnd_);

  startEngine();
  logLine("init: engine start attempted");
  // A missing or broken WebView2 used to leave a black panel with no clue; the
  // reason is logged and drawn in the player area instead.
  if (!youtube_.lastError().empty()) logLine(("webview: " + youtube_.lastError()).c_str());
  else logLine("webview: embedded player creating");
  if (settings_.fullscreenVideo) videoFullscreen_ = true;

  logLine("init: complete");
  SetTimer(hwnd_, 1, 16, nullptr);
  ShowWindow(hwnd_, SW_SHOW);
  UpdateWindow(hwnd_);
  startTime_ = nowSeconds();
  lastTick_ = startTime_;
  return true;
}

int App::run() {
  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  // Never leave the Windows player ducked behind our back.
  if (params().getBool(P::KaraokeOn)) applyPlayerVolume(1.0f, false, nullptr);
  return 0;
}

void App::startEngine() {
  std::wstring err;
  if (engine_.Start(settings_.engine, &err)) {
    settings_.engine = engine_.config();
    // Persist the measured song-path cache so the next launch is silent.
    SettingsStore::save(settings_);
    errorText_.clear();
    const EngineStats st = engine_.stats();
    char msg[256];
    snprintf(msg, sizeof(msg),
             "engine: started %d Hz / %d frames, %s, in %.2f ms out %.2f ms", st.sampleRate,
             st.bufferFrames, st.asio ? "ASIO" : "WASAPI", st.asioInputLatencyMs,
             st.asioOutputLatencyMs);
    logLine(msg);
    if (engine_.songLatencyMeasured()) {
      char lat[160];
      snprintf(lat, sizeof(lat),
               "engine: song path %.1f ms (cached/measured for this endpoint), record offset "
               "%.1f ms",
               engine_.measuredSongLatencyMs(), engine_.recordOffsetMs());
      logLine(lat);
    } else {
      logLine("engine: song path not measured - using the loopback client's report");
    }
    logLine(("engine: karaoke capture " +
             std::string(settings_.engine.loopbackFollowDefault ? "follows the Windows default: "
                                                               : "pinned to: ") +
             narrow(engine_.config().loopbackId))
                .c_str());
  } else {
    errorText_ = err;
    errorUntil_ = nowSeconds() + 12.0;
    logLine(("engine: start failed - " + std::string(err.begin(), err.end())).c_str());
  }
}

void App::restartEngine() {
  engine_.Stop();
  Sleep(120);
  startEngine();
  settings_.engine = engine_.config();
  SettingsStore::save(settings_);
}

void App::refreshDevices(bool force) {
  const double now = nowSeconds();
  // A short window keeps a clicked-open dropdown from re-enumerating while it
  // is already populated from the same click.
  if (!force && devicesLoadedAt_ >= 0.0 && now - devicesLoadedAt_ < 3.0) return;
  captureDevices_ = au::List(eCapture);
  renderDevices_ = au::List(eRender);
  defaultRenderId_ = au::DefaultId(eRender);
  devicesLoadedAt_ = now;
}

void App::applyDeviceConfig() {
  EngineConfig cfg = settings_.engine;
  if (cfg.useAsio && cfg.asioClsid.empty()) cfg.asioClsid = Engine::SuggestAsioClsid();
  settings_.engine = cfg;
  engine_.Stop();
  Sleep(150);
  std::wstring err;
  if (!engine_.Start(cfg, &err)) {
    errorText_ = err;
    errorUntil_ = nowSeconds() + 15.0;
    // Guarantee the app is not left silent: fall back to WASAPI.
    EngineConfig fallback = cfg;
    fallback.useAsio = false;
    if (fallback.renderId.empty()) fallback.renderId = Engine::SuggestRenderEndpoint();
    if (fallback.captureId.empty()) fallback.captureId = Engine::SuggestCaptureEndpoint();
    settings_.engine = fallback;
    engine_.Start(fallback, &err);
  } else {
    errorText_.clear();
  }
  settings_.engine = engine_.config();
  SettingsStore::save(settings_);
  // Opening a stream can change which endpoints Windows reports as active.
  refreshDevices(true);

  // The render stream can start while the capture stream quietly fails (a
  // disabled or unplugged microphone), which used to look like a working app
  // with a dead mic. Give the capture thread a moment, then say so.
  if (settings_.engine.captureEnabled && !settings_.engine.useAsio) {
    Sleep(300);
    if (!engine_.stats().captureOpen) {
      std::wstring name = L"the selected input";
      for (const auto& e : captureDevices_)
        if (e.id == settings_.engine.captureId) {
          name = e.name;
          if (!(e.state & DEVICE_STATE_ACTIVE))
            name += (e.state & DEVICE_STATE_DISABLED) ? L" (disabled)" : L" (not plugged in)";
        }
      errorText_ = L"Input could not be opened: " + name +
                   L". Choose another input, or enable it in Windows Sound settings.";
      errorUntil_ = nowSeconds() + 15.0;
      logLine("engine: selected capture endpoint could not be opened");
    }
  }
}

void App::loadPreset(int index) {
  const Preset* p = presets_.at(index);
  if (!p) return;
  presets_.apply(index, engine_.params());
  settings_.lastPreset = p->name;
  SettingsStore::save(settings_);
}

void App::selectCategory(int cat) {
  category_ = std::clamp(cat, 0, 2);
  settings_.presetCategory = category_;
  for (int i = 0; i < presets_.count(); ++i) {
    const Preset* p = presets_.at(i);
    if (!p) continue;
    const bool match = (category_ == 0 && p->category == "VOCAL") ||
                       (category_ == 1 && p->category == "REVERB") ||
                       (category_ == 2 && p->category == "EQ");
    if (match) {
      loadPreset(i);
      break;
    }
  }
}

void App::beginSavePreset() {
  overlaySave_ = true;
  editField_ = A_NAME_FIELD;
  const Preset* p = presets_.at(presets_.findByName(settings_.lastPreset));
  editText_ = widen(p ? p->name : "My Vocal");
  caret_ = editText_.size();
  needsRelayout_ = true;
}

void App::confirmSavePreset() {
  if (editText_.empty()) return;
  const char* cat = category_ == 1 ? "REVERB" : (category_ == 2 ? "EQ" : "VOCAL");
  const int idx = presets_.saveAs(narrow(editText_), cat, engine_.params());
  if (idx >= 0) {
    settings_.lastPreset = narrow(editText_);
    SettingsStore::save(settings_);
    status_ = L"Preset saved: " + editText_;
  }
  overlaySave_ = false;
  editField_ = A_NONE;
}

void App::toggleAB() {
  if (usingA_) {
    engine_.params().capture(slotA_.data());
    engine_.params().apply(slotB_.data());
    usingA_ = false;
  } else {
    engine_.params().capture(slotB_.data());
    engine_.params().apply(slotA_.data());
    usingA_ = true;
  }
}

void App::toggleRecording() {
  if (!recording_) {
    std::wstring folder = settings_.recordingFolder.empty() ? AppPaths::recordingsDir()
                                                            : settings_.recordingFolder;
    CreateDirectoryW(folder.c_str(), nullptr);
    const std::wstring path = folder + L"\\" + timestampName();
    std::wstring err;
    if (engine_.startRecording(path, settings_.recordBits, settings_.recordVocal, &err)) {
      recording_ = true;
      recordStart_ = nowSeconds();
      status_ = L"Recording: " + path;
    } else {
      errorText_ = err;
      errorUntil_ = nowSeconds() + 10.0;
    }
  } else {
    engine_.stopRecording();
    recording_ = false;
    status_ = L"Recording stopped.";
  }
}

void App::openRecordFolder() {
  std::wstring folder = settings_.recordingFolder.empty() ? AppPaths::recordingsDir()
                                                          : settings_.recordingFolder;
  ShellExecuteW(hwnd_, L"open", folder.c_str(), nullptr, nullptr, SW_SHOW);
}

void App::openSettings() {
  refreshDevices(true);
  overlaySettings_ = true;
  showPopup_ = false;
  popup_.open = false;
  needsRelayout_ = true;
}

void App::closeOverlay() {
  overlaySettings_ = false;
  overlaySave_ = false;
  overlayHelp_ = false;
  overlayQueue_ = false;
  showPopup_ = false;
  popup_.open = false;
  editField_ = A_NONE;
  needsRelayout_ = true;
}

void App::syncWindowToOverlay() {
  const bool wantsVideo =
      !overlaySettings_ && !overlaySave_ && !overlayQueue_ && youtube_.loaded();
  youtube_.setVisible(wantsVideo);
  updateYouTubeBounds();
}

void App::updateYouTubeBounds() {
  if (!youtube_.available()) return;
  const float s = gfx_.scale();
  RECT rc;
  if (videoFullscreen_) {
    GetClientRect(hwnd_, &rc);
  } else {
    rc.left = (LONG)(rcVideo_.left * s);
    rc.top = (LONG)(rcVideo_.top * s);
    rc.right = (LONG)(rcVideo_.right * s);
    rc.bottom = (LONG)(rcVideo_.bottom * s);
  }
  youtube_.setBounds(rc);
}

void App::runAction(int action, int value) {
  using ui::T;
  switch (action) {
  case A_PRESET_PREV:
  case A_PRESET_NEXT: {
    int idx = presets_.findByName(settings_.lastPreset);
    const int dir = action == A_PRESET_NEXT ? 1 : -1;
    for (int guard = 0; guard < presets_.count(); ++guard) {
      idx = (idx + dir + presets_.count()) % presets_.count();
      const Preset* p = presets_.at(idx);
      if (!p) break;
      const bool match = (category_ == 0 && p->category == "VOCAL") ||
                         (category_ == 1 && p->category == "REVERB") ||
                         (category_ == 2 && p->category == "EQ");
      if (match) {
        loadPreset(idx);
        break;
      }
    }
    break;
  }
  case A_SAVE_PRESET: beginSavePreset(); break;
  case A_AB: toggleAB(); break;
  case A_SETTINGS: openSettings(); break;
  case A_CLOSE_OVERLAY: closeOverlay(); break;
  case A_CAT_VOCAL: selectCategory(0); break;
  case A_CAT_REVERB: selectCategory(1); break;
  case A_CAT_EQ: selectCategory(2); break;
  case A_YT_PLAY:
    if (youtube_.playing())
      youtube_.pause();
    else
      youtube_.play();
    break;
  case A_YT_BACK: youtube_.seekBy(-10.0); break;
  case A_YT_FWD: youtube_.seekBy(10.0); break;
  case A_TEMPO_DOWN: stepPlaybackRate(-1); break;
  case A_TEMPO_UP: stepPlaybackRate(1); break;
  case A_TEMPO_RESET: stepPlaybackRate(0); break;
  case A_KARAOKE: {
    const bool on = !params().getBool(P::KaraokeOn);
    params().set(P::KaraokeOn, on ? 1.0f : 0.0f);
    settings_.karaoke = on;
    if (!on) {
      status_ = L"Karaoke off: the Windows player is back in control.";
      logLine("karaoke: vocal cut off");
    } else if (musicBusPossible()) {
      status_ = L"Karaoke on: the console plays the song with the vocal cut.";
      logLine("karaoke: vocal cut on, song carried by the console");
    } else {
      // Same WASAPI endpoint in and out: monitoring the cut there would feed our
      // own output back into the capture, so only the recording gets it.
      status_ = L"Karaoke on: vocal cut in the recording. Switch the audio driver to ASIO (or route the song to another device) to hear it live.";
      logLine("karaoke: vocal cut on (recording only, shared endpoint)");
    }
    SettingsStore::save(settings_);
    break;
  }
  case A_YT_FULLSCREEN:
    videoFullscreen_ = !videoFullscreen_;
    settings_.fullscreenVideo = videoFullscreen_;
    syncWindowToOverlay();
    break;
  case A_YT_LOAD: {
    // The field shows the last link even before it is focused, so LOAD must use
    // it too instead of silently doing nothing.
    const std::string typed = editText_.empty() ? settings_.lastUrl : narrow(editText_);
    const std::string listId = YouTubeHost::extractPlaylistId(typed);
    const std::string id = YouTubeHost::extractVideoId(typed);
    if (!listId.empty()) {
      beginPlaylistLoad(listId, id);
      settings_.lastUrl = typed;
      SettingsStore::save(settings_);
    } else if (!id.empty()) {
      queue_.clear();
      youtube_.loadVideo(id, true);
      logLine(("ui: loading video " + id).c_str());
      settings_.lastUrl = id;
      SettingsStore::save(settings_);
      syncWindowToOverlay();
      updateYouTubeBounds();
      status_ = L"Loading YouTube video " + widen(id);
    } else {
      errorText_ = L"Paste a YouTube video or playlist link.";
      errorUntil_ = nowSeconds() + 8.0;
    }
    editField_ = A_NONE;
    break;
  }
  case A_REC_TOGGLE: toggleRecording(); break;
  case A_REC_FOLDER: openRecordFolder(); break;
  case A_REC_FORMAT:
    settings_.recordBits = settings_.recordBits == 24 ? 32 : 24;
    SettingsStore::save(settings_);
    break;
  case A_REC_SOURCE:
    settings_.recordVocal = !settings_.recordVocal;
    SettingsStore::save(settings_);
    break;
  case A_DEVICE_APPLY: applyDeviceConfig(); break;
  case A_DEVICE_EXCLUSIVE:
    settings_.engine.exclusive = !settings_.engine.exclusive;
    break;
  case A_CAPTURE_ENABLE:
    settings_.engine.captureEnabled = !settings_.engine.captureEnabled;
    break;
  case A_DEVICE_ASIO_PANEL: {
    if (!settings_.engine.useAsio) break;
    // Open the driver's own control panel by briefly instantiating it.
    EngineStats st = engine_.stats();
    (void)st;
    status_ = L"Use the Focusrite control panel from the Windows tray to change buffer size.";
    break;
  }
  case A_DEVICE_MEASURE: {
    // Measure the path the song actually takes (endpoint -> loopback) and cache
    // it, so the alignment uses a real number instead of a guess.
    std::wstring note;
    const std::wstring target = settings_.engine.loopbackId.empty()
                                    ? au::DefaultId(eRender)
                                    : settings_.engine.loopbackId;
    const double ms = engine_.measureOutputLatencyMs(target, &note);
    if (ms >= 0) {
      settings_.engine.latencyCacheId = target;
      settings_.engine.latencyCacheMs = ms;
      SettingsStore::save(settings_);
      wchar_t buf[160];
      swprintf(buf, 160, L"Song path measured %.1f ms - APPLY & RESTART AUDIO to use it.", ms);
      status_ = buf;
    } else {
      status_ = L"Measurement unavailable: " + note;
    }
    break;
  }
  case A_SAVE_CONFIRM: confirmSavePreset(); break;
  case A_SAVE_CANCEL:
    overlaySave_ = false;
    editField_ = A_NONE;
    break;
  case A_DELETE_PRESET: {
    const int idx = presets_.findByName(settings_.lastPreset);
    if (presets_.remove(idx)) {
      status_ = L"Preset deleted.";
      settings_.lastPreset.clear();
      SettingsStore::save(settings_);
    } else {
      errorText_ = L"Factory presets cannot be deleted. Save your own first.";
      errorUntil_ = nowSeconds() + 6.0;
    }
    break;
  }
  case A_URL_FIELD:
    editField_ = A_URL_FIELD;
    editText_ = widen(settings_.lastUrl);
    caret_ = editText_.size();
    break;
  case A_PASTE: {
    if (editField_ == A_NONE) editField_ = A_URL_FIELD;
    pasteIntoField();
    break;
  }
  case A_SELECT_ASIO:
  case A_SELECT_CAPTURE:
  case A_SELECT_RENDER:
  case A_SELECT_LOOPBACK:
  case A_SELECT_RATE:
  case A_SELECT_BUFFER:
  case A_SELECT_INPUT_CH:
  case A_SELECT_OUT_L:
  case A_SELECT_OUT_R:
  case A_SELECT_KARAOKE_CUT:
  case A_REV_TYPE:
  case A_EQ_PRESET:
  case A_REV_PRESET:
    buildSelectPopup(action);
    break;
  case A_QUEUE_TOGGLE:
    overlayQueue_ = !overlayQueue_;
    queueScroll_ = 0.0f;
    syncWindowToOverlay();
    break;
  case A_QUEUE_CLOSE:
    overlayQueue_ = false;
    syncWindowToOverlay();
    break;
  case A_QUEUE_PREV: queueStep(-1); break;
  case A_QUEUE_NEXT: queueStep(1); break;
  case A_MUSIC_ROUTE: {
    if (!musicBusPossible()) {
      // Muting the player is what makes the console's copy exclusive, and a mute
      // also silences the loopback capture. Say so instead of pretending.
      errorText_ = L"Same device: bus copy would double. Route karaoke elsewhere (Settings).";
      errorUntil_ = nowSeconds() + 9.0;
    } else {
      settings_.musicThroughBus = !settings_.musicThroughBus;
      SettingsStore::save(settings_);
      logLine(settings_.musicThroughBus ? "music: routed through the console bus (EQ/pan live)"
                                        : "music: routed from the Windows player session");
    }
    break;
  }
  default:
    if (action >= kParamActionBase) {
      const int param = action - kParamActionBase;
      (void)value;
      (void)param;
    }
    break;
  }
  needsRelayout_ = true;
}

void App::buildSelectPopup(int owner) {
  popup_ = {};
  popup_.open = true;
  popup_.owner = owner;
  popup_.anchor = pendingAnchor_;
  showPopup_ = true;

  auto add = [&](const std::string& name, int id) {
    popup_.items.push_back(name);
    popup_.ids.push_back(id);
  };

  // Every installed endpoint is listed. Ones Windows cannot open stay
  // selectable but are labelled, so a choice is never silently dead.
  auto deviceLabel = [](const au::Endpoint& e) {
    std::string s = json::Value::toUtf8(e.name);
    if (e.state & DEVICE_STATE_ACTIVE) return s;
    s += (e.state & DEVICE_STATE_DISABLED) ? "  (disabled)" : "  (not plugged in)";
    return s;
  };
  auto anyActive = [](const std::vector<au::Endpoint>& v) {
    for (auto& e : v)
      if (e.state & DEVICE_STATE_ACTIVE) return true;
    return false;
  };
  // Ids are explicit device indices: list position must never depend on how
  // many fixed items were added before the devices.
  auto addDevices = [&](const std::vector<au::Endpoint>& devs, const char* noneText,
                        const char* inactiveText) {
    if (devs.empty()) {
      add(noneText, -1);
      return;
    }
    if (!anyActive(devs)) add(inactiveText, -1);
    for (size_t i = 0; i < devs.size(); ++i) add(deviceLabel(devs[i]), 10000 + (int)i);
  };

  if (owner == A_SELECT_ASIO) {
    add("WASAPI (Windows audio)", -1);
    auto drivers = Engine::ListAsioDrivers();
    for (size_t i = 0; i < drivers.size(); ++i) add(narrow(drivers[i].first), 10000 + (int)i);
    // Selecting an ASIO entry is resolved in selectItem via the item index.
  } else if (owner == A_SELECT_CAPTURE) {
    refreshDevices();
    addDevices(captureDevices_, "(no capture devices found)",
               "(no active input - enable one in Windows Sound settings)");
  } else if (owner == A_SELECT_RENDER) {
    refreshDevices();
    addDevices(renderDevices_, "(no output devices found)",
               "(no active output - enable one in Windows Sound settings)");
  } else if (owner == A_SELECT_LOOPBACK) {
    add("System default (follow Windows)", -2);
    add("None", -1);
    refreshDevices();
    addDevices(renderDevices_, "(no output devices found)",
               "(no active output - enable one in Windows Sound settings)");
  } else if (owner == A_SELECT_RATE) {
    for (int r : {44100, 48000, 88200, 96000}) add(std::to_string(r) + " Hz", r);
  } else if (owner == A_SELECT_KARAOKE_CUT) {
    for (int pct : {50, 70, 80, 85, 90, 95, 100}) add(std::to_string(pct) + "%", pct);
  } else if (owner == A_SELECT_BUFFER) {
    for (int b : {64, 128, 256, 512, 1024}) add(std::to_string(b), b);
  } else if (owner == A_SELECT_INPUT_CH) {
    const int chans = std::max(1, settings_.engine.captureChannels);
    for (int i = 0; i < chans; ++i) add("Input " + std::to_string(i + 1), i);
  } else if (owner == A_SELECT_OUT_L || owner == A_SELECT_OUT_R) {
    const int chans = std::max(2, settings_.engine.deviceChannels);
    for (int i = 0; i < chans; ++i) add("Output " + std::to_string(i + 1), i);
  } else if (owner == A_REV_TYPE) {
    for (int i = 0; i < 8; ++i) add(dsp::Reverb::typeName(i), i);
  } else if (owner == A_EQ_PRESET) {
    for (int i = 0; i < presets_.count(); ++i) {
      const Preset* pr = presets_.at(i);
      if (pr && pr->category == "EQ") add(pr->name, i);
    }
  } else if (owner == A_REV_PRESET) {
    for (int i = 0; i < presets_.count(); ++i) {
      const Preset* pr = presets_.at(i);
      if (pr && pr->category == "REVERB") add(pr->name, i);
    }
  }
}

void App::selectItem(int id) {
  const int owner = popup_.owner;
  showPopup_ = false;
  popup_.open = false;
  if (owner == A_SELECT_ASIO) {
    if (id < 0) {
      settings_.engine.useAsio = false;
    } else {
      auto drivers = Engine::ListAsioDrivers();
      const int idx = id - 10000;
      if (idx >= 0 && idx < (int)drivers.size()) {
        settings_.engine.useAsio = true;
        settings_.engine.asioClsid = drivers[idx].second;
      }
    }
  } else if (owner == A_SELECT_CAPTURE) {
    refreshDevices();
    const int idx = id - 10000;
    if (idx >= 0 && idx < (int)captureDevices_.size()) {
      const au::Endpoint& e = captureDevices_[idx];
      settings_.engine.captureId = e.id;
      if (e.channels > 0) settings_.engine.captureChannels = (int)e.channels;
      settings_.engine.captureChannel =
          std::min(settings_.engine.captureChannel, std::max(0, (int)e.channels - 1));
    }
  } else if (owner == A_SELECT_RENDER) {
    refreshDevices();
    const int idx = id - 10000;
    if (idx >= 0 && idx < (int)renderDevices_.size()) {
      const au::Endpoint& e = renderDevices_[idx];
      settings_.engine.renderId = e.id;
      if (e.channels > 0) settings_.engine.deviceChannels = (int)e.channels;
    }
  } else if (owner == A_SELECT_LOOPBACK) {
    if (id == -2) {
      settings_.engine.loopbackFollowDefault = true;
      settings_.engine.loopbackId = au::DefaultId(eRender);
    } else if (id < 0) {
      settings_.engine.loopbackFollowDefault = false;
      settings_.engine.loopbackId.clear();
    } else {
      refreshDevices();
      const int idx = id - 10000;
      if (idx >= 0 && idx < (int)renderDevices_.size()) {
        settings_.engine.loopbackFollowDefault = false;
        settings_.engine.loopbackId = renderDevices_[idx].id;
      }
    }
  } else if (owner == A_SELECT_RATE) {
    settings_.engine.sampleRate = id;
  } else if (owner == A_SELECT_KARAOKE_CUT) {
    params().set(P::KaraokeDepth, (float)id / 100.0f);
    logLine(("karaoke: vocal cut " + std::to_string(id) + "%").c_str());
  } else if (owner == A_SELECT_BUFFER) {
    settings_.engine.bufferFrames = id;
  } else if (owner == A_SELECT_INPUT_CH) {
    settings_.engine.captureChannel = id;
    SettingsStore::save(settings_);
  } else if (owner == A_SELECT_OUT_L) {
    settings_.engine.outLeft = id;
    // Routing used to live only in memory, so the menu appeared to do nothing
    // after a restart.
    SettingsStore::save(settings_);
  } else if (owner == A_SELECT_OUT_R) {
    settings_.engine.outRight = id;
    SettingsStore::save(settings_);
  } else if (owner == A_REV_TYPE) {
    params().set(P::RevType, (float)id);
    return;
  } else if (owner == A_EQ_PRESET) {
    // EQ presets are sparse: they only carry the four bands, the high pass and
    // the EQ switch. Applying one must not disturb the vocal chain or the
    // current preset name, so the parameters go in directly.
    const Preset* pr = presets_.at(id);
    if (pr) {
      presets_.apply(id, engine_.params());
      status_ = L"EQ preset applied: " + widen(pr->name);
      logLine(("ui: eq preset applied - " + pr->name).c_str());
    }
    return;
  } else if (owner == A_REV_PRESET) {
    // Reverb presets are sparse too: only the reverb block and the reverb
    // switch move, so a saved reverb is recallable on its own.
    const Preset* pr = presets_.at(id);
    if (pr) {
      presets_.apply(id, engine_.params());
      status_ = L"Reverb preset applied: " + widen(pr->name);
      logLine(("ui: reverb preset applied - " + pr->name).c_str());
    }
    return;
  }
  SettingsStore::save(settings_);
}

bool App::musicBusPossible() const {
  const EngineConfig& e = settings_.engine;
  // ASIO renders through the driver's own stream, so the Windows endpoint that
  // carries the player is a different mix: the bus copy does not double and the
  // console output never comes back through the loopback capture.
  if (engine_.stats().asio) return true;
  return !e.loopbackId.empty() && !e.renderId.empty() && e.loopbackId != e.renderId;
}

void App::stepPlaybackRate(int dir) {
  // Only the rates the official embed actually honours.
  static const double kRates[] = {0.5, 0.75, 1.0, 1.25, 1.5};
  const int count = (int)(sizeof(kRates) / sizeof(kRates[0]));
  int idx = 2;
  for (int i = 0; i < count; ++i) {
    if (std::fabs(kRates[i] - playbackRate_) < 0.001) idx = i;
  }
  if (dir == 0) {
    idx = 2;
  } else {
    idx = std::clamp(idx + dir, 0, count - 1);
  }
  playbackRate_ = kRates[idx];
  youtube_.setPlaybackRate(playbackRate_);
  settings_.playbackRate = playbackRate_;
  SettingsStore::save(settings_);
  char buf[64];
  snprintf(buf, sizeof(buf), "player: tempo %.2fx", playbackRate_);
  logLine(buf);
  wchar_t wbuf[64];
  swprintf(wbuf, 64, L"Song tempo %.2fx", playbackRate_);
  status_ = wbuf;
}

void App::setMusicDirect(bool on, const std::wstring* note) {
  if (on == sessionDirect_) return;
  sessionDirect_ = on;
  engine_.setMusicDirect(on);
  logLine(on ? "music: fader drives the Windows player session"
             : "music: monitor bus carries the music (no player session)");
  if (note && !note->empty()) logLine(json::Value::toUtf8(*note).c_str());
}

// ---------------------------------------------------------------------------
// Queue: one ordered list for YouTube playlists and, later, local files.
// ---------------------------------------------------------------------------
void App::beginPlaylistLoad(const std::string& listId, const std::string& startVideoId) {
  if (playlistJob_.running()) {
    status_ = L"A playlist is already loading.";
    return;
  }
  pendingPlaylistStart_ = startVideoId;
  queue_.clear();
  playlistJob_.start(listId);
  logLine(("queue: loading playlist " + listId).c_str());
  status_ = L"Reading the playlist from YouTube...";
  needsRelayout_ = true;
}

void App::playQueueIndex(int index) {
  const QueueItem* item = queue_.at(index);
  if (!item) return;
  queue_.jump(index);
  overlayQueue_ = false;
  if (item->kind == QueueItem::Kind::YouTube && !item->videoId.empty()) {
    youtube_.loadVideo(item->videoId, true);
    syncWindowToOverlay();
    updateYouTubeBounds();
    char buf[160];
    snprintf(buf, sizeof(buf), "queue: playing %d/%d %s", index + 1, queue_.size(),
             item->videoId.c_str());
    logLine(buf);
    status_ = L"Playing " + widen(item->title);
  } else if (item->kind == QueueItem::Kind::LocalFile) {
    status_ = L"Local file playback arrives with the file library: " + item->path;
  }
  needsRelayout_ = true;
}

void App::queueStep(int delta) {
  if (queue_.empty()) {
    // Listing unavailable: the embed's own playlist drives the order.
    if (youtube_.hasPlaylist()) {
      if (delta > 0)
        youtube_.nextVideo();
      else
        youtube_.previousVideo();
    } else {
      status_ = L"No playlist loaded.";
    }
    return;
  }
  const int target = queue_.current() + delta;
  if (target < 0) {
    status_ = L"Start of the playlist.";
    return;
  }
  if (target >= queue_.size()) {
    status_ = L"End of the playlist.";
    return;
  }
  playQueueIndex(target);
}

void App::tick() {
  const double now = nowSeconds();
  const double dt = std::min(0.25, now - lastTick_);
  lastTick_ = now;

  EngineStats st = engine_.stats();
  const float meterScale = 15.0f; // decay per second in normalised units
  auto toNorm = [](float peak) {
    if (peak <= 0.0001f) return 0.0f;
    const float db = 20.0f * std::log10(peak);
    return std::clamp((db + 60.0f) / 66.0f, 0.0f, 1.0f);
  };
  const float l = toNorm(st.outputPeakL);
  const float rr = toNorm(st.outputPeakR);
  meterL_ = std::max(l, meterL_ - (float)dt * meterScale);
  meterR_ = std::max(rr, meterR_ - (float)dt * meterScale);
  if (meterL_ >= peakL_) {
    peakL_ = meterL_;
    peakLTime_ = now;
  } else if (now - peakLTime_ > 1.6) {
    peakL_ = std::max(meterL_, peakL_ - (float)dt * 0.45f);
  }
  if (meterR_ >= peakR_) {
    peakR_ = meterR_;
    peakRTime_ = now;
  } else if (now - peakRTime_ > 1.6) {
    peakR_ = std::max(meterR_, peakR_ - (float)dt * 0.45f);
  }
  micMeter_ = std::max(toNorm(st.inputPeak), micMeter_ - (float)dt * meterScale);
  musMeter_ = std::max(toNorm(st.musicPeak), musMeter_ - (float)dt * meterScale);

  static double lastPoll = 0;
  if (now - lastPoll > 0.5) {
    lastPoll = now;
    youtube_.poll();
    if (youtube_.title().size()) settings_.videoTitle = widen(youtube_.title());
  }

  // Playlist listing finished on its worker thread.
  {
    PlaylistResult pr;
    if (playlistJob_.take(&pr)) {
      if (pr.ok) {
        std::vector<QueueItem> items;
        int start = 0;
        for (int i = 0; i < (int)pr.entries.size(); ++i) {
          QueueItem it;
          it.kind = QueueItem::Kind::YouTube;
          it.videoId = pr.entries[(size_t)i].videoId;
          it.title = pr.entries[(size_t)i].title;
          it.duration = pr.entries[(size_t)i].duration;
          if (!pendingPlaylistStart_.empty() && it.videoId == pendingPlaylistStart_) start = i;
          items.push_back(std::move(it));
        }
        queue_.setItems(std::move(items), start);
        queue_.setName(pr.name);
        queueScroll_ = 0.0f;
        char buf[160];
        snprintf(buf, sizeof(buf), "queue: playlist ready with %d item(s)", queue_.size());
        logLine(buf);
        playQueueIndex(queue_.current());
        overlayQueue_ = true;
        syncWindowToOverlay();
        status_ = L"Playlist ready: " + std::to_wstring(queue_.size()) + L" songs";
      } else {
        // The listing failed: play the official embed's own playlist so the show
        // still goes on, with the player driving previous/next.
        youtube_.loadPlaylist(pr.listId, true);
        queue_.clear();
        syncWindowToOverlay();
        updateYouTubeBounds();
        errorText_ = pr.error;
        errorUntil_ = nowSeconds() + 9.0;
        logLine("queue: playlist listing failed - using the embed playlist");
        logLine(json::Value::toUtf8(pr.error).c_str());
      }
      pendingPlaylistStart_.clear();
    }
  }

  // Auto-advance once when the current video finishes.
  if (youtube_.ended() && !queue_.empty()) {
    const std::string vid = youtube_.videoId();
    if (!vid.empty() && vid != lastEndedId_) {
      lastEndedId_ = vid;
      const int n = queue_.nextIndex();
      if (n >= 0)
        playQueueIndex(n);
      else
        status_ = L"End of the playlist.";
    }
  }

  // MUSIC fader / MUTE -> the embedded player's Windows session. Scaling only
  // the captured copy left the original Windows playback at full level, which
  // is why the fader appeared to do nothing. The decision needs hysteresis: a
  // single missed lookup (track change, session re-created, COM hiccup) must not
  // snap the monitor path between the two modes.
  if (now - lastSessionPush_ > 0.4) {
    lastSessionPush_ = now;
    // Karaoke has to own the song so the vocal cut is what the room hears.
    // Loopback capture only sees what a player actually renders, so a muted
    // session takes the song away from the engine as well. Karaoke therefore
    // ducks the player instead of muting it.
    const bool karaoke = params().getBool(P::KaraokeOn) && youtube_.loaded();
    const bool karaokeBus = karaoke && musicBusPossible();
    const bool busRequested = karaokeBus || (settings_.musicThroughBus && musicBusPossible());
    const bool wantSession = youtube_.loaded() && !busRequested;
    // -54 dB. Measured floor for this machine: a muted session drops the capture
    // to zero, this level keeps it linear, so it is as quiet as it can safely be.
    constexpr float kKaraokeBleed = 0.002f;
    if (!wantSession) {
      sessionHit_ = 0;
      sessionMiss_ = 0;
      std::wstring note;
      if (karaokeBus) {
        // Muting the player empties the capture (measured 0.3000 -> 0.0000), so
        // karaoke ducks it instead: the player keeps rendering and the capture
        // survives (0.3000 -> 0.0006 at this level), while almost nothing of the
        // original is left in the room because the engine scales the captured
        // copy straight back up (make-up 1/kKaraokeBleed).
        // Re-applied every tick, not once: WebView2 opens new audio sessions as
        // it runs and each fresh session starts at full volume, which would be
        // heard at size alongside the console copy.
        const bool ducked = applyPlayerVolume(kKaraokeBleed, false, &note);
        if (!sessionDucked_) {
          sessionDucked_ = true;
          lastSessionPct_ = -1;
          lastSessionMute_ = false;
          logLine(ducked ? "karaoke: windows player ducked to -54 dB, console carries the song"
                         : "karaoke: no player session to duck yet, will keep trying");
        }
        // Log every change in what the duck reaches, so a WebView2 session that
        // opens later does not slip through unnoticed.
        if (note != lastDuckNote_) {
          lastDuckNote_ = note;
          logLine(("karaoke duck: " + json::Value::toUtf8(note)).c_str());
        }
        engine_.setMusicDuck(1.0f / kKaraokeBleed);
      } else {
        engine_.setMusicDuck(1.0f);
        if (sessionDucked_) {
          applyPlayerVolume(1.0f, false, &note);
          sessionDucked_ = false;
        }
        if (lastSessionPct_ >= 0) {
          // Leaving session control: hand the Windows level back to the user.
          applyPlayerVolume(1.0f, false, &note);
          lastSessionPct_ = -1;
          lastSessionMute_ = false;
        }
      }
      setMusicDirect(false, nullptr);
    } else {
      engine_.setMusicDuck(1.0f);
      if (sessionDucked_) {
        // Hand the player back and stop monitoring the console copy in the same
        // frame. Restoring the volume first and switching the monitor a couple
        // of ticks later played the song twice - once straight from Windows and
        // once from the console - which is the blast of "full volume" heard
        // when karaoke is switched off.
        sessionDucked_ = false;
        sessionHit_ = 2;
        sessionMiss_ = 0;
        lastSessionPct_ = -1;
        setMusicDirect(true, nullptr);
        logLine("karaoke off: player restored, console copy stopped");
      }
      const bool anySolo = params().getBool(P::MicSolo) || params().getBool(P::MusicSolo);
      const bool mute =
          params().getBool(P::MusicMute) || (anySolo && !params().getBool(P::MusicSolo));
      const float vol = params().get(P::MusicFader);
      std::wstring note;
      const bool found = applyPlayerVolume(vol, mute, &note);
      const int pct = (int)std::lround(std::clamp(vol, 0.0f, 1.0f) * 100.0f);
      if (found) {
        ++sessionHit_;
        sessionMiss_ = 0;
        if (pct != lastSessionPct_ || mute != lastSessionMute_) {
          // Log the direction of a drag, not every single step of it.
          const int delta = pct > lastSessionPct_ ? pct - lastSessionPct_ : lastSessionPct_ - pct;
          const bool worthLogging = lastSessionPct_ < 0 || mute != lastSessionMute_ || delta >= 5;
          lastSessionPct_ = pct;
          lastSessionMute_ = mute;
          if (worthLogging) {
            char buf[96];
            snprintf(buf, sizeof(buf), "music: windows player volume %d%%%s", pct,
                     mute ? " (muted)" : "");
            logLine(buf);
          }
        }
      } else {
        ++sessionMiss_;
        sessionHit_ = 0;
      }
      // Two hits to take over, six misses (~2.4 s) to give it back.
      if (!sessionDirect_ && sessionHit_ >= 2) {
        setMusicDirect(true, &note);
      } else if (sessionDirect_ && sessionMiss_ >= 6) {
        setMusicDirect(false, &note);
      }
    }
  }

  if (errorText_.size() && now > errorUntil_) errorText_.clear();

  InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::onPointerDown(float x, float y) {
  const Hit* h = hitAt(x, y);
  // Queue rows have their own id space, like popup items.
  if (h && h->id >= kQueueBase) {
    playQueueIndex(h->id - kQueueBase);
    return;
  }
  // Popup list items live in their own id space.
  if (h && h->id >= 9000) {
    const int idx = h->id - 9000;
    if (idx >= 0 && idx < (int)popup_.ids.size()) selectItem(popup_.ids[(size_t)idx]);
    return;
  }
  if (!h) {
    if (popup_.open) {
      showPopup_ = false;
      popup_.open = false;
    }
    return;
  }
  if (popup_.open && h->kind != 6) {
    showPopup_ = false;
    popup_.open = false;
  }
  if (h->id >= kActionBase && h->id < kActionBase + A_COUNT) {
    const int action = h->id - kActionBase;
    pendingAnchor_ = h->rect;
    if (action == A_YT_SEEK) {
      const float f = (x - h->rect.left) / std::max(1.0f, h->rect.right - h->rect.left);
      if (youtube_.duration() > 0.5) youtube_.seekTo(youtube_.duration() * std::clamp(f, 0.0f, 1.0f));
      return;
    }
    runAction(action);
    return;
  }
  // Latching chips (IN / MUTE / SOLO / PHASE / EQ band dots) are switches, not
  // faders. They used to fall through to the drag branch, so a click neither
  // changed the value nor the colour. One click now flips them end to end.
  if (h->kind == 2 && h->id >= 0 && h->id < kParamCount) {
    const ParamInfo& info = kParams[h->id];
    if (info.max - info.min <= 1.001f) {
      const bool on = params().get((P)h->id) > (info.min + info.max) * 0.5f;
      params().set((P)h->id, on ? info.min : info.max);
      return;
    }
  }
  if (h->kind == 3 && h->id < kParamCount) {
    pendingAnchor_ = h->rect;
    if ((P)h->id == P::PreMode) {
      const int next = ((int)std::lround(params().get(P::PreMode)) + 1) % 3;
      params().set(P::PreMode, (float)next);
    } else if ((P)h->id == P::RevType) {
      buildSelectPopup(kActionBase + A_REV_TYPE);
    }
    return;
  }
  if (h->id >= 0 && h->id < kParamCount) {
    if (h->kind == 4) {
      // Horizontal slider: jump straight to the pointer position.
      const float n = (x - h->rect.left) / std::max(1.0f, h->rect.right - h->rect.left);
      params().setNorm((P)h->id, std::clamp(n, 0.0f, 1.0f));
      dragId_ = h->id;
      dragKind_ = 4;
      return;
    }
    dragId_ = h->id;
    dragKind_ = h->kind;
    dragStartY_ = y;
    dragStartX_ = x;
    dragStartNorm_ = params().norm((P)h->id);
  }
}

void App::onPointerMove(float x, float y) {
  if (dragId_ < 0) return;
  const ParamInfo& info = kParams[dragId_];
  if (dragKind_ == 1) {
    // Vertical fader: absolute mapping inside its track.
    const Hit* h = nullptr;
    for (auto& hit : hits_)
      if (hit.id == dragId_ && hit.kind == 1) h = &hit;
    if (!h) return;
    const float top = h->rect.top + 20.0f;
    const float bottom = h->rect.bottom - 24.0f;
    float n = (bottom - y) / std::max(1.0f, bottom - top);
    params().setNorm((P)dragId_, std::clamp(n, 0.0f, 1.0f));
  } else if (dragKind_ == 4) {
    for (auto& hit : hits_)
      if (hit.id == dragId_ && hit.kind == 4) {
        const float n = (x - hit.rect.left) / std::max(1.0f, hit.rect.right - hit.rect.left);
        params().setNorm((P)dragId_, std::clamp(n, 0.0f, 1.0f));
      }
  } else {
    const float sens = (GetKeyState(VK_SHIFT) & 0x8000) ? 0.0012f : 0.0042f;
    const float n = std::clamp(dragStartNorm_ + (dragStartY_ - y) * sens, 0.0f, 1.0f);
    params().setNorm((P)dragId_, n);
  }
  (void)info;
}

void App::onPointerUp() {
  if (dragId_ == (int)P::RecSync) {
    settings_.recSyncMs = params().get(P::RecSync);
    SettingsStore::save(settings_);
  }
  dragId_ = -1;
  dragKind_ = -1;
}

std::wstring App::clipboardText() {
  std::wstring text;
  if (!OpenClipboard(hwnd_)) return text;
  if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
    if (const auto* p = (const wchar_t*)GlobalLock(h)) {
      text.assign(p);
      GlobalUnlock(h);
    }
  } else if (HANDLE h = GetClipboardData(CF_TEXT)) {
    if (const auto* p = (const char*)GlobalLock(h)) {
      text = widen(p);
      GlobalUnlock(h);
    }
  }
  CloseClipboard();
  return text;
}

void App::pasteIntoField() {
  if (editField_ == A_NONE) return;

  const std::wstring raw = clipboardText();
  logLine(("ui: paste requested, clipboard " + std::to_string((int)raw.size()) + " chars").c_str());
  std::wstring clean;
  clean.reserve(raw.size());
  for (wchar_t c : raw) {
    // Copied links frequently arrive wrapped in quotes, newlines or tabs.
    if (c == L'\r' || c == L'\n' || c == L'\t') continue;
    if (c < 32 || c == 127) continue;
    clean.push_back(c);
  }
  const wchar_t spaces[] = {L' ', L'\u00A0', L'"', L'\''};
  auto trimmable = [&](wchar_t c) {
    for (wchar_t s : spaces)
      if (c == s) return true;
    return false;
  };
  while (!clean.empty() && trimmable(clean.front())) clean.erase(clean.begin());
  while (!clean.empty() && trimmable(clean.back())) clean.pop_back();
  if (clean.empty()) {
    status_ = L"Clipboard is empty.";
    return;
  }

  const bool looksLikeUrl = clean.find(L"http") != std::wstring::npos ||
                            clean.find(L"youtu") != std::wstring::npos ||
                            clean.size() == 11 || clean.size() == 13;
  if (editField_ == A_URL_FIELD && looksLikeUrl) {
    // A pasted link replaces the field instead of being appended to it.
    editText_ = clean;
    caret_ = editText_.size();
    status_ = L"Link pasted - press LOAD or Enter.";
    logLine(("ui: pasted " + std::to_string((int)clean.size()) + " chars into the link field").c_str());
  } else {
    if (caret_ > editText_.size()) caret_ = editText_.size();
    editText_.insert(editText_.begin() + (long)caret_, clean.begin(), clean.end());
    caret_ += clean.size();
  }
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::onWheel(float x, float y, float delta) {
  // An open dropdown owns the wheel: long endpoint lists have to be reachable.
  if (popup_.open) {
    const float itemH = 24.0f;
    const float contentH = itemH * popup_.items.size() + 8.0f;
    const float maxH = std::min(340.0f, std::max(140.0f, gfx_.height() - 140.0f));
    const float viewH = std::min(maxH, contentH) - 8.0f;
    const float maxScroll = std::max(0.0f, contentH - 8.0f - viewH);
    popup_.scroll = std::clamp(popup_.scroll - delta * 48.0f, 0.0f, maxScroll);
    return;
  }
  if (overlayQueue_) {
    const float maxScroll = std::max(0.0f, queueContentH_ - queueViewH_);
    queueScroll_ = std::clamp(queueScroll_ - delta * 40.0f, 0.0f, maxScroll);
    return;
  }
  const Hit* h = hitAt(x, y);
  // Scrolling over the microphone column scrolls the strip.
  if ((!h || h->kind == 0) && x >= rcLeft_.left && x <= rcLeft_.right && y >= rcLeft_.top &&
      y <= rcLeft_.bottom) {
    leftScroll_ = std::max(0.0f, leftScroll_ - delta * 40.0f);
    return;
  }
  if (!h) return;
  if (h->id >= 0 && h->id < kParamCount) {
    const float step = (GetKeyState(VK_SHIFT) & 0x8000) ? 0.005f : 0.02f;
    const float n = std::clamp(params().norm((P)h->id) + delta * step, 0.0f, 1.0f);
    params().setNorm((P)h->id, n);
  }
}

const Hit* App::hitAt(float x, float y) const {
  // Reverse order so the most recently drawn (topmost) widget wins.
  for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) {
    if (x >= it->rect.left && x <= it->rect.right && y >= it->rect.top && y <= it->rect.bottom)
      return &(*it);
  }
  return nullptr;
}

void App::addHit(int id, D2D1_RECT_F r, int kind, int extra) {
  Hit h;
  h.id = id;
  h.rect = r;
  h.kind = kind;
  h.extra = extra;
  hits_.push_back(h);
}

std::string App::paramLabel(int param) const { return kParams[param].label; }

std::string App::paramText(int param) const {
  const ParamInfo& info = kParams[param];
  const float v = params().get((P)param);
  char buf[48];
  const std::string unit = info.unit ? info.unit : "";

  if (param == (int)P::PreMode) {
    return v < 0.5f ? "CLEAN" : (v < 1.5f ? "WARM" : "DRIVEN");
  }
  if (param == (int)P::RevType) {
    return dsp::Reverb::typeName((int)std::lround(v));
  }
  if (unit == "Hz") return ui::fmtHz(v);
  if (unit == "dB") return ui::fmtDb(v, std::fabs(v) < 10.0f ? 1 : 0) + " dB";
  if (unit == "ms") return ui::fmtMs(v);
  if (unit == "s") return ui::fmtSec(v);
  if (unit == ":1") {
    snprintf(buf, sizeof(buf), "%.1f:1", v);
    return buf;
  }
  if (param == (int)P::MicFader || param == (int)P::MusicFader ||
      param == (int)P::MasterFader) {
    const float db = 20.0f * std::log10(std::max(0.0001f, v));
    return ui::fmtDb(db, 1) + " dB";
  }
  snprintf(buf, sizeof(buf), "%d%%", (int)std::lround(v * 100.0f));
  return buf;
}

bool App::paramEnabled(int param) const {
  switch ((P)param) {
  case P::EqLowGain:
  case P::EqLowFreq:
  case P::EqLowQ:
    return params().getBool(P::EqOn) && params().getBool(P::EqLowOn);
  case P::EqLoMidGain:
  case P::EqLoMidFreq:
  case P::EqLoMidQ:
    return params().getBool(P::EqOn) && params().getBool(P::EqLoMidOn);
  case P::EqHiMidGain:
  case P::EqHiMidFreq:
  case P::EqHiMidQ:
    return params().getBool(P::EqOn) && params().getBool(P::EqHiMidOn);
  case P::EqHighGain:
  case P::EqHighFreq:
  case P::EqHighQ:
    return params().getBool(P::EqOn) && params().getBool(P::EqHighOn);
  case P::CompThresh:
  case P::CompRatio:
  case P::CompAttack:
  case P::CompRelease:
  case P::CompKnee:
  case P::CompMakeup:
  case P::CompMix:
    return params().getBool(P::CompOn);
  case P::RevType:
  case P::RevSend:
  case P::RevWet:
  case P::RevPredelay:
  case P::RevDecay:
  case P::RevSize:
  case P::RevDamp:
  case P::RevDiffusion:
  case P::RevEarly:
  case P::RevHf:
  case P::RevLf:
    return params().getBool(P::RevOn);
  case P::HpfFreq:
    return params().getBool(P::HpfOn);
  default:
    return true;
  }
}

// ---------------------------------------------------------------------------
// Window messages
// ---------------------------------------------------------------------------
LRESULT App::handleMessage(UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
  case WM_PAINT: {
    PAINTSTRUCT ps;
    BeginPaint(hwnd_, &ps);
    paint();
    EndPaint(hwnd_, &ps);
    return 0;
  }
  case WM_ERASEBKGND: return 1;
  case WM_TIMER:
    if (wp == 1) tick();
    return 0;
  case WM_SIZE:
    if (wp != SIZE_MINIMIZED) {
      RECT rc;
      GetClientRect(hwnd_, &rc);
      const UINT dpi = GetDpiForWindow(hwnd_);
      gfx_.resize(rc.right - rc.left, rc.bottom - rc.top, dpi ? (float)dpi : 96.0f);
      needsRelayout_ = true;
      syncWindowToOverlay();
    }
    return 0;
  case WM_DPICHANGED: {
    RECT* r = (RECT*)lp;
    SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    needsRelayout_ = true;
    return 0;
  }
  case WM_GETMINMAXINFO: {
    auto* mmi = (MINMAXINFO*)lp;
    const float s = gfx_.scale();
    mmi->ptMinTrackSize.x = (LONG)(1180 * s);
    mmi->ptMinTrackSize.y = (LONG)(680 * s);
    return 0;
  }
  case WM_SETCURSOR: {
    if (LOWORD(lp) == HTCLIENT) {
      POINT pt;
      GetCursorPos(&pt);
      ScreenToClient(hwnd_, &pt);
      const float s = gfx_.scale();
      const Hit* h = hitAt(pt.x / s, pt.y / s);
      if (h && (h->kind == 0 || h->kind == 1)) {
        SetCursor(LoadCursor(nullptr, IDC_SIZENS));
        return TRUE;
      }
      if (h) {
        SetCursor(LoadCursor(nullptr, IDC_HAND));
        return TRUE;
      }
    }
    break;
  }
  case WM_POINTERDOWN:
  case WM_POINTERUPDATE:
  case WM_POINTERUP: {
    const UINT id = GET_POINTERID_WPARAM(wp);
    POINTER_INFO pi{};
    if (!GetPointerInfo(id, &pi)) break;
    POINT pt = pi.ptPixelLocation;
    ScreenToClient(hwnd_, &pt);
    const float s = gfx_.scale();
    const float x = pt.x / s, y = pt.y / s;
    if (msg == WM_POINTERDOWN) {
      SetCapture(hwnd_);
      onPointerDown(x, y);
    } else if (msg == WM_POINTERUPDATE) {
      if (pi.pointerFlags & POINTER_FLAG_INCONTACT) onPointerMove(x, y);
    } else {
      onPointerUp();
      ReleaseCapture();
    }
    return 0;
  }
  case WM_MOUSEMOVE: {
    const float s = gfx_.scale();
    const float x = (float)GET_X_LPARAM(lp) / s, y = (float)GET_Y_LPARAM(lp) / s;
    if (dragId_ >= 0) onPointerMove(x, y);
    return 0;
  }
  case WM_LBUTTONDOWN: {
    SetFocus(hwnd_);
    SetCapture(hwnd_);
    const float s = gfx_.scale();
    onPointerDown((float)GET_X_LPARAM(lp) / s, (float)GET_Y_LPARAM(lp) / s);
    return 0;
  }
  case WM_LBUTTONUP: {
    onPointerUp();
    ReleaseCapture();
    return 0;
  }
  case WM_MOUSEWHEEL: {
    POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    ScreenToClient(hwnd_, &pt);
    const float s = gfx_.scale();
    onWheel(pt.x / s, pt.y / s, (float)GET_WHEEL_DELTA_WPARAM(wp) / 120.0f);
    return 0;
  }
  case WM_PASTE: {
    if (editField_ != A_NONE) {
      pasteIntoField();
      return 0;
    }
    break;
  }
  case WM_CHAR: {
    if (editField_ != A_NONE) {
      // Ctrl+V and Ctrl+X arrive here as control characters; handled elsewhere.
      if (wp < 32) {
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
      }
      const wchar_t ch = (wchar_t)wp;
      if (ch >= 32) {
        editText_.insert(editText_.begin() + (long)caret_, ch);
        ++caret_;
      }
      InvalidateRect(hwnd_, nullptr, FALSE);
      return 0;
    }
    break;
  }
  case WM_KEYDOWN: {
    if (editField_ != A_NONE) {
      if (wp == VK_BACK && caret_ > 0) {
        editText_.erase(editText_.begin() + (long)(caret_ - 1));
        --caret_;
      } else if (wp == VK_LEFT && caret_ > 0) {
        --caret_;
      } else if (wp == VK_RIGHT && caret_ < editText_.size()) {
        ++caret_;
      } else if (wp == VK_RETURN) {
        if (editField_ == A_URL_FIELD)
          runAction(A_YT_LOAD);
        else if (editField_ == A_NAME_FIELD)
          confirmSavePreset();
      } else if (wp == VK_DELETE && caret_ < editText_.size()) {
        editText_.erase(editText_.begin() + (long)caret_);
      } else if (wp == VK_HOME) {
        caret_ = 0;
      } else if (wp == VK_END) {
        caret_ = editText_.size();
      } else if (wp == 'V' && (GetKeyState(VK_CONTROL) & 0x8000)) {
        pasteIntoField();
      } else if (wp == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) {
        caret_ = editText_.size();
      } else if (wp == VK_ESCAPE) {
        editField_ = A_NONE;
      }
      InvalidateRect(hwnd_, nullptr, FALSE);
      return 0;
    }
    if (wp == VK_ESCAPE) {
      if (videoFullscreen_) {
        videoFullscreen_ = false;
        syncWindowToOverlay();
      } else if (overlaySettings_ || overlaySave_) {
        closeOverlay();
      }
    } else if (wp == VK_F11) {
      videoFullscreen_ = !videoFullscreen_;
      syncWindowToOverlay();
    } else if (wp == VK_SPACE) {
      runAction(A_YT_PLAY);
    } else if (wp == 'S' && (GetKeyState(VK_CONTROL) & 0x8000)) {
      beginSavePreset();
    }
    return 0;
  }
  case WM_CLOSE:
    KillTimer(hwnd_, 1);
    engine_.Stop();
    youtube_.destroy();
    settings_.lastUrl = youtube_.videoId();
    SettingsStore::save(settings_);
    running_ = false;
    DestroyWindow(hwnd_);
    return 0;
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  default:
    break;
  }
  return DefWindowProcW(hwnd_, msg, wp, lp);
}

} // namespace lvs
