#pragma once
// ---------------------------------------------------------------------------
// Windows audio session control for the embedded player.
//
// The official YouTube embed plays through Windows (WASAPI), not through this
// application's render path, so a fader that only scales the captured copy
// cannot turn the song down: the raw playback stays at full level. This module
// finds the sessions owned by our own WebView2 processes on the default render
// endpoint and drives their volume, which is what the singer actually hears.
// ---------------------------------------------------------------------------
#include <string>

namespace lvs {

// Sets master volume (0..1) and mute on every audio session belonging to this
// process tree's WebView2. Returns true when at least one session was found;
// false means the player is not running (or its audio moved elsewhere) and the
// caller should keep the music in its own mix bus instead.
bool applyPlayerVolume(float volume, bool mute, std::wstring* note);

} // namespace lvs
