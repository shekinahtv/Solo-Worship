#pragma once
// ---------------------------------------------------------------------------
// YouTube playlist listing.
//
// The IFrame API deliberately does not expose the queue, so the ordered list is
// read from the public playlist page's own metadata (video ids, titles, lengths).
// Nothing is downloaded: only the HTML of the playlist page is fetched, with the
// same public information a browser shows. Audio still comes from the official
// embed. If the page changes shape the fetch reports failure and the caller falls
// back to the embed's own playlist playback.
// ---------------------------------------------------------------------------
#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace lvs {

struct PlaylistEntry {
  std::string videoId;
  std::string title;
  std::string duration;
};

struct PlaylistResult {
  bool ok = false;
  std::string listId;
  std::wstring name;
  std::vector<PlaylistEntry> entries;
  std::wstring error;
};

// Blocking. Call from a worker thread only.
PlaylistResult fetchPlaylist(const std::string& listId);

// Runs one fetch on a worker thread so the UI never blocks on the network.
class PlaylistJob {
public:
  ~PlaylistJob();
  void start(std::string listId);
  bool running() const { return running_.load(); }
  // True once, when a result from the last start() is available.
  bool take(PlaylistResult* out);

private:
  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<bool> done_{false};
  PlaylistResult result_;
};

} // namespace lvs
