// ---------------------------------------------------------------------------
// SOLO WORSHIPER - entry point.
// ---------------------------------------------------------------------------
#include <windows.h>

#include <objbase.h>

#include "app/App.h"

int WINAPI wWinMain(HINSTANCE hinst, HINSTANCE, LPWSTR, int) {
  // Per-monitor DPI so the console stays crisp on high-DPI panels.
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  // The UI thread hosts WebView2, which requires a single-threaded apartment.
  // Worker threads create their own MTA apartments.
  HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return 1;

  lvs::App app;
  if (!app.init(hinst)) {
    MessageBoxW(nullptr,
                L"SOLO WORSHIPER could not start.\n\n"
                L"Check that your audio device is connected and that the application "
                L"folder was not moved after installation.",
                L"SOLO WORSHIPER", MB_ICONERROR | MB_OK);
    CoUninitialize();
    return 1;
  }
  const int code = app.run();
  CoUninitialize();
  return code;
}
