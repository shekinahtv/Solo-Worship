#include "core/Presets.h"

#include "core/Json.h"
#include "core/Settings.h"

#include <algorithm>
#include <cstdio>

namespace lvs {

namespace {
struct Set {
  P p;
  float v;
};

struct FactoryDef {
  const char* category;
  const char* name;
  std::vector<Set> sets;
};

std::vector<FactoryDef> factoryDefs() {
  using namespace lvs;
  std::vector<FactoryDef> defs;

  auto vocal = [&](const char* name, std::vector<Set> s) {
    defs.push_back({"VOCAL", name, std::move(s)});
  };

  vocal("Warm Male Vocal",
        {{P::PreGain, 1.0f}, {P::PreDrive, 0.28f}, {P::PreWarmth, 0.62f}, {P::PreSat, 0.32f},
         {P::PreOut, -1.0f}, {P::PreMode, 1.0f}, {P::HpfFreq, 85.0f},
         {P::EqLowGain, 1.5f}, {P::EqLowFreq, 110.0f}, {P::EqLoMidGain, -1.8f},
         {P::EqLoMidFreq, 430.0f}, {P::EqHiMidGain, 1.2f}, {P::EqHiMidFreq, 2600.0f},
         {P::EqHighGain, -1.2f}, {P::EqHighFreq, 8500.0f},
         {P::CompThresh, -24.0f}, {P::CompRatio, 3.5f}, {P::CompAttack, 14.0f},
         {P::CompRelease, 200.0f}, {P::CompKnee, 9.0f}, {P::CompMakeup, 5.0f},
         {P::RevType, 2.0f}, {P::RevSend, 0.32f}, {P::RevWet, 0.30f}, {P::RevDecay, 2.0f}});

  vocal("Bright Male Vocal",
        {{P::PreGain, 1.0f}, {P::PreDrive, 0.18f}, {P::PreWarmth, 0.30f}, {P::PreSat, 0.24f},
         {P::PreMode, 1.0f}, {P::HpfFreq, 95.0f},
         {P::EqLowGain, -0.8f}, {P::EqLowFreq, 120.0f}, {P::EqLoMidGain, -2.2f},
         {P::EqLoMidFreq, 380.0f}, {P::EqHiMidGain, 2.4f}, {P::EqHiMidFreq, 3200.0f},
         {P::EqHighGain, 3.0f}, {P::EqHighFreq, 9500.0f},
         {P::CompThresh, -22.0f}, {P::CompRatio, 3.0f}, {P::CompAttack, 9.0f},
         {P::CompRelease, 160.0f}, {P::CompMakeup, 4.0f},
         {P::RevType, 3.0f}, {P::RevSend, 0.26f}, {P::RevWet, 0.26f}, {P::RevDecay, 1.7f}});

  vocal("Female Lead Vocal",
        {{P::PreGain, 0.5f}, {P::PreDrive, 0.16f}, {P::PreWarmth, 0.40f}, {P::PreSat, 0.22f},
         {P::PreMode, 1.0f}, {P::HpfFreq, 120.0f},
         {P::EqLowGain, -2.0f}, {P::EqLowFreq, 160.0f}, {P::EqLoMidGain, -1.4f},
         {P::EqLoMidFreq, 520.0f}, {P::EqHiMidGain, 1.6f}, {P::EqHiMidFreq, 3600.0f},
         {P::EqHighGain, 2.0f}, {P::EqHighFreq, 10000.0f},
         {P::CompThresh, -26.0f}, {P::CompRatio, 3.2f}, {P::CompAttack, 10.0f},
         {P::CompRelease, 170.0f}, {P::CompMakeup, 6.0f},
         {P::RevType, 3.0f}, {P::RevSend, 0.34f}, {P::RevWet, 0.34f}, {P::RevDecay, 2.2f}});

  vocal("Worship Vocal",
        {{P::PreGain, 1.0f}, {P::PreDrive, 0.22f}, {P::PreWarmth, 0.55f}, {P::PreSat, 0.28f},
         {P::PreMode, 1.0f}, {P::HpfFreq, 100.0f},
         {P::EqLowGain, 0.5f}, {P::EqLowFreq, 120.0f}, {P::EqLoMidGain, -2.5f},
         {P::EqLoMidFreq, 460.0f}, {P::EqHiMidGain, 1.4f}, {P::EqHiMidFreq, 2900.0f},
         {P::EqHighGain, 1.0f}, {P::EqHighFreq, 8000.0f},
         {P::CompThresh, -25.0f}, {P::CompRatio, 4.0f}, {P::CompAttack, 12.0f},
         {P::CompRelease, 220.0f}, {P::CompMakeup, 5.5f},
         {P::RevType, 0.0f}, {P::RevSend, 0.55f}, {P::RevWet, 0.45f}, {P::RevDecay, 3.4f},
         {P::RevPredelay, 28.0f}, {P::RevHf, 0.40f}, {P::RevLf, 0.35f}});

  vocal("Soft Worship",
        {{P::PreGain, 1.5f}, {P::PreDrive, 0.14f}, {P::PreWarmth, 0.70f}, {P::PreSat, 0.20f},
         {P::PreMode, 1.0f}, {P::HpfFreq, 90.0f},
         {P::EqLowGain, 1.0f}, {P::EqLowFreq, 140.0f}, {P::EqLoMidGain, -3.0f},
         {P::EqLoMidFreq, 500.0f}, {P::EqHiMidGain, 0.6f}, {P::EqHiMidFreq, 2500.0f},
         {P::EqHighGain, -1.5f}, {P::EqHighFreq, 7500.0f},
         {P::CompThresh, -30.0f}, {P::CompRatio, 4.5f}, {P::CompAttack, 18.0f},
         {P::CompRelease, 280.0f}, {P::CompMakeup, 7.0f},
         {P::RevType, 0.0f}, {P::RevSend, 0.62f}, {P::RevWet, 0.50f}, {P::RevDecay, 4.2f},
         {P::RevPredelay, 35.0f}, {P::RevHf, 0.35f}, {P::RevLf, 0.42f}});

  vocal("Powerful Lead Vocal",
        {{P::PreGain, 0.0f}, {P::PreDrive, 0.42f}, {P::PreWarmth, 0.45f}, {P::PreSat, 0.45f},
         {P::PreMode, 2.0f}, {P::HpfFreq, 105.0f},
         {P::EqLowGain, 0.8f}, {P::EqLowFreq, 100.0f}, {P::EqLoMidGain, -2.0f},
         {P::EqLoMidFreq, 400.0f}, {P::EqHiMidGain, 1.8f}, {P::EqHiMidFreq, 3000.0f},
         {P::EqHighGain, 1.5f}, {P::EqHighFreq, 9000.0f},
         {P::CompThresh, -20.0f}, {P::CompRatio, 5.0f}, {P::CompAttack, 6.0f},
         {P::CompRelease, 140.0f}, {P::CompMakeup, 3.0f},
         {P::RevType, 3.0f}, {P::RevSend, 0.30f}, {P::RevWet, 0.30f}, {P::RevDecay, 2.6f}});

  vocal("Spoken Word",
        {{P::PreGain, 1.0f}, {P::PreDrive, 0.10f}, {P::PreWarmth, 0.45f}, {P::PreSat, 0.15f},
         {P::PreMode, 0.0f}, {P::HpfFreq, 130.0f},
         {P::EqLowGain, -3.0f}, {P::EqLowFreq, 180.0f}, {P::EqLoMidGain, -2.0f},
         {P::EqLoMidFreq, 500.0f}, {P::EqHiMidGain, 2.2f}, {P::EqHiMidFreq, 3400.0f},
         {P::EqHighGain, 1.0f}, {P::EqHighFreq, 7000.0f},
         {P::CompThresh, -28.0f}, {P::CompRatio, 4.0f}, {P::CompAttack, 8.0f},
         {P::CompRelease, 120.0f}, {P::CompMakeup, 5.0f},
         {P::RevType, 4.0f}, {P::RevSend, 0.12f}, {P::RevWet, 0.12f}, {P::RevDecay, 1.0f}});

  vocal("Pastor / Preacher",
        {{P::PreGain, 1.0f}, {P::PreDrive, 0.12f}, {P::PreWarmth, 0.38f}, {P::PreSat, 0.12f},
         {P::PreMode, 0.0f}, {P::HpfFreq, 140.0f},
         {P::EqLowGain, -4.0f}, {P::EqLowFreq, 200.0f}, {P::EqLoMidGain, -1.5f},
         {P::EqLoMidFreq, 560.0f}, {P::EqHiMidGain, 2.8f}, {P::EqHiMidFreq, 3600.0f},
         {P::EqHighGain, 0.5f}, {P::EqHighFreq, 6500.0f},
         {P::CompThresh, -32.0f}, {P::CompRatio, 5.5f}, {P::CompAttack, 6.0f},
         {P::CompRelease, 100.0f}, {P::CompMakeup, 7.5f},
         {P::RevType, 4.0f}, {P::RevSend, 0.08f}, {P::RevWet, 0.10f}, {P::RevDecay, 0.9f}});

  auto reverb = [&](const char* name, std::vector<Set> s) {
    defs.push_back({"REVERB", name, std::move(s)});
  };
  reverb("Dry Studio",
         {{P::RevType, 2.0f}, {P::RevSend, 0.05f}, {P::RevWet, 0.08f}, {P::RevDecay, 0.8f},
          {P::RevSize, 0.30f}, {P::RevPredelay, 10.0f}, {P::RevEarly, 0.20f}, {P::RevDiffusion, 0.5f}});
  reverb("Small Room",
         {{P::RevType, 4.0f}, {P::RevSend, 0.28f}, {P::RevWet, 0.24f}, {P::RevDecay, 1.1f},
          {P::RevSize, 0.28f}, {P::RevPredelay, 12.0f}, {P::RevEarly, 0.85f}, {P::RevHf, 0.55f}});
  reverb("Vocal Plate",
         {{P::RevType, 3.0f}, {P::RevSend, 0.36f}, {P::RevWet, 0.34f}, {P::RevDecay, 2.1f},
          {P::RevSize, 0.55f}, {P::RevPredelay, 22.0f}, {P::RevHf, 0.30f}, {P::RevLf, 0.50f},
          {P::RevDiffusion, 0.85f}});
  reverb("Large Hall",
         {{P::RevType, 1.0f}, {P::RevSend, 0.48f}, {P::RevWet, 0.42f}, {P::RevDecay, 3.8f},
          {P::RevSize, 0.78f}, {P::RevPredelay, 32.0f}, {P::RevHf, 0.40f}, {P::RevEarly, 0.30f}});
  reverb("Worship Hall",
         {{P::RevType, 0.0f}, {P::RevSend, 0.58f}, {P::RevWet, 0.46f}, {P::RevDecay, 4.6f},
          {P::RevSize, 0.72f}, {P::RevPredelay, 40.0f}, {P::RevHf, 0.35f}, {P::RevLf, 0.42f}});
  reverb("Cathedral",
         {{P::RevType, 6.0f}, {P::RevSend, 0.62f}, {P::RevWet, 0.50f}, {P::RevDecay, 7.0f},
          {P::RevSize, 0.92f}, {P::RevPredelay, 55.0f}, {P::RevHf, 0.30f}, {P::RevEarly, 0.20f}});
  reverb("Ambient Worship",
         {{P::RevType, 7.0f}, {P::RevSend, 0.66f}, {P::RevWet, 0.55f}, {P::RevDecay, 9.0f},
          {P::RevSize, 0.98f}, {P::RevPredelay, 75.0f}, {P::RevHf, 0.22f}, {P::RevEarly, 0.12f}});

  auto eq = [&](const char* name, std::vector<Set> s) {
    defs.push_back({"EQ", name, std::move(s)});
  };
  eq("Male Voice",
     {{P::HpfFreq, 90.0f}, {P::EqLowGain, 0.8f}, {P::EqLowFreq, 110.0f}, {P::EqLoMidGain, -2.0f},
      {P::EqLoMidFreq, 420.0f}, {P::EqHiMidGain, 1.5f}, {P::EqHiMidFreq, 2700.0f},
      {P::EqHighGain, -1.0f}, {P::EqHighFreq, 8500.0f}});
  eq("Female Voice",
     {{P::HpfFreq, 130.0f}, {P::EqLowGain, -2.2f}, {P::EqLowFreq, 160.0f}, {P::EqLoMidGain, -1.2f},
      {P::EqLoMidFreq, 520.0f}, {P::EqHiMidGain, 1.8f}, {P::EqHiMidFreq, 3600.0f},
      {P::EqHighGain, 2.2f}, {P::EqHighFreq, 10500.0f}});
  eq("Worship Vocal",
     {{P::HpfFreq, 100.0f}, {P::EqLowGain, 0.4f}, {P::EqLowFreq, 120.0f}, {P::EqLoMidGain, -2.6f},
      {P::EqLoMidFreq, 460.0f}, {P::EqHiMidGain, 1.3f}, {P::EqHiMidFreq, 2900.0f},
      {P::EqHighGain, 1.2f}, {P::EqHighFreq, 8000.0f}});
  eq("Spoken Voice",
     {{P::HpfFreq, 140.0f}, {P::EqLowGain, -3.4f}, {P::EqLowFreq, 190.0f}, {P::EqLoMidGain, -1.8f},
      {P::EqLoMidFreq, 500.0f}, {P::EqHiMidGain, 2.6f}, {P::EqHiMidFreq, 3400.0f},
      {P::EqHighGain, 0.8f}, {P::EqHighFreq, 7000.0f}});
  eq("Warm Vocal",
     {{P::HpfFreq, 80.0f}, {P::EqLowGain, 2.2f}, {P::EqLowFreq, 120.0f}, {P::EqLoMidGain, -1.5f},
      {P::EqLoMidFreq, 380.0f}, {P::EqHiMidGain, 0.8f}, {P::EqHiMidFreq, 2400.0f},
      {P::EqHighGain, -2.4f}, {P::EqHighFreq, 7500.0f}});

  return defs;
}
} // namespace

void PresetManager::buildFactory() {
  presets_.clear();
  for (auto& def : factoryDefs()) {
    Preset p;
    p.name = def.name;
    p.category = def.category;
    p.factory = true;
    for (auto& s : def.sets) p.values.emplace_back((int)s.p, s.v);
    presets_.push_back(std::move(p));
  }
}

std::wstring PresetManager::userPresetPath() { return AppPaths::presetsFile(); }

void PresetManager::init() {
  buildFactory();
  reloadUser();
}

void PresetManager::reloadUser() {
  // Drop existing user presets, keep factory.
  presets_.erase(std::remove_if(presets_.begin(), presets_.end(),
                                [](const Preset& p) { return !p.factory; }),
                 presets_.end());
  loadFile(userPresetPath());
}

const Preset* PresetManager::at(int i) const {
  if (i < 0 || i >= (int)presets_.size()) return nullptr;
  return &presets_[i];
}

int PresetManager::findByName(const std::string& name) const {
  for (int i = 0; i < (int)presets_.size(); ++i)
    if (presets_[i].name == name) return i;
  return -1;
}

void PresetManager::apply(int index, ParamStore& params) const {
  const Preset* p = at(index);
  if (!p) return;
  for (auto& kv : p->values) {
    if (kv.first >= 0 && kv.first < kParamCount) params.set((P)kv.first, kv.second);
  }
}

int PresetManager::saveAs(const std::string& name, const std::string& category,
                          const ParamStore& params) {
  int existing = findByName(name);
  Preset p;
  p.name = name;
  p.category = category;
  p.factory = false;
  for (int i = 0; i < kParamCount; ++i) {
    // Store every parameter so a user preset is a complete snapshot.
    if (category == "VOCAL" && kParams[i].group != nullptr) {
      const std::string g = kParams[i].group;
      const bool vocalGroup = (g == "Preamp" || g == "Hpf" || g == "Eq" || g == "Comp");
      if (!vocalGroup) continue;
    }
    if (category == "REVERB") {
      const std::string g = kParams[i].group ? kParams[i].group : "";
      if (g != "Reverb") continue;
    }
    if (category == "EQ") {
      const std::string g = kParams[i].group ? kParams[i].group : "";
      if (g != "Eq" && g != "Hpf") continue;
    }
    p.values.emplace_back(i, params.get((P)i));
  }
  if (existing >= 0 && !presets_[existing].factory) {
    presets_[existing] = p;
    saveFile(userPresetPath());
    return existing;
  }
  presets_.push_back(p);
  saveFile(userPresetPath());
  return (int)presets_.size() - 1;
}

bool PresetManager::rename(int index, const std::string& newName) {
  if (index < 0 || index >= (int)presets_.size()) return false;
  if (presets_[index].factory) return false;
  presets_[index].name = newName;
  return saveFile(userPresetPath());
}

bool PresetManager::remove(int index) {
  if (index < 0 || index >= (int)presets_.size()) return false;
  if (presets_[index].factory) return false;
  presets_.erase(presets_.begin() + index);
  return saveFile(userPresetPath());
}

bool PresetManager::loadFile(const std::wstring& path) {
  FILE* f = _wfopen(path.c_str(), L"rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  std::string text((size_t)std::max(0L, size), '\0');
  if (size > 0) fread(text.data(), 1, (size_t)size, f);
  fclose(f);

  std::string err;
  auto root = json::Value::parse(text, &err);
  if (!root.isObject()) return false;
  const json::Value* list = root.find("presets");
  if (!list || !list->isArray()) return false;
  for (const auto& item : list->asArray()) {
    if (!item.isObject()) continue;
    Preset p;
    p.factory = false;
    const json::Value* n = item.find("name");
    p.name = n ? n->string() : "Untitled";
    const json::Value* c = item.find("category");
    p.category = c ? c->string("VOCAL") : "VOCAL";
    const json::Value* vals = item.find("values");
    if (vals && vals->isObject()) {
      for (const auto& kv : vals->asObject()) {
        for (int i = 0; i < kParamCount; ++i) {
          if (kv.first == kParams[i].id) {
            p.values.emplace_back(i, kv.second.f());
            break;
          }
        }
      }
    }
    presets_.push_back(std::move(p));
  }
  return true;
}

bool PresetManager::saveFile(const std::wstring& path) const {
  auto root = json::Value::object();
  auto arr = json::Value::array();
  for (const auto& p : presets_) {
    if (p.factory) continue;
    auto item = json::Value::object();
    item["name"] = json::Value(p.name);
    item["category"] = json::Value(p.category);
    auto vals = json::Value::object();
    for (auto& kv : p.values) {
      if (kv.first >= 0 && kv.first < kParamCount) vals[kParams[kv.first].id] = json::Value(kv.second);
    }
    item["values"] = vals;
    arr.push(item);
  }
  root["presets"] = arr;
  root["version"] = json::Value(1);

  FILE* f = _wfopen(path.c_str(), L"wb");
  if (!f) return false;
  const std::string text = root.dump(2);
  fwrite(text.data(), 1, text.size(), f);
  fclose(f);
  return true;
}

} // namespace lvs
