#!/bin/bash
# Build (and flash) BrainOS for the reTerminal Sticky. Mirrors upstream's upload.sh.
#   firmware/build.sh            build and flash over USB
#   firmware/build.sh build      build only, into ~/.cache/brainos-build
#   PORT=/dev/cu.usbmodemXXXX firmware/build.sh
set -euo pipefail
cd "$(dirname "$0")/brainos"
FQBN=esp32:esp32:esp32s3:PSRAM=opi,FlashSize=32M,PartitionScheme=custom,DebugLevel=error
APP_MAX=$((0x600000))
COMMIT=$(git rev-parse --short HEAD 2>/dev/null || echo unknown)
if [ "$COMMIT" != unknown ] && [ -n "$(git status --porcelain -- . 2>/dev/null)" ]; then COMMIT="$COMMIT-dirty"; fi
VERSION=${BRAINOS_VERSION:-dev-$COMMIT}
FLAGS="-DSTICKY_COMMIT=$COMMIT -DSTICKY_VERSION=$VERSION"
# The build goes outside Documents: iCloud sync drops " 2" duplicates into busy folders.
BUILD=${BUILD_DIR:-$HOME/.cache/brainos-build}
arduino-cli compile --fqbn "$FQBN" --build-path "$BUILD" --warnings default \
  --build-property "compiler.cpp.extra_flags=$FLAGS" \
  --build-property "upload.maximum_size=$APP_MAX" .
[ "${1:-}" = build ] && exit 0
PORT=${PORT:-$(ls /dev/cu.usbmodem* /dev/cu.wchusbserial* /dev/ttyACM* /dev/ttyUSB* 2>/dev/null | head -1 || true)}
[ -n "$PORT" ] || { echo "no board found; set PORT=/dev/..." >&2; exit 1; }
esptool --chip esp32s3 --port "$PORT" --baud 115200 --after hard_reset write-flash --flash-mode dio --flash-freq 80m --flash-size 32MB \
  0x0 "$BUILD/brainos.ino.bootloader.bin" 0x8000 "$BUILD/brainos.ino.partitions.bin" 0xe000 "$BUILD/boot_app0.bin" 0x10000 "$BUILD/brainos.ino.bin"
