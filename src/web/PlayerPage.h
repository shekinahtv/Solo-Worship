#pragma once
// ---------------------------------------------------------------------------
// The page that hosts the official YouTube player.
//
// YouTube refuses a bare navigation to https://www.youtube.com/embed/<id> with
// "Video player configuration error 153" because that request carries no origin
// and no referer. Serving this page from the WebView2 virtual host
// https://lvs.local gives the iframe a real HTTPS origin, and the IFrame API
// then lets our own same-origin script control playback.
//
// The page is written to disk once at startup; nothing is downloaded.
// ---------------------------------------------------------------------------
namespace lvs {

inline const char* kPlayerHtml = R"HTML(<!doctype html>
<html><head><meta charset="utf-8">
<title>SOLO WORSHIPER</title>
<style>
  html, body { margin:0; height:100%; background:#08090B; overflow:hidden; }
  #player { position:absolute; inset:0; width:100%; height:100%; }
  #fallback { position:absolute; inset:0; display:none; align-items:center;
              justify-content:center; color:#6A7380; font:14px 'Segoe UI',sans-serif;
              text-align:center; padding:24px; }
</style>
</head><body>
<div id="player"></div>
<div id="fallback">The YouTube player could not start.<br>Check the internet connection, or
open the link in a browser to confirm the video is available.</div>
<script>
  var params = new URLSearchParams(location.search);
  var videoId = params.get('v') || '';
  var listId = params.get('list') || '';
  var autoplay = params.get('autoplay') !== '0';
  var player = null, ready = false, failed = false;

  function onYouTubeIframeAPIReady() {
    if (!videoId && !listId) return;
    var vars = {
      autoplay: autoplay ? 1 : 0,
      rel: 0,
      modestbranding: 1,
      playsinline: 1,
      origin: location.origin,
      enablejsapi: 1
    };
    // Playlist mode is only used when the console could not read the item list;
    // normally it loads one video at a time from its own queue.
    if (listId) { vars.listType = 'playlist'; vars.list = listId; }
    var options = {
      playerVars: vars,
      events: {
        onReady: function (e) { ready = true; if (autoplay) e.target.playVideo(); },
        onError: function (e) {
          failed = true;
          var url = videoId ? ('https://www.youtube.com/watch?v=' + videoId)
                            : ('https://www.youtube.com/playlist?list=' + listId);
          var f = document.getElementById('fallback');
          f.style.display = 'flex';
          f.innerHTML = 'This video cannot be played in an embedded player (error ' +
                        e.data + ').<br><br>' +
                        '<a style="color:#E5B96F" href="' + url +
                        '" target="_blank">Open it on YouTube</a>';
        }
      }
    };
    if (videoId) options.videoId = videoId;
    player = new YT.Player('player', options);
  }

  var tag = document.createElement('script');
  tag.src = 'https://www.youtube.com/iframe_api';
  tag.onerror = function () {
    var f = document.getElementById('fallback');
    f.style.display = 'flex';
  };
  document.head.appendChild(tag);
</script>
</body></html>)HTML";

} // namespace lvs
