#pragma once
#include "web/YouTubeHost.h"

#include <shlobj.h>
#include <wrl.h>
#include <wrl/event.h>

#include <fstream>

#include <WebView2.h>
#include <WebView2EnvironmentOptions.h>

#include <cctype>
#include <cstdio>
#include <string>

#include "core/Settings.h"
#include "web/PlayerPage.h"

using namespace Microsoft::WRL;

namespace lvs {

namespace {

std::wstring webViewDataDir() {
  wchar_t buf[MAX_PATH] = {};
  if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, buf))) {
    std::wstring dir = std::wstring(buf) + L"\\SoloWorshiper\\WebView2";
    CreateDirectoryW((std::wstring(buf) + L"\\SoloWorshiper").c_str(), nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
  }
  return L"";
}

std::wstring webPagesDir() {
  wchar_t buf[MAX_PATH] = {};
  if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, buf))) {
    std::wstring base = std::wstring(buf) + L"\\SoloWorshiper";
    std::wstring dir = base + L"\\web";
    CreateDirectoryW(base.c_str(), nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
  }
  return L"";
}

// Writes player.html next to the WebView2 profile. The file is static; rewriting
// it every start keeps an edited copy from breaking the player.
bool writePlayerPage(const std::wstring& dir) {
  if (dir.empty()) return false;
  const std::wstring path = dir + L"\\player.html";
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write(kPlayerHtml, (std::streamsize)strlen(kPlayerHtml));
  return true;
}

std::string narrow(const wchar_t* s) {
  if (!s) return {};
  int len = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
  std::string out((size_t)std::max(0, len - 1), '\0');
  if (len > 0) WideCharToMultiByte(CP_UTF8, 0, s, -1, out.data(), len, nullptr, nullptr);
  return out;
}

// The script result arrives as a JSON string: strip the quotes and the
// minimal escapes we actually produce.
std::string unquote(const wchar_t* raw) {
  std::string s = narrow(raw);
  if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
    s = s.substr(1, s.size() - 2);
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
      if (s[i] == '\\' && i + 1 < s.size()) {
        const char n = s[++i];
        if (n == 'n') out += '\n';
        else if (n == 't') out += '\t';
        else out += n;
      } else {
        out += s[i];
      }
    }
    return out;
  }
  return s;
}

} // namespace

YouTubeHost::~YouTubeHost() { destroy(); }

bool YouTubeHost::create(HWND parent) {
  parent_ = parent;
  if (controller_) return true;
  const std::wstring dataDir = webViewDataDir();

  // Karaoke must start on LOAD without a click inside the player frame, so the
  // embedded browser opts out of the autoplay gesture requirement.
  ComPtr<CoreWebView2EnvironmentOptions> envOptions = Make<CoreWebView2EnvironmentOptions>();
  if (envOptions) {
    envOptions->put_AdditionalBrowserArguments(L"--autoplay-policy=no-user-gesture-required");
  }
  writePlayerPage(webPagesDir());

  HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
      nullptr, dataDir.empty() ? nullptr : dataDir.c_str(), envOptions.Get(),
      Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
          [this](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
            if (FAILED(result) || !env) {
              error_ = "WebView2 runtime could not be started (error " +
                       std::to_string((long)result) + ")";
              return S_OK;
            }
            env->AddRef();
            environment_ = env;
            env->CreateCoreWebView2Controller(
                parent_,
                Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [this](HRESULT r2, ICoreWebView2Controller* ctrl) -> HRESULT {
                      if (FAILED(r2) || !ctrl) {
                        error_ = "The embedded browser could not be created.";
                        return S_OK;
                      }
                      ctrl->AddRef();
                      controller_ = ctrl;
                      ctrl->put_Bounds(bounds_);
                      ICoreWebView2* wv = nullptr;
                      if (SUCCEEDED(ctrl->get_CoreWebView2(&wv)) && wv) {
                        wv->AddRef();
                        webview_ = wv;
                        ICoreWebView2Settings* st = nullptr;
                        if (SUCCEEDED(wv->get_Settings(&st)) && st) {
                          st->put_AreDefaultContextMenusEnabled(FALSE);
                          st->put_IsStatusBarEnabled(FALSE);
                          st->put_AreDevToolsEnabled(FALSE);
                          st->put_IsZoomControlEnabled(FALSE);
                          st->Release();
                        }
                        // Serve the player page from https://lvs.local so the
                        // embed has a real origin and referer.
                        ICoreWebView2_3* wv3 = nullptr;
                        if (SUCCEEDED(wv->QueryInterface(IID_ICoreWebView2_3, (void**)&wv3)) &&
                            wv3) {
                          wv3->SetVirtualHostNameToFolderMapping(
                              L"lvs.local", webPagesDir().c_str(),
                              COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
                          wv3->Release();
                        }

                        // Keep embedded links inside the panel.
                        EventRegistrationToken tok{};
                        wv->add_NewWindowRequested(
                            Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                                [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args)
                                    -> HRESULT {
                                  args->put_Handled(TRUE);
                                  return S_OK;
                                })
                                .Get(),
                            &tok);
                        ctrl->put_IsVisible(visible_ ? TRUE : FALSE);
                        if (!videoId_.empty()) {
                          navigate();
                        } else {
                          // Dark placeholder so the panel never flashes white.
                          wv->NavigateToString(
                              L"<!doctype html><html><head><meta charset=\"utf-8\">"
                              L"<style>html,body{margin:0;height:100%;background:#08090B}"
                              L"</style></head><body></body></html>");
                        }
                      }
                      return S_OK;
                    })
                    .Get());
            return S_OK;
          })
          .Get());

  if (FAILED(hr)) {
    error_ = "WebView2 is not available on this system (error " + std::to_string((long)hr) + ")";
    return false;
  }
  return true;
}

void YouTubeHost::destroy() {
  if (controller_) {
    ((ICoreWebView2Controller*)controller_)->Close();
    ((ICoreWebView2Controller*)controller_)->Release();
    controller_ = nullptr;
  }
  if (webview_) {
    ((ICoreWebView2*)webview_)->Release();
    webview_ = nullptr;
  }
  if (environment_) {
    ((ICoreWebView2Environment*)environment_)->Release();
    environment_ = nullptr;
  }
}

void YouTubeHost::setBounds(RECT px) {
  bounds_ = px;
  if (controller_) ((ICoreWebView2Controller*)controller_)->put_Bounds(px);
}

void YouTubeHost::setVisible(bool visible) {
  visible_ = visible;
  if (controller_) ((ICoreWebView2Controller*)controller_)->put_IsVisible(visible ? TRUE : FALSE);
}

void YouTubeHost::focus() {
  if (controller_) ((ICoreWebView2Controller*)controller_)->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
}

void YouTubeHost::eval(const std::string& js) {
  if (!webview_) return;
  const std::wstring wjs = std::wstring(js.begin(), js.end());
  ((ICoreWebView2*)webview_)->ExecuteScript(wjs.c_str(), nullptr);
}

void YouTubeHost::loadVideo(const std::string& id, bool autoplay) {
  videoId_ = id;
  playlistId_.clear();
  autoplay_ = autoplay;
  title_.clear();
  position_ = duration_ = 0.0;
  playing_ = false;
  state_ = -1;
  if (!controller_) return;
  navigate();
}

void YouTubeHost::loadPlaylist(const std::string& listId, bool autoplay) {
  playlistId_ = listId;
  videoId_.clear();
  autoplay_ = autoplay;
  title_.clear();
  position_ = duration_ = 0.0;
  playing_ = false;
  state_ = -1;
  if (!controller_) return;
  navigate();
}

void YouTubeHost::navigate() {
  if (!webview_) return;
  if (videoId_.empty() && playlistId_.empty()) return;
  char url[512];
  snprintf(url, sizeof(url), "https://lvs.local/player.html?v=%s&list=%s&autoplay=%d",
           videoId_.c_str(), playlistId_.c_str(), autoplay_ ? 1 : 0);
  const std::wstring wurl(url, url + strlen(url));
  ((ICoreWebView2*)webview_)->Navigate(wurl.c_str());
}

void YouTubeHost::play() {
  eval("(function(){if(ready&&player){player.playVideo();}})();");
}

void YouTubeHost::pause() {
  eval("(function(){if(ready&&player){player.pauseVideo();}})();");
}

void YouTubeHost::seekTo(double seconds) {
  char js[192];
  snprintf(js, sizeof(js),
           "(function(){if(ready&&player){player.seekTo(%.2f,true);}})();",
           seconds < 0 ? 0.0 : seconds);
  eval(js);
}

void YouTubeHost::seekBy(double seconds) {
  char js[192];
  snprintf(js, sizeof(js),
           "(function(){if(ready&&player){player.seekTo((player.getCurrentTime()||0)+%.2f,"
           "true);}})();",
           seconds);
  eval(js);
}

void YouTubeHost::nextVideo() {
  eval("(function(){if(ready&&player){player.nextVideo();}})();");
}

void YouTubeHost::previousVideo() {
  eval("(function(){if(ready&&player){player.previousVideo();}})();");
}

void YouTubeHost::poll() {
  if (!webview_) return;
  // Playback state as a simple pipe-delimited string.
  ((ICoreWebView2*)webview_)->ExecuteScript(
      L"(function(){if(!ready||!player)return '';"
      L"var t=player.getCurrentTime()||0,d=player.getDuration()||0,"
      L"s=player.getPlayerState(),ti='',vi='';"
      L"try{var vd=player.getVideoData()||{};ti=vd.title||'';vi=vd.video_id||'';}catch(e){}"
      L"return t+'|'+d+'|'+(s===1?1:0)+'|'+s+'|'+vi+'|'+ti;})()",
      Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
          [this](HRESULT hr, LPCWSTR result) -> HRESULT {
            if (FAILED(hr) || !result) return S_OK;
            const std::string s = unquote(result);
            if (s.empty()) return S_OK;
            const size_t p1 = s.find('|');
            const size_t p2 = p1 == std::string::npos ? std::string::npos : s.find('|', p1 + 1);
            const size_t p3 = p2 == std::string::npos ? std::string::npos : s.find('|', p2 + 1);
            const size_t p4 = p3 == std::string::npos ? std::string::npos : s.find('|', p3 + 1);
            const size_t p5 = p4 == std::string::npos ? std::string::npos : s.find('|', p4 + 1);
            if (p1 == std::string::npos || p2 == std::string::npos) return S_OK;
            position_ = atof(s.substr(0, p1).c_str());
            duration_ = atof(s.substr(p1 + 1, p2 - p1 - 1).c_str());
            const size_t playEnd = p3 == std::string::npos ? s.size() : p3;
            playing_ = atoi(s.substr(p2 + 1, playEnd - p2 - 1).c_str()) != 0;
            if (p3 != std::string::npos && p4 != std::string::npos) {
              state_ = atoi(s.substr(p3 + 1, p4 - p3 - 1).c_str());
            }
            if (p5 != std::string::npos) title_ = s.substr(p5 + 1);
            return S_OK;
          })
          .Get());
}

std::string YouTubeHost::extractPlaylistId(const std::string& url) {
  const std::string key = "list=";
  size_t p = url.find(key);
  if (p == std::string::npos) return {};
  size_t at = p + key.size();
  size_t end = at;
  while (end < url.size()) {
    const unsigned char c = (unsigned char)url[end];
    if (!(std::isalnum(c) || c == '-' || c == '_')) break;
    ++end;
  }
  const size_t len = end - at;
  if (len < 12 || len > 64) return {};
  return url.substr(at, len);
}

std::string YouTubeHost::extractVideoId(const std::string& url) {
  auto isId = [](const std::string& s, size_t at) {
    if (at >= s.size() || s.size() - at < 11) return false;
    for (size_t i = 0; i < 11; ++i) {
      const unsigned char c = (unsigned char)s[at + i];
      if (!(std::isalnum(c) || c == '-' || c == '_')) return false;
    }
    return true;
  };
  const std::string key = "v=";
  size_t p = url.find(key);
  if (p != std::string::npos && isId(url, p + key.size())) return url.substr(p + 2, 11);
  for (const char* marker : {"youtu.be/", "/embed/", "/shorts/", "/live/"}) {
    p = url.find(marker);
    if (p != std::string::npos) {
      const size_t at = p + strlen(marker);
      if (isId(url, at)) return url.substr(at, 11);
    }
  }
  // Bare ID pasted on its own.
  if (url.size() == 11 && isId(url, 0)) return url;
  return {};
}

} // namespace lvs
