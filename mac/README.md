# SOLO WORSHIPER for macOS

The console's queue, karaoke player and live vocal monitor, rebuilt as a native
AppKit/SwiftUI application. Universal source: the same tree builds for Apple
silicon and Intel Macs, and it links nothing beyond the system frameworks
(SwiftUI, AVFoundation, CoreAudio, WebKit).

Bundle id: `com.soloworshiper.mac` · version 1.0.0 · macOS 13 or newer

## What works

| Area | Behaviour |
| --- | --- |
| Devices | Every audio device CoreAudio has loaded is listed for input and for output: the built-in output, an onboard codec, a USB interface such as a Focusrite, virtual devices. The system default is preselected; picking another device reopens the microphone path on it. |
| Queue | One ordered queue for both sources, current song highlighted, `UP NEXT` shown, previous / next / jump by clicking a row, auto-advance at the end of a song. |
| YouTube video link | Plays in the official YouTube embed inside a `WKWebView`. |
| YouTube playlist link | The app reads the playlist's public item list (titles and lengths) and fills the queue; previous / next / jump move through it, each song plays in the embed. |
| Local files | `ADD FILES` opens the system picker (audio and video, multi-select). Files join the same queue and play through the app's own `AVPlayer`, with auto-advance. |
| Microphone | `MIC` starts monitoring through a real DSP chain: 80 Hz high pass → three-band EQ (150 Hz shelf, 1.2 kHz bell, 6 kHz shelf) → compressor with makeup → reverb send → output volume → soft limiter, with a live input meter and trim / EQ / reverb / volume controls. |

## What it does not do (on purpose)

* **No mixing of another application's audio into the microphone path.** macOS
  gives no application access to another app's or the system mixer's audio, so
  the karaoke track is heard straight from the system and the microphone monitor
  is a separate signal. The desktop console's `MUSIC -> WINDOWS` split has no
  macOS equivalent.
* **No recording yet** on this platform. The Windows console records 24-bit /
  float WAV; this build does not, and no button pretends it does.
* No ASIO, no 2-in/2-out routing. Every device is exposed through CoreAudio, so
  latency is whatever the selected device grants.
* Use headphones: monitoring an open microphone through the speakers is a
  feedback loop by construction.

## Build

Needs the Xcode Command Line Tools (`xcode-select --install`); a full Xcode
install is not required, and there is no project file to keep in sync.

```bash
cd mac
./build.sh                       # -> build/SoloWorshiper.app
open build/SoloWorshiper.app
```

The binary is compiled for the host architecture (`uname -m`) with a macOS 13
deployment target and signed ad-hoc. To install, copy the app to
`/Applications`. Because the signature is ad-hoc, the first launch on another
Mac needs Finder → right-click → **Open** (or System Settings → Privacy &
Security → **Open Anyway**).

To make a distributable disk image:

```bash
cd mac
./build.sh
hdiutil create -volname "Solo Worshiper" -srcfolder build/SoloWorshiper.app \
        -ov -format UDZO build/SoloWorshiper-1.0.0.dmg
```

## Permissions

The first time `MIC` is pressed macOS asks for microphone access
(`NSMicrophoneUsageDescription` is in `Info.plist`). If it was denied, re-enable
it in System Settings → Privacy & Security → Microphone.

## Verify it yourself

On a Mac with the app built:

1. Launch it. The `IN` and `OUT` pickers list every device; the system default
   is already selected.
2. Paste a playlist link → the song list appears with lengths and the first
   song plays in the embed.
3. `NEXT` → the second row becomes current and `UP NEXT` moves down one; click
   any row → that song plays and becomes current.
4. `ADD FILES` → pick a song → it is appended to the same queue and plays
   through the app's own player.
5. Pick your interface in `OUT` (e.g. a Focusrite), press `MIC`, speak: the
   meter moves and the processed voice is audible in the headphones.
6. Switch `OUT` while the microphone is running: the monitor reopens on the new
   device instead of going silent.

## Source layout

```
mac/build.sh                     swiftc build into a .app bundle
mac/SoloWorshiper/
  SoloWorshiperApp.swift         app entry point and window
  ConsoleView.swift              console layout (header, sources, stage, queue, mic panel)
  ConsoleModel.swift             queue, transport, mic controls, device selection
  Queue.swift                    the shared ordered queue
  YouTubePlayer.swift            WKWebView embed bridge, AVPlayerLayer host
  PlaylistClient.swift           playlist listing and link parsing
  MicEngine.swift                AVAudioEngine duplex monitor and the DSP chain
  AudioDevices.swift             CoreAudio enumeration and device selection
  player.html                    the official embed page
  Info.plist                     bundle metadata and microphone usage string
```

## Building a .dmg without a Mac

`.github/workflows/macos-dmg.yml` runs on a `macos-15` runner, so the `.dmg` can be
produced from this Windows repository. It builds both architectures, combines them
with `lipo`, ad-hoc signs the bundle, packages the image with `./package_dmg.sh`
and uploads `SoloWorshiper-1.0.0.dmg` plus its SHA-256 as the artifact
`SoloWorshiper-macos-dmg`.

Trigger it either way:

```bash
# from the GitHub web UI: Actions -> macOS DMG -> Run workflow
git tag v1.0.0 && git push origin v1.0.0      # or push a tag
```

Locally on a Mac the same two steps work:

```bash
./build.sh                                   # build/SoloWorshiper.app
./package_dmg.sh                             # build/SoloWorshiper-1.0.0.dmg
```

The bundle is signed ad-hoc (`codesign --sign -`), which is enough to launch and to
answer the microphone prompt, but it is **not** notarised: on another Mac the first
launch needs right-click → Open, or `xattr -dr com.apple.quarantine` on the copied
app. Removing that warning needs an Apple Developer ID certificate and
`notarytool`, which this workflow does not have.

## Status of this build

The sources and build script are complete and self-consistent, but they were
written on a Windows machine: the app has **not** been compiled or run yet. Run
`./build.sh` on a Mac once and fix anything the compiler reports; the verification
list above is the acceptance test.
