# SOLO WORSHIPER for Android (phones and tablets)

The console's queue, karaoke player and live vocal monitor, rebuilt as a native
Android application. Kotlin is not used; the app is plain Java on the Android
framework with Media3 for local playback, so it builds with the Android Gradle
Plugin alone.

Package: `com.soloworshiper.app` · version 1.0.0 · minSdk 26 · targetSdk 36

## What works

| Area | Behaviour |
| --- | --- |
| Queue | One ordered queue for both sources, with the current song highlighted and `UP NEXT` shown. Previous / next / jump by tapping a row. |
| YouTube video link | Plays in the official YouTube embed inside the app. Audio comes from the phone's own mixer. |
| YouTube playlist link | The app reads the playlist's public item list (titles and lengths) and fills the queue; previous / next / jump move through it and each song plays in the embed. Auto-advances when a song ends. |
| Local files | `ADD FILES` opens the system picker (audio and video, multi-select). Files are appended to the same queue and played by the app's own Media3 player, with auto-advance and persistent URI permissions. |
| Microphone | `MIC` starts monitoring through a real DSP chain: 80 Hz high pass → 3-band EQ (150 Hz shelf, 1.2 kHz bell, 6 kHz shelf) → compressor with makeup → reverb send → output volume → soft limiter. 48 kHz float, `PERFORMANCE_MODE_LOW_LATENCY`, with a live input meter. |

## What it does not do (on purpose)

* **No mixing of the phone's playback into the microphone path.** Android gives no
  application access to another app's or the system mixer's audio, so the karaoke
  track is heard straight from the phone and the microphone monitor is a separate
  signal. The desktop console's `MUSIC -> WINDOWS` split has no Android equivalent.
* **No recording yet** on this platform. The desktop console records 24-bit / float
  WAV; the phone build does not, and no button pretends it does.
* **No ASIO, no 2-in/2-out routing.** This is the phone's own audio path; latency is
  whatever the device grants (typically tens of milliseconds).
* Use headphones: monitoring an open microphone through the phone's speaker is a
  feedback loop by construction.

## Build

The project is `android/` inside this repository. It needs a JDK and the Android
SDK (platform 36, build-tools 36). Gradle comes from the wrapper distribution
already present on the development machine.

```bash
export JAVA_HOME="/c/Program Files/Android/Android Studio/jbr"
GRADLE="$USERPROFILE/.gradle/wrapper/dists/gradle-9.8.2-bin/*/gradle-9.8.2/bin/gradle.bat"
"$GRADLE" -p android assembleDebug
```

The APK lands at `android/app/build/outputs/apk/debug/app-debug.apk`. The build
that ships in `dist/` is that same file, copied to
`dist/SoloWorshiper-Android-1.0.0.apk`.

`local.properties` points at the SDK on this machine; regenerate it on another
machine with Android Studio or by setting `sdk.dir`.

## Install on a phone or tablet

1. Copy `SoloWorshiper-Android-1.0.0.apk` to the device and open it (allow
   "install unknown apps" for the file manager), **or**
2. `adb install -r SoloWorshiper-Android-1.0.0.apk`

The APK is signed with the standard Android debug key, which is what sideloading
needs. A store release would need your own signing key.

On first use the microphone button asks for the `RECORD_AUDIO` permission; the
karaoke player needs network access.

## Verify it yourself

1. Paste a playlist link → the song list appears with lengths; the first song plays.
2. Tap `NEXT` → the second row becomes current and `UP NEXT` moves down one.
3. Tap any row → that song plays and becomes current.
4. `ADD FILES` → pick a song from the device → it is appended to the queue.
5. `MIC` → speak; the meter moves and the processed voice is audible in the
   headphones.

## Source layout

```
android/app/src/main/java/com/soloworshiper/app/
  MainActivity.java    UI, transport, queue wiring, YouTube polling
  QueueModel.java      the shared ordered queue
  MicEngine.java       duplex audio thread and the DSP chain
  PlaylistParser.java  playlist listing (public page, no API key)
android/app/src/main/assets/player.html   the official embed page
```
