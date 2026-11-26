#!/usr/bin/env bash
# Builds SoloWorshiper.app with swiftc and a hand-made bundle, so no Xcode
# project file has to be kept in step with the sources.
#
#   ./build.sh          build into mac/build/SoloWorshiper.app
#   open build/SoloWorshiper.app
set -euo pipefail
cd "$(dirname "$0")"

APP="build/SoloWorshiper.app"
BIN="$APP/Contents/MacOS/SoloWorshiper"
TARGET="$(uname -m)-apple-macos13.0"

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"

SOURCES=()
while IFS= read -r file; do
  SOURCES+=("$file")
done < <(find SoloWorshiper -name '*.swift' | sort)

if [ ${#SOURCES[@]} -eq 0 ]; then
  echo "build.sh: no Swift sources found under SoloWorshiper/" >&2
  exit 1
fi

echo "Building $TARGET (${#SOURCES[@]} sources)"
swiftc \
  -O -parse-as-library \
  -target "$TARGET" \
  -framework AppKit \
  -framework AVFoundation \
  -framework CoreAudio \
  -framework SwiftUI \
  -framework WebKit \
  -o "$BIN" \
  "${SOURCES[@]}"

cp SoloWorshiper/Info.plist "$APP/Contents/Info.plist"
cp SoloWorshiper/player.html "$APP/Contents/Resources/player.html"

# Ad-hoc signature: enough to run locally, including the microphone prompt.
codesign --force --sign - "$APP"

echo "Built $APP"
