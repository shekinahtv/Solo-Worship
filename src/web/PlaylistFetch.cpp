#include "web/PlaylistFetch.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace lvs {

namespace {

std::wstring widen(const std::string& s) {
  if (s.empty()) return {};
  const int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring out((size_t)len, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), len);
  return out;
}

std::string narrow(const std::wstring& s) {
  if (s.empty()) return {};
  const int len = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr,
                                      nullptr);
  std::string out((size_t)len, '\0');
  WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), len, nullptr, nullptr);
  return out;
}

bool httpGet(const std::wstring& path, std::string* body, std::wstring* error) {
  HINTERNET session = WinHttpOpen(L"SOLO WORSHIPER/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) {
    *error = L"WinHttpOpen failed";
    return false;
  }
  WinHttpSetTimeouts(session, 6000, 6000, 8000, 8000);

  bool ok = false;
  HINTERNET connect = WinHttpConnect(session, L"www.youtube.com",
                                     INTERNET_DEFAULT_HTTPS_PORT, 0);
  HINTERNET request = nullptr;
  if (connect) {
    request = WinHttpOpenRequest(connect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                 WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
  }
  if (request) {
    // The mobile/stripped listing page carries no item data; ask as a desktop
    // browser, which is the shape of the page this parser reads.
    const wchar_t* headers =
        L"Accept: text/html,application/xhtml+xml\r\n"
        L"Accept-Language: en-US,en;q=0.9\r\n"
        L"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        L"(KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36\r\n"
        L"Cookie: SOCS=CAI; CONSENT=YES+cb\r\n";
    if (WinHttpSendRequest(request, headers, (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr)) {
      DWORD status = 0;
      DWORD len = sizeof(status);
      WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                          WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX);
      if (status == 200) {
        for (;;) {
          DWORD available = 0;
          if (!WinHttpQueryDataAvailable(request, &available)) break;
          if (available == 0) break;
          std::string chunk((size_t)available, '\0');
          DWORD read = 0;
          if (!WinHttpReadData(request, chunk.data(), available, &read)) break;
          chunk.resize(read);
          body->append(chunk);
          if (body->size() > 8u * 1024u * 1024u) break; // a playlist page is far smaller
        }
        ok = !body->empty();
        if (!ok) *error = L"empty response";
      } else {
        wchar_t buf[96];
        swprintf(buf, 96, L"HTTP %lu", (unsigned long)status);
        *error = buf;
      }
    } else {
      *error = L"request failed";
    }
  } else {
    *error = L"could not open the playlist page";
  }

  if (request) WinHttpCloseHandle(request);
  if (connect) WinHttpCloseHandle(connect);
  WinHttpCloseHandle(session);
  return ok;
}

// Reads a JSON string body starting after the opening quote, and unescapes the
// sequences YouTube uses in titles.
std::string readJsonString(const std::string& s, size_t from) {
  std::string out;
  for (size_t i = from; i < s.size(); ++i) {
    const char c = s[i];
    if (c == '"') break;
    if (c != '\\') {
      out.push_back(c);
      continue;
    }
    if (i + 1 >= s.size()) break;
    const char e = s[++i];
    switch (e) {
      case 'n': out.push_back(' '); break;
      case 't': out.push_back(' '); break;
      case 'r': break;
      case 'u': {
        if (i + 4 >= s.size()) break;
        unsigned code = 0;
        bool okHex = true;
        for (int k = 1; k <= 4; ++k) {
          const char h = s[i + (size_t)k];
          code <<= 4;
          if (h >= '0' && h <= '9') code |= (unsigned)(h - '0');
          else if (h >= 'a' && h <= 'f') code |= (unsigned)(h - 'a' + 10);
          else if (h >= 'A' && h <= 'F') code |= (unsigned)(h - 'A' + 10);
          else okHex = false;
        }
        i += 4;
        if (!okHex) break;
        // Surrogate pairs are rare in titles; encode the BMP part directly.
        if (code < 0x80) out.push_back((char)code);
        else if (code < 0x800) {
          out.push_back((char)(0xC0 | (code >> 6)));
          out.push_back((char)(0x80 | (code & 0x3F)));
        } else {
          out.push_back((char)(0xE0 | (code >> 12)));
          out.push_back((char)(0x80 | ((code >> 6) & 0x3F)));
          out.push_back((char)(0x80 | (code & 0x3F)));
        }
        break;
      }
      default: out.push_back(e); break;
    }
  }
  return out;
}

std::string firstString(const std::string& html, const char* key, size_t from, size_t limit) {
  const size_t p = html.find(key, from);
  if (p == std::string::npos || p > limit) return {};
  return readJsonString(html, p + strlen(key));
}

} // namespace

PlaylistResult fetchPlaylist(const std::string& listId) {
  PlaylistResult result;
  result.listId = listId;
  if (listId.empty()) {
    result.error = L"empty playlist id";
    return result;
  }

  const std::string path = "/playlist?list=" + listId + "&hl=en&gl=US";
  std::string html;
  std::wstring error;
  if (!httpGet(widen(path), &html, &error)) {
    result.error = L"Playlist listing unavailable (" + error + L").";
    return result;
  }

  // Playlist title from the page's own metadata.
  const std::string meta = "property=\"og:title\" content=\"";
  const size_t metaAt = html.find(meta);
  if (metaAt != std::string::npos) {
    result.name = widen(readJsonString(html, metaAt + meta.size()));
  }
  if (result.name.empty()) result.name = L"YouTube playlist";

  // Layout A - current YouTube pages: each item is a lockupViewModel whose
  // thumbnail badge carries the length and the video id, and whose metadata
  // carries the title. Walk the page in order, which is the playlist order.
  {
    const char* kLockup = "\"lockupViewModel\":{";
    const char* kBadge = "\"thumbnailBadgeViewModel\":{";
    const char* kActivate = "\"animationActivationTargetId\":\"";
    const char* kTitle = "\"lockupMetadataViewModel\":{\"title\":{\"content\":\"";
    size_t pos = 0;
    while ((pos = html.find(kLockup, pos)) != std::string::npos) {
      const size_t next = html.find(kLockup, pos + strlen(kLockup));
      const size_t limit = (next == std::string::npos) ? std::min(html.size(), pos + 12000) : next;

      PlaylistEntry entry;
      const size_t badge = html.find(kBadge, pos);
      if (badge != std::string::npos && badge < limit) {
        const size_t text = html.find("\"text\":\"", badge);
        if (text != std::string::npos && text < badge + 400) {
          entry.duration = readJsonString(html, text + 8);
        }
        const size_t act = html.find(kActivate, badge);
        if (act != std::string::npos && act < badge + 900) {
          entry.videoId = html.substr(act + strlen(kActivate), 11);
        }
      }
      if (entry.videoId.empty()) {
        const size_t thumb = html.find("i.ytimg.com/vi/", pos);
        if (thumb != std::string::npos && thumb < limit) {
          entry.videoId = html.substr(thumb + strlen("i.ytimg.com/vi/"), 11);
        }
      }
      const size_t titleAt = html.find(kTitle, pos);
      if (titleAt != std::string::npos && titleAt < limit) {
        entry.title = readJsonString(html, titleAt + strlen(kTitle));
      }
      if (entry.videoId.size() == 11) {
        if (entry.title.empty()) entry.title = entry.videoId;
        if (result.entries.empty() || result.entries.back().videoId != entry.videoId) {
          result.entries.push_back(std::move(entry));
        }
      }
      pos = (next == std::string::npos) ? html.size() : next;
      if (result.entries.size() >= 500) break;
    }
  }

  // Layout B - older pages: each item is a playlistVideoRenderer object.
  size_t pos = 0;
  while ((pos = html.find("\"playlistVideoRenderer\":{", pos)) != std::string::npos) {
    const size_t limit = std::min(html.size(), pos + 6000);
    const size_t vid = html.find("\"videoId\":\"", pos);
    if (vid == std::string::npos || vid > limit) {
      pos += 24;
      continue;
    }
    PlaylistEntry entry;
    entry.videoId = html.substr(vid + 12, 11);

    const size_t titleAt = html.find("\"title\":{", vid);
    if (titleAt != std::string::npos && titleAt < limit) {
      // Newer pages use runs[].text, older ones simpleText.
      const size_t text = html.find("\"text\":\"", titleAt);
      const size_t simple = html.find("\"simpleText\":\"", titleAt);
      if (simple != std::string::npos && simple < limit + 600 &&
          (text == std::string::npos || simple < text)) {
        entry.title = readJsonString(html, simple + 13);
      } else if (text != std::string::npos && text < limit + 600) {
        entry.title = readJsonString(html, text + 8);
      }
    }
    if (entry.title.empty()) entry.title = entry.videoId;

    const size_t lenAt = html.find("\"lengthText\":{", vid);
    if (lenAt != std::string::npos && lenAt < limit + 900) {
      const size_t simple = html.find("\"simpleText\":\"", lenAt);
      if (simple != std::string::npos && simple < lenAt + 400) {
        entry.duration = readJsonString(html, simple + 13);
      }
    }

    if (!result.entries.empty() && result.entries.back().videoId == entry.videoId) {
      pos = vid + 12;
      continue;
    }
    result.entries.push_back(std::move(entry));
    pos = vid + 12;
    if (result.entries.size() >= 500) break;
  }

  if (result.entries.empty()) {
    wchar_t buf[160];
    swprintf(buf, 160, L"Playlist listing unavailable (%u KB read, no items found).",
             (unsigned)(html.size() / 1024));
    result.error = buf;
    return result;
  }
  result.ok = true;
  return result;
}

PlaylistJob::~PlaylistJob() {
  if (thread_.joinable()) thread_.join();
}

void PlaylistJob::start(std::string listId) {
  if (running_.load()) return;
  if (thread_.joinable()) thread_.join();
  running_.store(true);
  done_.store(false);
  result_ = PlaylistResult();
  thread_ = std::thread([this, listId]() {
    PlaylistResult r = fetchPlaylist(listId);
    result_ = std::move(r);
    done_.store(true);
    running_.store(false);
  });
}

bool PlaylistJob::take(PlaylistResult* out) {
  if (!done_.load()) return false;
  if (out) *out = result_;
  done_.store(false);
  return true;
}

} // namespace lvs
