#!/usr/bin/env bash
# Packages a built SoloWorshiper.app into a compressed .dmg that carries the
# usual Applications shortcut, so the mounted image can be drag-installed.
#
#   ./package_dmg.sh
#       build/SoloWorshiper.app  ->  build/SoloWorshiper-1.0.0.dmg
#
#   ./package_dmg.sh <bundle.app> <output.dmg>
set -euo pipefail
cd "$(dirname "$0")"

APP="${1:-build/SoloWorshiper.app}"
OUT="${2:-build/SoloWorshiper-1.0.0.dmg}"

if [ ! -d "$APP" ]; then
  echo "package_dmg.sh: no bundle at $APP - run ./build.sh first" >&2
  exit 1
fi
if [ ! -x "$APP/Contents/MacOS/SoloWorshiper" ]; then
  echo "package_dmg.sh: $APP has no executable - the bundle is incomplete" >&2
  exit 1
fi

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"

mkdir -p "$(dirname "$OUT")"
rm -f "$OUT"
hdiutil create \
  -volname "Solo Worshiper" \
  -srcfolder "$STAGE" \
  -fs HFS+ \
  -format UDZO \
  -ov \
  "$OUT" >/dev/null

echo "Built $OUT ($(du -h "$OUT" | cut -f1))"
