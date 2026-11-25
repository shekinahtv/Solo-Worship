#pragma once
// ---------------------------------------------------------------------------
// Preset system: factory vocal / reverb / EQ presets plus user presets stored
// as JSON in %APPDATA%\SoloWorshiper\presets.json.
// ---------------------------------------------------------------------------
#include "core/Params.h"

#include <string>
#include <vector>

namespace lvs {

struct Preset {
  std::string name;
  std::string category; // VOCAL, REVERB, EQ
  bool factory = true;
  std::vector<std::pair<int, float>> values; // parameter index -> value
};

class PresetManager {
public:
  void init();                       // factory presets + user file
  void reloadUser();

  const std::vector<Preset>& all() const { return presets_; }
  int count() const { return (int)presets_.size(); }
  const Preset* at(int i) const;
  int findByName(const std::string& name) const;

  void apply(int index, ParamStore& params) const;
  int saveAs(const std::string& name, const std::string& category, const ParamStore& params);
  bool rename(int index, const std::string& newName);
  bool remove(int index);

  bool loadFile(const std::wstring& path);
  bool saveFile(const std::wstring& path) const;

  static std::wstring userPresetPath();

private:
  void buildFactory();
  std::vector<Preset> presets_;
};

} // namespace lvs
