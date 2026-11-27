#include "audio/SessionVolume.h"

#include <windows.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace lvs {

namespace {

std::wstring lower(std::wstring s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](wchar_t c) { return (wchar_t)towlower(c); });
  return s;
}

// One snapshot per call: image name for every process plus the parent links we
// need to recognise our own WebView2 children.
struct ProcessMap {
  std::unordered_map<DWORD, std::wstring> names;
  std::unordered_map<DWORD, DWORD> parents;

  void take() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
      do {
        names[pe.th32ProcessID] = lower(pe.szExeFile);
        parents[pe.th32ProcessID] = pe.th32ParentProcessID;
      } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
  }

  bool descendsFrom(DWORD pid, DWORD ancestor) const {
    for (int guard = 0; guard < 24 && pid != 0; ++guard) {
      if (pid == ancestor) return true;
      const auto it = parents.find(pid);
      if (it == parents.end()) return false;
      pid = it->second;
    }
    return false;
  }

  bool isPlayer(DWORD pid) const {
    if (pid == 0) return false;
    const auto it = names.find(pid);
    if (it == names.end()) return false;
    const std::wstring& name = it->second;
    const bool webview = name.find(L"msedgewebview2") != std::wstring::npos ||
                         name.find(L"webviewhost") != std::wstring::npos;
    if (!webview) return false;
    // WebView2 runs the media stack in child processes of our own browser
    // process; requiring the link stops us touching a normal Edge window.
    return descendsFrom(pid, GetCurrentProcessId());
  }
};

} // namespace

bool applyPlayerVolume(float volume, bool mute, std::wstring* note) {
  const float v = std::clamp(volume, 0.0f, 1.0f);

  ProcessMap procs;
  procs.take();
  if (procs.names.empty()) {
    if (note) *note = L"session volume: process snapshot failed";
    return false;
  }

  IMMDeviceEnumerator* enumerator = nullptr;
  HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(&enumerator));
  if (FAILED(hr) || !enumerator) {
    if (note) *note = L"session volume: no device enumerator";
    return false;
  }

  IMMDevice* device = nullptr;
  hr = enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device);
  IAudioSessionManager2* manager = nullptr;
  if (SUCCEEDED(hr) && device) {
    hr = device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                          (void**)&manager);
  }
  IAudioSessionEnumerator* sessions = nullptr;
  if (SUCCEEDED(hr) && manager) hr = manager->GetSessionEnumerator(&sessions);

  int found = 0;
  int count = 0;
  if (SUCCEEDED(hr) && sessions) sessions->GetCount(&count);
  for (int i = 0; i < count; ++i) {
    IAudioSessionControl* control = nullptr;
    if (FAILED(sessions->GetSession(i, &control)) || !control) continue;
    IAudioSessionControl2* control2 = nullptr;
    if (SUCCEEDED(control->QueryInterface(IID_PPV_ARGS(&control2))) && control2) {
      DWORD pid = 0;
      if (SUCCEEDED(control2->GetProcessId(&pid)) && procs.isPlayer(pid)) {
        ISimpleAudioVolume* simple = nullptr;
        if (SUCCEEDED(control2->QueryInterface(IID_PPV_ARGS(&simple))) && simple) {
          simple->SetMasterVolume(v, nullptr);
          simple->SetMute(mute ? TRUE : FALSE, nullptr);
          simple->Release();
          ++found;
        }
      }
      control2->Release();
    }
    control->Release();
  }

  if (sessions) sessions->Release();
  if (manager) manager->Release();
  if (device) device->Release();
  enumerator->Release();

  if (note) {
    wchar_t buf[128];
    swprintf(buf, 128, L"session volume: %d of %d player session(s) at %.1f%%%s", found, count,
             v * 100.0f, mute ? L" (muted)" : L"");
    *note = buf;
  }
  return found > 0;
}

} // namespace lvs
