#!/usr/bin/env python3
"""Take a screenshot of the board over serial and save a PNG (rotated to how the screen is held).
   shot.py <port> <out.png>   (run with ESP-IDF's python: it has pyserial)"""
import struct, sys, time, zlib, serial

def png(path, w, h, rows):  # rows: list of bytes, one byte per pixel (0..255 grey)
    raw = b''.join(b'\0' + r for r in rows)
    def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 0, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))

s = serial.Serial(sys.argv[1], 115200, timeout=10)
time.sleep(0.2); s.reset_input_buffer(); s.write(b'shot\n')
hdr = None
while True:
    line = s.readline().decode('ascii', 'replace').strip()
    if line.startswith('SHOT '): hdr = line.split(); break
    if not line: sys.exit('no SHOT header')
w, h, rot = int(hdr[1]), int(hdr[2]), int(hdr[3])
pix = []
for y in range(h):
    row = bytes.fromhex(s.readline().decode('ascii').strip())
    pix.append([255 if row[x >> 3] & (0x80 >> (x & 7)) else 0 for x in range(w)])
# rotate so the image is upright as drawn: rotation 1 = 90 degrees clockwise on the canvas
if rot == 3: out = [[pix[h - 1 - y][x] for y in range(h)] for x in range(w)]; w, h = h, w
elif rot == 1: out = [[pix[y][w - 1 - x] for y in range(h)] for x in range(w)]; w, h = h, w
elif rot == 2: out = [[pix[h - 1 - y][w - 1 - x] for x in range(w)] for y in range(h)]
else: out = pix
png(sys.argv[2], w, h, [bytes(r) for r in out])
print(f'{sys.argv[2]}: {w}x{h}')
