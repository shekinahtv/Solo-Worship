# Architecture

## Process and threading

```
UI thread (STA, COM)
 ├─ Win32 message loop, WM_TIMER at 16 ms (also pushes the player session volume)
 ├─ Direct2D / DirectWrite rendering (single HwndRenderTarget)
 ├─ WebView2 controller and browser process callbacks
 └─ reads Engine stats through atomics only

ASIO thread (owned by the driver, MTA)
 └─ Driver::bufferSwitch() → Engine::asioLoop() → DSP → output buffers

WASAPI render thread (exclusive / shared, MTA)
 ├─ wait on render event (or spin-poll in exclusive mode)
 ├─ Engine::pullMusic() → loopback capture → resample → mix
 └─ Engine::process() → DSP → render

WASAPI capture thread (MTA)
 └─ mic input → ring buffer → consumed by the render callback

Recorder (opened per take, written from the render callback)
```

There are no locks and no allocations in the audio path. Every cross-thread value
is an `std::atomic` (levels, gain reduction, CPU load, counters, device state),
every parameter is a smoothed atomic in `ParamStore`, and all DSP state is
preallocated in `Engine::Start`.

## Parameters

`src/core/Params.h` is one X-macro table. Each row declares a section, a name, a
JSON key, a display label, minimum, maximum, default and unit. From that table come
the `enum class P`, the `ParamStore` accessors, preset serialisation, A/B compare
and the UI's text formatting. Adding a control means adding a row, not touching the
preset format.

Every parameter is a `std::atomic<float>` read once per block and smoothed by a
one-pole (5–20 ms depending on the parameter) before use, so knob moves never click.
Boolean parameters (enable switches, mutes) are read unsmoothed at block rate.

## Signal chain, in the order it runs

```
mic in ─ input gain/trim ─ preamp (drive → warmth → saturation) ─ phase ─ HPF
      ─ 4-band British EQ ─ compressor (with parallel mix) ─ reverb send/wet
      ─ mic fader ─ mic pan ─┐
                             ├─ master bus ─ balance ─ bus comp ─ limiter ─ out
loopback music ─ resample ─ music EQ ─ music fader ─ music pan ─┘
                             └───────────────┴─ meters, recorder tap
```

The reverb is an independent bus: the dry vocal and the reverb return are summed,
so `WET` is a true effect level rather than a dry/wet crossfade of the same signal.
Solo is exclusive on the bus it belongs to; mute wins over solo.

One branch is conditional. When `Engine::musicDirect()` is set, the embedded
player's Windows session volume is under fader control, the song is already audible
from Windows, and the captured copy is left out of the monitor bus to avoid a
doubled, ~16 ms-delayed copy. The copy still reaches the meter and the recorder.
`audio/SessionVolume.cpp` implements the session lookup (default render endpoint →
`IAudioSessionManager2` → sessions whose process descends from this application and
is a WebView2 binary) and is called from the UI timer, never from the audio thread.

## Buffers

* Internal processing is float32, 2 channels, `bufferFrames` (default 128) per block.
* The loopback path runs at the render endpoint's own rate (usually 44.1 kHz) and is
  converted to the session rate with a fractional-delay interpolator. The read
  position is steered by the fill level of the capture ring, so a clock mismatch
  between the interface and the render endpoint is corrected continuously rather
  than by dropping a whole block (which would be an audible click).
* `pullMusic()` never blocks: if the loopback ring is empty it returns silence and
  the drift controller eases the read pointer forward.

## Latency and dropout accounting

`EngineStats` publishes what actually happened: negotiated rate and buffer size,
driver-reported ASIO latencies, stream latency for WASAPI, buffer-period latency,
CPU load as a fraction of the block budget, and counters for dropouts (a block that
arrived later than its deadline) and underruns (a render that had no data ready).
The UI shows those counters instead of a placeholder.

A dropout is counted when the ASIO callback observes a gap larger than
1.5 buffers since the previous one; an underrun when a WASAPI render pass finds
fewer frames than requested. Both are monotonic for the session so a problem that
happened during a take is still visible afterwards.

## Device changes

The engine polls the endpoint list every 2 s on a timer thread. If the configured
device disappears it stops the stream, marks the status as disconnected and retries
the same configuration; when the device returns the stream is reopened with the
same settings and the counters continue. The ASIO path does the same through
`ASIOInit`/`ASIOCreateBuffers` on the same driver CLSID.

## UI rendering

`ui::Gfx` wraps one `ID2D1HwndRenderTarget` plus a DirectWrite factory and a small
font ramp (`F_TINY` 10 px … `F_MED` 18 px). Widgets are immediate-mode functions
that draw and return; hit regions are registered by the layout code into a vector
that the pointer handlers search in reverse order (last drawn wins). Popups and
overlays are drawn after the panels so they sit above them, and their hit ids live
in a separate range so a click can never fall through to a control underneath.

The console relayouts from the client rectangle every frame from `WM_SIZE`, using
the DPI-scaled surface size, so 1280×720, 1920×1080 and fractional desktop scaling
all produce a coherent layout. Touch targets are at least 28 px tall at 100 % DPI
and the pointer path handles `WM_POINTERDOWN`/`WM_POINTERUPDATE` as well as mouse
messages, so pen and touch drags feel the same as a mouse drag.
