cmake_minimum_required(VERSION 3.20)
project(SoloWorshiper VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT MSVC)
  message(FATAL_ERROR "SOLO WORSHIPER targets Windows x64 with MSVC.")
endif()

set(LVS_WEBVIEW2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/webview2/pkg")

# ---------------------------------------------------------------------------
# Core: audio engine, DSP, presets. No UI dependencies, links into both the
# windowed application and the console self-test probe.
# ---------------------------------------------------------------------------
add_library(lvs_core STATIC
  src/audio/Wasapi.cpp
  src/audio/Asio.cpp
  src/audio/Engine.cpp
  src/audio/Recorder.cpp
  src/audio/SessionVolume.cpp
  src/core/Params.cpp
  src/core/Presets.cpp
  src/core/Settings.cpp
)

target_include_directories(lvs_core PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${LVS_WEBVIEW2_DIR}/build/native/include"
)

target_compile_definitions(lvs_core PUBLIC
  UNICODE _UNICODE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  _CRT_SECURE_NO_WARNINGS
)

target_compile_options(lvs_core PRIVATE
  /W4 /permissive- /utf-8 /MP
  $<$<CONFIG:Release>:/O2 /Oi /Gy /fp:precise>
)

target_link_libraries(lvs_core PUBLIC
  ole32 uuid oleaut32 winmm avrt ksuser wmcodecdspuuid
)

set_property(TARGET lvs_core PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---------------------------------------------------------------------------
# Console probe: enumerates devices, opens the Focusrite path, runs the real
# DSP chain and reports measured levels/latency. Used by the build check.
# ---------------------------------------------------------------------------
add_executable(lvs_probe tools/probe.cpp)
target_link_libraries(lvs_probe PRIVATE lvs_core)

add_executable(lvs_latencytest tools/latencytest.cpp)
target_link_libraries(lvs_latencytest PRIVATE lvs_core)
set_property(TARGET lvs_latencytest PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set_property(TARGET lvs_probe PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---------------------------------------------------------------------------
# The application.
# ---------------------------------------------------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
add_executable(SoloWorshiper WIN32
  assets/app.rc
  src/main.cpp
  src/app/App.cpp
  src/app/Console.cpp
  src/web/YouTubeHost.cpp
  src/web/PlaylistFetch.cpp
  src/ui/Ui.cpp
)

target_link_libraries(SoloWorshiper PRIVATE
  lvs_core
  d2d1 dwrite d3d11 dxgi dcomp
  windowscodecs shlwapi shell32 comctl32 dwmapi uxtheme winhttp
  "${LVS_WEBVIEW2_DIR}/build/native/x64/WebView2Loader.dll.lib"
)
set_property(TARGET SoloWorshiper PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_custom_command(TARGET SoloWorshiper POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${LVS_WEBVIEW2_DIR}/build/native/x64/WebView2Loader.dll"
          "$<TARGET_FILE_DIR:SoloWorshiper>/WebView2Loader.dll"
  COMMENT "Staging WebView2Loader.dll"
)
endif()
