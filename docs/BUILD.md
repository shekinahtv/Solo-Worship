# Building

## Toolchain

Verified on this machine:

| Component | Version used |
| --- | --- |
| Visual Studio Build Tools | 18 (MSVC 14.51.36231) — any 2019/2022/2026 toolset with C++20 works |
| Windows SDK | 10.0.26100 |
| CMake | 3.20 or newer |
| Ninja | any |
| MSVC runtime | static (`/MT`) — the shipped exe needs no VC++ redistributable |

Both CMake and Ninja can come from the VS installation; `tools\build.bat` finds
them through `vcvars64.bat` and falling back to the bundled copies.

## Dependencies

| Dependency | Where it comes from | Notes |
| --- | --- | --- |
| Direct2D, DirectWrite, WASAPI, DWM | Windows SDK | no extra install |
| WebView2 loader + headers | `third_party/webview2/pkg` | `Microsoft.Web.WebView2` NuGet package, already extracted in the tree |
| WebView2 Runtime | Windows 10 1809+ / Windows 11 | the installer bootstraps it if missing |
| ASIO | the interface's own driver installation | this project declares the ASIO 2.3 interface itself and loads the installed driver |

WebView2 has no import library requirement here: `WebView2Loader.dll`'s exports are
resolved from the SDK's static library that the CMake target links.

Nothing else is fetched at build time — the build works offline.

## Build

```bat
tools\build.bat
```

The script runs `vcvars64.bat`, configures CMake with the Ninja generator into
`build\`, builds `Release`, and prints `BUILD OK` on success. Output:

```
build\SoloWorshiper.exe    the application
build\lvs_probe.exe          console self-test (devices, ASIO, counters)
build\lvs_latencytest.exe    reports the buffer size each driver grants
```

A clean build takes about 40 s on a modern laptop.

### Manual configure

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Notes

* `/MT` is deliberate: the application ships as a single exe with no runtime
  installer requirement.
* `UNICODE`, `WIN32_LEAN_AND_MEAN` and `NOMINMAX` are set for every target.
* Warnings are level 4; the build is warning-clean as shipped.

## Installer

`installer\lvs.iss` is an Inno Setup 6 script. To build the setup program:

```bat
tools\build.bat
tools\installer.bat
```

`tools\installer.bat` locates `iscc.exe` in the usual Inno Setup folders
(`%LOCALAPPDATA%\Programs\Inno Setup 6` for a per-user install,
`%ProgramFiles(x86)%\Inno Setup 6` for a machine-wide one) and fails with a clear
message when Inno Setup is not installed. The shipped setup program was built with
Inno Setup **6.7.4** (`innosetup-6.7.4.exe` from the Inno Setup releases); any 6.x
compiler produces the same package. Output:
`dist\SoloWorshiperSetup-1.0.0.exe`.

The installer:

* installs to `%ProgramFiles%\Solo Worshiper`, per-user data stays in `%APPDATA%`
* adds Start Menu and optional desktop shortcuts
* checks for the WebView2 Runtime registry key and, when missing, runs the
  Evergreen bootstrapper (needs a network connection on first install)
* registers an uninstaller that leaves recordings and user presets untouched

## Android

The phone/tablet app lives in `android\` and is built with the Android Gradle Plugin
(no Kotlin plugin, no NDK). It needs a JDK and an Android SDK with platform 36 and
build-tools 36.

```bash
export JAVA_HOME="/c/Program Files/Android/Android Studio/jbr"
GRADLE="$USERPROFILE/.gradle/wrapper/dists/gradle-9.8.2-bin/*/gradle-9.8.2/bin/gradle.bat"
"$GRADLE" -p android assembleDebug
cp android/app/build/outputs/apk/debug/app-debug.apk dist/SoloWorshiper-Android-1.0.0.apk
```

Install with `adb install -r dist\SoloWorshiper-Android-1.0.0.apk` or by opening the
APK on the device. The APK is signed with the standard Android debug key, which is
what sideloading expects; a store release needs your own key. `android/local.properties`
holds this machine's SDK path and must be regenerated elsewhere.

## Where data lives

| Path | Contents |
| --- | --- |
| `%APPDATA%\SoloWorshiper\settings.json` | device configuration, window state, last preset |
| `%APPDATA%\SoloWorshiper\presets.json` | user presets |
| `%APPDATA%\SoloWorshiper\startup.log` | startup diagnostics, appended per launch |
| `%LOCALAPPDATA%\SoloWorshiper\WebView2` | embedded browser profile |
| `Documents\Music\Solo Worshiper` | recordings (24-bit or 32-bit float WAV) |

Deleting the `%APPDATA%` folder restores factory defaults; it never touches
recordings.
