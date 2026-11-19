#!/bin/bash
# Flash the spike and its model to the Sticky over USB (CH343: 115200 baud only).
#   tools/flash.sh oido     <port> models/oido_v2b.tnm models/oido_v2.tlm
#   tools/flash.sh babytalk <port> models/citrinet256_int4.mmrt
# Oído's partitions hold [u32 size][12 zero bytes][blob]; babytalk's model is the raw .mmrt file.
set -euo pipefail
cd "$(dirname "$0")/.."
ENGINE=$1; PORT=$2; MODEL=$3; LM=${4:-}
OUT=images_$ENGINE; mkdir -p $OUT
if [ "$ENGINE" = oido ]; then
  python3 - "$MODEL" "$OUT/model.bin" <<'PY'
import struct, sys
b = open(sys.argv[1], 'rb').read(); open(sys.argv[2], 'wb').write(struct.pack('<I', len(b)) + b'\0' * 12 + b)
PY
  python3 - "$LM" "$OUT/lm.bin" <<'PY'
import struct, sys
b = open(sys.argv[1], 'rb').read() if sys.argv[1] else b''; open(sys.argv[2], 'wb').write(struct.pack('<I', len(b)) + b'\0' * 12 + b)
PY
  LMARG="0x210000 $OUT/lm.bin"
else
  cp "$MODEL" $OUT/model.bin; LMARG=""
fi
B=build_$ENGINE
esptool --chip esp32s3 --port "$PORT" --baud 115200 --after hard_reset write-flash --flash-mode dio --flash-freq 80m --flash-size 32MB \
  0x0 $B/bootloader/bootloader.bin 0x8000 $B/partition_table/partition-table.bin 0x10000 $B/sticky_stt.bin 0x410000 $OUT/model.bin $LMARG
echo "flashed $ENGINE with $(basename "$MODEL")"
