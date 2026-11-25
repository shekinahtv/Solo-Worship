#pragma once
// ---------------------------------------------------------------------------
// Official YouTube embed hosted in WebView2.
//
// The application never downloads or extracts media: it loads the standard
// https://www.youtube.com/embed/<id> page and reads only playback state. The
// audio is picked up for mixing through WASAPI loopback (see docs/ROUTING.md).
// ---------------------------------------------------------------------------
#include <windows.h>

#include <string>

namespace lvs {

class YouTubeHost {
public:
  ~YouTubeHost();

  bool create(HWND parent);
  void destroy();
  bool available() const { return controller_ != nullptr; }

  void setBounds(RECT px);
  void setVisible(bool visible);
  void focus();

  void loadVideo(const std::string& id, bool autoplay);
  // Play the official embed's own playlist view (used only when the item list
  // could not be read, so previous/next fall back to the player's own order).
  void loadPlaylist(const std::string& listId, bool autoplay);
  void play();
  void pause();
  void seekTo(double seconds);
  void seekBy(double seconds);
  void nextVideo();
  void previousVideo();

  void poll(); // request position / duration / title from the page

  double position() const { return position_; }
  double duration() const { return duration_; }
  bool playing() const { return playing_; }
  int state() const { return state_; }
  bool ended() const { return loaded() && state_ == 0; }
  bool loaded() const { return !videoId_.empty() || !playlistId_.empty(); }
  bool hasPlaylist() const { return !playlistId_.empty(); }
  const std::string& title() const { return title_; }
  const std::string& videoId() const { return videoId_; }
  const std::string& lastError() const { return error_; }

  static std::string extractVideoId(const std::string& url);
  static std::string extractPlaylistId(const std::string& url);

private:
  void navigate();
  void eval(const std::string& js);

  void* controller_ = nullptr;
  void* webview_ = nullptr;
  void* environment_ = nullptr;
  HWND parent_ = nullptr;
  RECT bounds_ = {};
  bool visible_ = false;

  std::string videoId_;
  std::string playlistId_;
  std::string title_;
  std::string error_;
  double position_ = 0.0;
  double duration_ = 0.0;
  bool playing_ = false;
  int state_ = -1;
  bool autoplay_ = true;
};

} // namespace lvs
