# Audio routing

## The chain on a 2-in / 2-out interface

```
 Windows audio
 ┌───────────────────────────────┐
 │ YouTube embed (WebView2)      │          ┌──────────────────────────┐
 │   ↓ plays to the default      │          │                          │
 │     render endpoint           │          │   SOLO WORSHIPER      │
 │   ↓                           │  WASAPI  │                          │
 │ Render endpoint ─ loopback ───┼─────────▶│ music bus → music EQ     │
 │                               │  capture │            ↓             │
 └───────────────────────────────┘          │      master bus          │
                                            │            ↑             │
 Microphone (Analogue 1) ── ASIO capture ──▶│ vocal chain ── mic bus   │
                                            │            ↓             │
                                            │  output 1/2 (ASIO)       │──▶ Scarlett outputs
                                            └──────────────────────────┘
```

Both the vocal and the karaoke audio leave through outputs 1/2. That is the whole
hardware: two analogue inputs, two analogue outputs. The console does not expose
phantom cue or monitor buses, because the interface cannot send them.

## The feedback problem, and the answer

On a 2-in/2-out interface the microphone hears what the outputs play. Karaoke
through speakers plus an open vocal microphone is a loop with gain, and it will howl.

Three defences are built in:

1. **The status bar warns** when the loopback endpoint is the same device the
   console is rendering to (`FEEDBACK RISK` message).
2. **Headphones are the intended monitoring path.** Settings offers the Focusrite
   as the console output and any *other* device as the karaoke render endpoint, so
   the room speakers carry only the vocal, and the singer hears the mix.
3. **A brickwall limiter sits on the master bus** and is on by default, so a
   runaway loop is limited rather than instantly destructive.

## Recommended setups

### A. Headphones on the interface (best latency, no feedback)

| Setting | Value |
| --- | --- |
| Console output | `Focusrite USB ASIO`, 48 kHz, 128 frames |
| Karaoke render endpoint | the Focusrite as well (system default) |
| Monitoring | headphones plugged into the interface |

Everything happens on the interface: karaoke plays to the Focusrite, the loopback
capture takes it back in software (no cable), the vocal and the music are mixed and
sent to outputs 1/2 which are the headphones. 17 ms end to end.

### B. Speakers for the room, headphones for the singer

| Setting | Value |
| --- | --- |
| Console output | `Focusrite USB ASIO` |
| Karaoke render endpoint | the motherboard / HDMI device feeding the room |

The karaoke goes out of the room speakers through the non-Focusrite device, the
loopback captures it there, and only the mixed result reaches the Focusrite — so
the singer hears everything and the room hears the backing track from a different
path. Feedback risk remains for the vocal itself: keep the microphone out of the
speaker's direct field, or mute the vocal in the room.

### C. No ASIO driver installed

Select WASAPI in Settings. Expect 22 ms shared or 80 ms exclusive on the Focusrite
WDM driver, or 10–20 ms exclusive on motherboards that honour small buffers. The
console measures and reports whichever it actually got; it does not display the
requested value.

## How loopback capture works here

By default the source follows the Windows default render endpoint — the device the
browser actually plays to — and re-resolves it whenever it changes. Pinning a
specific device is possible in Settings, but then the console captures exactly what
plays on that device and nothing else. `System default (follow Windows)` is the
entry to switch back, and the startup log records which endpoint is in use.

The capture ring keeps 512–2048 frames (about 12–42 ms, default ~16 ms) of music
buffered; the older quarter-buffer target held the song ~85 ms behind the picture.

`AudioClient` in loopback mode is opened on the **render** endpoint with
`AUDCLNT_STREAMFLAGS_LOOPBACK`. The endpoint's mix format (usually 44.1 kHz float
stereo) is captured into a ring buffer and resampled to the session rate with a
fractional-delay interpolator. Because the loopback clock and the ASIO clock are
independent, the ring is drift-steered: the read pointer is nudged by a few
samples per block based on the fill level, which keeps latency constant without
audible artefacts.

If the render endpoint disappears (device unplugged, format change), the capture is
torn down and recreated; the music bus outputs silence meanwhile and the status bar
reports the state. The vocal path is never interrupted by this.

## Where the MUSIC fader acts

The browser plays through Windows, not through the console's render callback. So a
fader that scaled only the captured copy would leave the original Windows playback
at full level — the song would not get quieter no matter what the fader said. That
was a real bug and it is fixed in two parts:

1. `MUSIC` (and `MUTE`, `SOLO`) drive the **Windows audio session** of the
   WebView2 processes that belong to this application (`audio/SessionVolume.cpp`
   finds them by walking the process tree, so a normal Edge window is never
   touched). Pressing the fader changes what the headphones actually receive, and
   the log records `music: windows player volume 41%` each time it changes.
2. While that session control is active, the console does **not** add the captured
   copy to the monitor bus — otherwise the song would be heard twice, once about
   16 ms late. The copy still feeds the `MUSIC CHANNEL` meter and the master
   recording tap, so recordings contain the song at the fader/EQ setting.

If no player session is found (no video loaded yet, or the browser's audio moved to
another process), the console falls back to the earlier behaviour: the captured copy
is mixed into the monitor bus and the fader scales that copy. The status bar states
which mode is live: `MUSIC FADER -> WINDOWS PLAYER SESSION` or the feedback warning.

### The music strip says which mode it is in

The chip beside `MUSIC CHANNEL` reads `MUSIC -> WINDOWS` (cyan) or `MUSIC -> BUS`
(amber), and the caption under the fader says either `EQ + PAN APPLY TO THE
RECORDING ONLY` or `EQ + PAN LIVE IN MONITOR AND RECORDING`, with `REC ONLY` next to
the strip's `EQ IN`. In Windows mode the three music EQ knobs and the music pan are
real controls — but they shape the copy that is metered and recorded, not what the
headphones receive, because the player's output never passes through the console.

Clicking the chip switches to bus monitoring when that is physically sane: karaoke
must be playing on a *different* device from the console output (Settings → karaoke
capture pinned to the room device). In that setup the console's copy is the only
music path, so fader, EQ, pan and master are all live. When karaoke and the console
output are the same device the chip refuses with `Same device: bus copy would
double. Route karaoke elsewhere (Settings).` — a bus copy there would be heard twice,
about 16 ms apart, and muting the player to avoid it would also silence the capture.

### Mode changes have hysteresis

Taking over the session needs two consecutive successful lookups; giving it back
needs six consecutive misses (about 2.4 s). A track change or a re-created audio
session therefore cannot flip the monitor path — and audibly change the music level
— for a single polling interval.

### Karaoke ducks the player, it does not mute it

`KARAOKE` needs the player's audio inside the console, and the only route is the
loopback capture of the endpoint. That capture sees only what a player actually
renders, so muting the session takes the song away from the engine as well: with
the player at 0 % the capture measures 0.0000 and the console is left with nothing
to play and nothing to cut. The session is ducked to 0.2 % (−54 dB) instead, which
keeps it rendering and captured (0.3000 → 0.0006 on the reference machine).

The captured copy is then scaled straight back up. Because that gain would be
catastrophic if the duck had not taken effect, the engine bounds it by the level the
capture actually carries — target peak 0.25 — so a session still sitting at full
volume is played at its own level rather than multiplied hundreds of times. WebView2
opens new audio sessions as it runs and each fresh one starts at full volume, so the
duck is re-applied every 400 ms and every change is logged:

```
karaoke duck: session volume: 1 of 4 player session(s) at 0.2%
```

Switching karaoke off restores the player and returns the monitor to the session in
the same frame. Restoring the volume first and handing the monitor back two ticks
later played the song twice — once from Windows, once from the console — which was
audible as a jump to full volume.

### The vocal cut

The song that reaches the meter, the monitor and the recording tap after `KARAOKE`
passes through a spectral centre canceller (`dsp/Effects.h`, `VocalCut`): 1024-point
STFT, 75 % overlap, per-bin weighting that removes only what is centre-dominant, and
a 180 Hz – 9.5 kHz band limit so the kick and bass line survive along with the
sides. Depth is *Settings → VOCAL CUT DEPTH*; at 100 % the centre goes whatever it
holds and at lower settings only clearly centre-dominant content is taken.

Measured on a real take — 20 s of the same song, cut off against cut on, band by
band on the centre channel:

| band | removed | side kept |
| --- | --- | --- |
| sub / kick 30–90 Hz | 1.0 % | 99.9 % |
| bass 90–250 Hz | 8.5 % | 99.8 % |
| low-mid 250–500 Hz | 75.3 % | 99.8 % |
| vocal 0.5–3 kHz | 84.4 % | 99.8 % |
| presence 3–6 kHz | 96.7 % | 99.7 % |
| air 8–16 kHz | 9.1 % | 99.7 % |

The filter delays the song by one 21 ms frame; the recording alignment adds exactly
that while karaoke is engaged, so the voice stays on the beat.

## What is deliberately not done

* No audio is downloaded, buffered to disk, decoded or extracted from YouTube. The
  browser plays the official embed; the console captures what Windows plays.
* No DRM or content-protection bypass. If an endpoint refuses loopback capture
  (protected content), the console gets silence and says so.
* No virtual cable is installed or required.

The player itself is a page served from the WebView2 virtual host
`https://lvs.local/player.html`. A bare navigation to `youtube.com/embed/<id>` is
refused by YouTube with *error 153* because that request has no origin or referer;
serving the page from a real HTTPS origin fixes it, and the page drives the official
IFrame API for play, pause, seek and title.
