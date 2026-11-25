# Test procedures

## What has been verified on the reference machine

Focusrite Scarlett 2i2 Gen 2, Windows 11, 48 kHz.

| Check | Command / method | Result |
| --- | --- | --- |
| ASIO driver enumerated | `lvs_probe.exe --asiotest` | 2 drivers found, `Focusrite USB ASIO` opens |
| Channel counts | same | 2 in / 2 out, as the hardware has |
| Negotiated rate and buffer | same | 48000 Hz, 128 frames |
| Driver-reported latency | `ASIOGetLatencies()` | in 6.23 ms, out 8.23 ms |
| Callback rate | 3 s run | 371 callbacks/s (expected 375) |
| Dropouts / underruns | 3 s run | 0 / 0 |
| Input signal present | 3 s run | peak −85 dB (converter noise floor, no microphone connected) |
| WDM exclusive buffer | `lvs_latencytest.exe` | 3528 frames = 80 ms (driver refuses smaller) |
| WDM shared buffer | `lvs_latencytest.exe` | 970 frames = 22 ms minimum |
| Exclusive capture format | `lvs_probe.exe --excltest` | `0x88890008` unsupported format — WDM capture stays shared |
| Application start | `build\SoloWorshiper.exe` | window opens, engine starts on ASIO, status bar green |
| UI rendering | screenshot at 1720×1000 | console draws all panels, no clipped controls |
| Link pasting | PASTE button, Ctrl+V | clipboard link replaces the field, stray whitespace/line breaks stripped |
| LOAD without touching the field | press LOAD with the stored link still shown | the stored video loads (the button used to read only the edit buffer and silently fail) |
| YouTube embed | video `kA2gJF-dCKE` (karaoke, embeddable) | player page loads, poster and title shown, transport shows 0:09 / 3:59, PAUSE active |
| Loopback → music bus | same video playing | `MUSIC CHANNEL` meter at about −4 dB, moving between frames 3 s apart |
| Master meters | same | L/R meters show programme level, no clipping |
| EQ PRESET button and popup | click the chip, then an entry | list opens with 5 EQ presets; choosing *Female Voice* moved the bands from 110/430/2.6k/8.5k to 130/520/3.6k/10.5k, gains −2.2/+1.8/+2.2 dB, and the curve followed |
| REV PRESET button and popup | click the chip, then *Worship Hall* | list opens with all 7 reverb presets; the type changed to `HALL` and send/wet/pre-delay/decay/size became 58 % / 46 % / 40 ms / 4.6 s / 72 %, status line `Reverb preset applied: Worship Hall` |
| MUSIC fader → Windows session | play the embed, move the fader | log went `music: windows player volume 62%` → `41%` → `31%` → `20%` and the player's Windows session volume followed, so the song level changed at the source instead of only on a copy |
| Doubling suppressed | same video playing | status bar reads `MUSIC FADER -> WINDOWS PLAYER SESSION`; the captured copy is metered and recorded but not added to the monitor bus, so the song is heard once |
| Music routing is visible | play the embed, look at the strip | chip `MUSIC -> WINDOWS` lit cyan, caption `EQ + PAN APPLY TO THE RECORDING ONLY` in amber, `REC ONLY` beside `EQ IN` |
| Routing switch guard | click the routing chip on this machine (karaoke and output share the device) | toast `Same device: bus copy would double. Route karaoke elsewhere (Settings).` and the mode stays `WINDOWS` |
| Routing switch, bus direction | temporarily pin the karaoke capture to the motherboard output, restart, click the chip | log `music: routed through the console bus (EQ/pan live)`, chip `MUSIC -> BUS` lit amber, caption `EQ + PAN LIVE IN MONITOR AND RECORDING`, no `REC ONLY` tag; clicking again logged `music: routed from the Windows player session`. The setting was restored afterwards |
| Mode stability | leave the embed playing for 12 s untouched | exactly one `music: fader drives the Windows player session` line and one `windows player volume` line; no bouncing between modes |
| Rename and data migration | launch the rebuilt binary | title bar and console header read `SOLO WORSHIPER`; `%APPDATA%\SoloWorshiper\settings.json` exists with the previous device configuration carried over |

Videos whose owner disabled embedding (for example the Sing King karaoke channel)
play as `error 150` inside the frame; that is YouTube's decision, not a console
fault, and the player page says so with a link to open it on YouTube.

## What has not been verified

Stated plainly so nothing is overclaimed:

* **Acoustic round-trip latency.** The 17.1 ms figure is the driver's numbers plus
  one buffer period. It is not a measured loopback.
* **What the karaoke audio *sounds* like.** The meters prove the loopback path
  carries programme material, but the result was not monitored on headphones or
  speakers during the test, so nothing is claimed about its quality, level
  matching or drift over a long song. The session-volume fix is verified by the
  volume percentages in the log, not by ear.
* **Reverb as heard.** The preset values are verified in the interface and the log;
  the acoustic result was not listened to.
* **Bus monitoring (`MUSIC -> BUS`).** The switch and its guard are verified; the
  switch was not exercised because this machine plays karaoke on the same device it
  monitors on, which the guard correctly refuses.
* **Mic gain at the new +36 dB top.** The range is in the parameter table and the
  knob renders it; the noise floor at the top of that range was not measured.
* **Recording to disk** was implemented and the WAV writer is unit-exercised, but no
  recording was made with a live microphone on this machine.
* **Touch input.** The pointer path is implemented; it has not been exercised on a
  touchscreen.

## Repeat the audio checks

```bat
cd build
lvs_probe.exe --list          :: endpoints and default devices
lvs_probe.exe --asiotest      :: ASIO open, latency, callback rate, dropouts
lvs_probe.exe --rate 48000 --buffer 128
lvs_probe.exe --shared        :: force the WASAPI shared path
lvs_probe.exe --excltest      :: exclusive-mode format negotiation
lvs_latencytest.exe           :: what buffer each driver actually grants
```

`lvs_probe` prints at 1 s intervals and exits after the run; a non-zero exit means
the device could not be opened.

## Test checklist for a full pass

1. **Startup** — launch with no interface connected. The status bar turns red with
   the device error; the console still renders. Plug the interface in; within ~2 s
   the status bar goes green and reports the rate/buffer.
2. **Vocal chain** — speak into input 1. The mic meter moves. Enable HPF and lift
   `LOW`: the EQ curve follows and the sound thins. Push `DRIVE` to 100 %: harmonic
   content rises audibly. Set the compressor ratio to 8:1 and threshold −30 dB: the
   GR meter shows reduction and the level evens out.
3. **Reverb** — `SEND` and `WET` up, `DECAY` 4 s. The tail continues after the
   phrase; `PRE-DLY` above 40 ms separates the tail from the word.
4. **Karaoke** — paste a link, `LOAD`, play. The `MUSIC CHANNEL` meter must move;
   if it stays at −60 dB the loopback endpoint is wrong (see ROUTING.md).
5. **Master** — move `MIC`, `MUSIC`, `MASTER`; watch the L/R meters and the limiter
   gain-reduction bar. `MUTE`/`SOLO` must mute and isolate as labelled.
6. **Presets** — step through the vocal presets with `<` / `>`; every knob and
   switch must move. `SAVE` names and stores a preset; it survives a restart. `A`/`B`
   swaps and swaps back with the values intact.
7. **Recording** — press `REC`, sing, press `STOP`. The WAV appears in the folder
   shown in the panel and plays in any player at the selected bit depth.
8. **Resilience** — unplug the interface during playback: playback stops cleanly,
   the status bar reports it, replugging resumes. No crash, no stuck UI.
9. **Layout** — resize to 1280×720 and to a maximised 1920×1080; no control may
   overlap and everything must stay reachable. Try 125 % and 150 % desktop scaling.

## Latency measurement (if you want a real round trip)

The honest way to measure the acoustic round trip:

1. Connect interface output 1 to input 1 with a cable (or point a microphone at the
   speaker, which adds ~1 ms per metre).
2. In a DAW or with `lvs_latencytest.exe --impulse`, play a click and record the
   return.
3. The delay between the emitted and recorded impulse is the real round trip; compare
   it with the estimate in the title bar.

A 128-frame ASIO buffer at 48 kHz cannot measure below 2.67 ms, so differences
smaller than one buffer are meaningless.

## Playlist queue (Windows, verified 2026-11-25)

Loaded the nine-song playlist `PLorXF1_P-vygjA2bG5cKST4o8JEUNBIqD` from the console:

```
ui: pasted 72 chars into the link field
queue: loading playlist PLorXF1_P-vygjA2bG5cKST4o8JEUNBIqD
queue: playlist ready with 9 item(s)
queue: playing 1/9 vTmGj5PxzFY
queue: playing 2/9 UDHmcGldHyo        <- NEXT
queue: playing 3/9 wX-twEAzV_k        <- NEXT again
```

The `PLAYLIST` chip opens the queue overlay with the title, `1 of 9`, every row with
its length, the current row highlighted, and `PREVIOUS` / `NEXT`. Clicking a row
jumps to it. Listing needs the desktop User-Agent header: the mobile listing page is
a shell with no item data, which is why an earlier build reported
`playlist listing failed`.

## Android build (verified on the emulator)

Checked on the API 37 x86_64 emulator (`Medium_Phone_API_37.0`), not on a physical
device:

* installs and launches with no crash; `MainActivity` reaches the resumed state
* a pasted playlist link produced all nine songs with titles and lengths, the
  current row highlighted and `UP NEXT` filled
* `NEXT` moved to song 2 and a tap on row 4 jumped to song 4 (confirmed from the
  status line and the `UP NEXT` line)
* the YouTube embed played inside the WebView (audio focus taken from the app, real
  title adopted from the player)
* a pushed 6-second WAV selected through the system picker was queued and played by
  Media3: the audio track delivered exactly 288000 frames at 48 kHz

Not verified for Android: any physical phone or tablet, the microphone chain (the
emulator exposes no audio input), a USB or Bluetooth interface, and the embed on a
device without Google Play services.
