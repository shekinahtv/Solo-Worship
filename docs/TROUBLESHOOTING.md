# Troubleshooting

Start with `%APPDATA%\SoloWorshiper\startup.log`. Every launch appends what it
did and which step failed, including the engine's negotiated format or the reason
the device could not be opened.

## The application will not start

The window opens, then a message box says the application could not start.

| Log line | Meaning | Fix |
| --- | --- | --- |
| `RegisterClassExW failed` | another instance of the class exists in this desktop session | close all instances, run again |
| `CreateWindowExW failed (err …)` | window creation aborted | check the log's error code; a non-zero code is a Win32 error |
| `Direct2D render target creation failed` | the GPU/remote-desktop render target could not be created | update the display driver; on Remote Desktop, run from the console session |

## Status bar says the device failed

* `errorText` in the status bar carries the engine's message. `0x8889000F`
  (`AUDCLNT_E_DEVICE_INVALIDATED`) means the endpoint was removed or disabled —
  check Windows Sound settings, then wait two seconds; the engine retries.
* `ASIOInit` failure: the interface is in use by another ASIO application (a DAW,
  Guitar Rig, another instance of this console). ASIO is exclusive per driver —
  close the other program.
* Wrong sample rate: the Focusrite ASIO driver only accepts what its control panel
  says. Open the Focusrite Control panel, set 48 kHz, then `APPLY` in Settings.

## No microphone signal

1. Is the mic on input 1? The console reads `INPUT 1` unless changed; the Scarlett's
   input 1 is the XLR on the left.
2. Phantom power: condenser microphones need `48V` on the interface. The console
   cannot switch it — use the hardware button.
3. Gain: raise `GAIN` in the preamp section (not `OUT`). The meter sits after the
   preamp, so a flat meter with the hardware gain at zero is expected.
4. Is the microphone muted by the operating system? Windows 11 has a global mute
   hotkey (`Win+Alt+K`) that also affects capture endpoints.

## The video shows "error 150" or another player error

YouTube refused the embed. `error 150` means the video owner disabled playback on
other sites — many karaoke channels do this. The player page states the error number
and links to the video on YouTube. Pick a different upload; the console cannot
override the owner's choice.

## Karaoke audio is silent but the video plays

The `MUSIC CHANNEL` meter is the diagnostic. If it stays at −60 dB:

* The loopback endpoint is not the device the browser is actually playing to.
  Windows plays to the **default** render device; the console captures whatever
  endpoint is selected in Settings. Set them to the same device, or change the
  Windows default.
* Another application has the endpoint in exclusive mode (`Allow applications to
  take exclusive control` in the device's advanced properties). Either clear that
  option or close the other application.
* The device is protected-content capable and refuses loopback; the console gets
  silence. Use a different render endpoint.
* The karaoke capture is pinned to a device nothing is playing on. Open Settings and
  set `KARAOKE CAPTURE (LOOPBACK)` back to `System default (follow Windows)`.

## The music fader does not change the volume

The fader drives the player's Windows audio session, so the log is the place to
look: `music: fader drives the Windows player session` followed by
`music: windows player volume 62%` means it is working, and the percentage must
change when you move the fader.

* `music: no player session - music mixed through the bus` means the WebView2
audio session was not found. Load a video first: the session only exists once the
player has produced audio. If it still does not appear, the browser's audio is on a
different endpoint than the Windows default (per-app output routing) and the fader
falls back to scaling the captured copy only.
* The Windows volume mixer can also move that session. Whatever it says after the
console's last update wins until the fader is touched again.
* `MUSIC` never boosts above unity in session mode; the fader's top is 0 dB. Use the
  interface's own monitor level for more.
* The mode is shown on the chip beside `MUSIC CHANNEL`: `MUSIC -> WINDOWS` (the
  song is heard from the player, the strip's EQ/pan go to the recording) or
  `MUSIC -> BUS` (the whole strip is live). If clicking the chip shows `Same device:
  bus copy would double`, karaoke and the console output are the same endpoint and
  bus monitoring is not available — pin the karaoke capture to another device in
  Settings first.
* A single failed session lookup no longer flips the mode: it takes six consecutive
  misses (about 2.4 s) to fall back to the bus. If the log shows repeated
  `monitor bus carries the music` lines during a track change, the browser is
  restarting its audio session; playback continues, but report it with the log.

## The music sounds pitch-shifted or crackly

Clock drift between the ASIO device and the loopback endpoint. The drift controller
should handle it silently; if it is audible, the two devices are running at very
different rates (for example 44.1 kHz system and 48 kHz session on a device that
resamples badly). Set the Windows device to 48 kHz to match the session.

## Feedback

Expected on a 2-in/2-out interface with speakers. Use headphones, or route the
karaoke to a different device than the one the microphone monitors. See
`ROUTING.md`.

## Pasting a link does nothing

Click the field once (it highlights), then use `Ctrl+V`, `Shift+Insert`, or the
`PASTE` button next to `LOAD`. Copied links usually carry surrounding spaces or a
trailing newline; those are stripped, and a pasted link replaces the field instead
of being appended to it. If the clipboard holds no text the status line says so.

## The karaoke panel is black with "NO VIDEO LOADED"

That is the idle state. Paste a link and press `LOAD`. If `LOAD` reports an error,
check the link contains an 11-character video id (`youtube.com/watch?v=XXXXXXXXXXX`,
`youtu.be/XXXXXXXXXXX`); the parser accepts both plus `/shorts/` and `/embed/`.

If a video never appears, WebView2 may be missing:
`Settings → Apps → Microsoft Edge WebView2 Runtime`, or install the Evergreen
bootstrapper. The rest of the console works without it.

## Recording problems

* The folder does not exist: the console creates it on the first recording, but if
  it was deleted while running, press `OPEN RECORDINGS FOLDER` once — that recreates
  it.
* A take is missing its end: the WAV header is written when recording stops. After a
  crash the audio data is present but the header claims zero length; a WAV repair
  tool can rebuild it.
* Disk full: recording stops and the status bar reports it. Nothing is lost up to
  that point beyond the final buffer.

## Latency is higher than shown

The displayed round trip is estimated from the driver. The real figure includes the
converter latency, the operating system's scheduling and the loopback device's own
buffer. Measure it as described in `TESTING.md` before concluding anything.

If WASAPI is in use, check the reported buffer in the status bar: the Focusrite WDM
driver grants 3528 frames (80 ms) in exclusive mode. That is a driver limitation,
not a setting this application can override — install the ASIO driver and switch
back to ASIO.

## The UI looks wrong at 125 %/150 % scaling

The application is per-monitor DPI aware v2 and re-lays out on `WM_DPICHANGED`.
Drag the window to the other monitor and back to force a relayout. If text is
blurry, the window was created on a monitor whose scaling was changed after launch;
restarting picks up the new scale.
