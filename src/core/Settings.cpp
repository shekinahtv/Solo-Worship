#include "core/Settings.h"

#include "core/Json.h"

#include <cstdio>
#include <shlobj.h>
#include <windows.h>

namespace lvs {

namespace {
// The console used to be called Live Vocal Studio. Carry a singer's settings
// and saved presets over once, without ever overwriting newer files.
void migrateOldData(const std::wstring& newDir) {
  wchar_t buf[MAX_PATH] = {};
  if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, buf))) return;
  const std::wstring oldDir = std::wstring(buf) + L"\\LiveVocalStudio";
  for (const wchar_t* name : {L"settings.json", L"presets.json"}) {
    const std::wstring from = oldDir + L"\\" + name;
    const std::wstring to = newDir + L"\\" + name;
    if (GetFileAttributesW(from.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
    if (GetFileAttributesW(to.c_str()) != INVALID_FILE_ATTRIBUTES) continue;
    CopyFileW(from.c_str(), to.c_str(), TRUE);
  }
}
} // namespace

namespace AppPaths {

std::wstring appDataDir() {
  wchar_t buf[MAX_PATH] = {};
  if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, buf))) {
    std::wstring dir = std::wstring(buf) + L"\\SoloWorshiper";
    CreateDirectoryW(dir.c_str(), nullptr);
    migrateOldData(dir);
    return dir;
  }
  wchar_t tmp[MAX_PATH] = {};
  GetTempPathW(MAX_PATH, tmp);
  std::wstring dir = std::wstring(tmp) + L"SoloWorshiper";
  CreateDirectoryW(dir.c_str(), nullptr);
  return dir;
}

std::wstring settingsFile() { return appDataDir() + L"\\settings.json"; }
std::wstring presetsFile() { return appDataDir() + L"\\presets.json"; }

std::wstring defaultRecordingFolder() {
  wchar_t buf[MAX_PATH] = {};
  if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_MYMUSIC, nullptr, SHGFP_TYPE_CURRENT, buf))) {
    std::wstring dir = std::wstring(buf) + L"\\Solo Worshiper";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
  }
  return appDataDir() + L"\\Recordings";
}

std::wstring recordingsDir() {
  std::wstring dir = defaultRecordingFolder();
  CreateDirectoryW(dir.c_str(), nullptr);
  return dir;
}

} // namespace AppPaths

static std::string narrow(const std::wstring& w) { return json::Value::toUtf8(w); }
static std::wstring widen(const std::string& s) { return json::Value::toWide(s); }

AppSettings SettingsStore::load() {
  AppSettings s;
  s.engine.captureId = Engine::SuggestCaptureEndpoint();
  s.engine.renderId = Engine::SuggestRenderEndpoint();
  s.engine.loopbackId = Engine::SuggestLoopbackEndpoint();
  s.engine.sampleRate = 48000;
  s.engine.bufferFrames = 128;
  s.engine.exclusive = true;
  // Prefer ASIO: it is the only path that reaches the hardware's small buffers.
  s.engine.asioClsid = Engine::SuggestAsioClsid();
  s.engine.useAsio = !s.engine.asioClsid.empty();
  s.recordingFolder = AppPaths::defaultRecordingFolder();

  FILE* f = _wfopen(AppPaths::settingsFile().c_str(), L"rb");
  if (!f) return s;
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  std::string text((size_t)(size > 0 ? size : 0), '\0');
  if (size > 0) fread(text.data(), 1, (size_t)size, f);
  fclose(f);

  auto root = json::Value::parse(text);
  if (!root.isObject()) return s;
  if (const auto* e = root.find("engine")) {
    if (const auto* v = e->find("capture")) s.engine.captureId = widen(v->string());
    if (const auto* v = e->find("render")) s.engine.renderId = widen(v->string());
    if (const auto* v = e->find("loopback")) s.engine.loopbackId = widen(v->string());
    if (const auto* v = e->find("rate")) s.engine.sampleRate = v->i(48000);
    if (const auto* v = e->find("buffer")) s.engine.bufferFrames = v->i(128);
    if (const auto* v = e->find("exclusive")) s.engine.exclusive = v->boolean(true);
    if (const auto* v = e->find("channel")) s.engine.captureChannel = v->i(0);
    if (const auto* v = e->find("outL")) s.engine.outLeft = v->i(0);
    if (const auto* v = e->find("outR")) s.engine.outRight = v->i(1);
    if (const auto* v = e->find("captureEnabled")) s.engine.captureEnabled = v->boolean(true);
    if (const auto* v = e->find("asio")) s.engine.asioClsid = widen(v->string());
    if (const auto* v = e->find("useAsio")) s.engine.useAsio = v->boolean(false);
    if (const auto* v = e->find("musicEnabled")) s.engine.musicEnabled = v->boolean(true);
    if (const auto* v = e->find("loopbackFollow")) s.engine.loopbackFollowDefault = v->boolean(true);
    if (const auto* v = e->find("syncId")) s.engine.latencyCacheId = widen(v->string());
    if (const auto* v = e->find("syncMs")) s.engine.latencyCacheMs = (double)v->f(0.0f);
  }
  if (const auto* v = root.find("recordFolder")) s.recordingFolder = widen(v->string());
  if (const auto* v = root.find("recordBits")) s.recordBits = v->i(24);
  if (const auto* v = root.find("recordVocal")) s.recordVocal = v->boolean(false);
  if (const auto* v = root.find("lastUrl")) s.lastUrl = v->string();
  if (const auto* v = root.find("lastPreset")) s.lastPreset = v->string();
  if (const auto* v = root.find("presetCategory")) s.presetCategory = v->i(0);
  if (const auto* v = root.find("videoTitle")) s.videoTitle = widen(v->string());
  if (const auto* v = root.find("fullscreenVideo")) s.fullscreenVideo = v->boolean(false);
  if (const auto* v = root.find("musicBus")) s.musicThroughBus = v->boolean(false);
  if (const auto* v = root.find("recSyncMs")) s.recSyncMs = v->f(0.0f);
  if (const auto* v = root.find("syncTrimMigrated")) s.syncTrimMigrated = v->boolean(false);
  if (const auto* v = root.find("playbackRate")) s.playbackRate = (double)v->f(1.0f);
  if (const auto* v = root.find("karaoke")) s.karaoke = v->boolean(false);
  if (const auto* v = root.find("windowScale")) s.windowScale = v->f(1.0f);
  return s;
}

bool SettingsStore::save(const AppSettings& s) {
  auto root = json::Value::object();
  auto e = json::Value::object();
  e["capture"] = json::Value(narrow(s.engine.captureId));
  e["render"] = json::Value(narrow(s.engine.renderId));
  e["loopback"] = json::Value(narrow(s.engine.loopbackId));
  e["rate"] = json::Value(s.engine.sampleRate);
  e["buffer"] = json::Value(s.engine.bufferFrames);
  e["exclusive"] = json::Value(s.engine.exclusive);
  e["channel"] = json::Value(s.engine.captureChannel);
  e["outL"] = json::Value(s.engine.outLeft);
  e["outR"] = json::Value(s.engine.outRight);
  e["captureEnabled"] = json::Value(s.engine.captureEnabled);
  e["asio"] = json::Value(narrow(s.engine.asioClsid));
  e["useAsio"] = json::Value(s.engine.useAsio);
  e["musicEnabled"] = json::Value(s.engine.musicEnabled);
  e["loopbackFollow"] = json::Value(s.engine.loopbackFollowDefault);
  e["syncId"] = json::Value(narrow(s.engine.latencyCacheId));
  e["syncMs"] = json::Value(s.engine.latencyCacheMs);
  root["engine"] = e;
  root["recordFolder"] = json::Value(narrow(s.recordingFolder));
  root["recordBits"] = json::Value(s.recordBits);
  root["recordVocal"] = json::Value(s.recordVocal);
  root["lastUrl"] = json::Value(s.lastUrl);
  root["lastPreset"] = json::Value(s.lastPreset);
  root["presetCategory"] = json::Value(s.presetCategory);
  root["videoTitle"] = json::Value(narrow(s.videoTitle));
  root["fullscreenVideo"] = json::Value(s.fullscreenVideo);
  root["musicBus"] = json::Value(s.musicThroughBus);
  root["recSyncMs"] = json::Value((double)s.recSyncMs);
  root["syncTrimMigrated"] = json::Value(s.syncTrimMigrated);
  root["playbackRate"] = json::Value(s.playbackRate);
  root["karaoke"] = json::Value(s.karaoke);
  root["windowScale"] = json::Value((double)s.windowScale);

  FILE* f = _wfopen(AppPaths::settingsFile().c_str(), L"wb");
  if (!f) return false;
  const std::string text = root.dump(2);
  fwrite(text.data(), 1, text.size(), f);
  fclose(f);
  return true;
}

} // namespace lvs
