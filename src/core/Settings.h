#pragma once
// ---------------------------------------------------------------------------
// Application settings: device configuration, recording options, UI state.
// Stored as JSON under %APPDATA%\SoloWorshiper.
// ---------------------------------------------------------------------------
#include "audio/Engine.h"

#include <string>

namespace lvs {

namespace AppPaths {
std::wstring appDataDir();      // creates the folder on demand
std::wstring settingsFile();
std::wstring presetsFile();
std::wstring recordingsDir();
std::wstring defaultRecordingFolder();
} // namespace AppPaths

struct AppSettings {
  EngineConfig engine;
  std::wstring recordingFolder;
  int recordBits = 24;
  bool recordVocal = false; // false == master bus
  std::string lastUrl;
  std::string lastPreset;
  int presetCategory = 0;
  std::wstring videoTitle;
  bool fullscreenVideo = false;
  // Route the karaoke through the console bus (EQ/pan live) instead of leaving
  // it on the Windows player. Only meaningful when karaoke and the console
  // output are different devices; see App::tick().
  bool musicThroughBus = false;
  float windowScale = 1.0f;
};

class SettingsStore {
public:
  static AppSettings load();
  static bool save(const AppSettings& s);
};

} // namespace lvs
