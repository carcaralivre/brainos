#!/bin/bash
# Build the spike for one engine: tools/build.sh oido|babytalk [v2b|v2a|nemo4]
# Models: oido v2b/v2a use beam 2 / topk 3 / lm 0.30 / bonus 0.50 (the project's "fastdec" settings).
set -euo pipefail
cd "$(dirname "$0")/.."
ENGINE=$1; VARIANT=${2:-}
: "${OIDO_DIR:?set OIDO_DIR}"; : "${BABYTALK_DIR:?set BABYTALK_DIR}"
. "${IDF_PATH:-$HOME/esp/esp-idf}/export.sh" >/dev/null
EXTRA=""
case "$ENGINE:$VARIANT" in
  oido:v2*) EXTRA="-DBEAM=2 -DTOPK=3 -DLMW=0.30f -DLMB=0.50f" ;;
esac
STICKY_ENGINE="$ENGINE" idf.py -B "build_$ENGINE" -D SDKCONFIG="sdkconfig_$ENGINE" -D OIDO_DIR="$OIDO_DIR" -D BABYTALK_DIR="$BABYTALK_DIR" \
  ${EXTRA:+-D "CMAKE_C_FLAGS=$EXTRA"} build
ls -l "build_$ENGINE/sticky_stt.bin"
