#!/usr/bin/env python3
"""Drive the board over serial: drive.py <port> 'cmd' 'shot:file.png' ...  (ESP-IDF python: pyserial).
Each argument is a serial command, or shot:<png> to save a screenshot of the current screen."""
import struct, sys, time, zlib, serial

def png(path, w, h, rows):
    raw = b''.join(b'\0' + r for r in rows)
    def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 0, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))

def shot(s, path):
    s.reset_input_buffer(); s.write(b'shot\n')
    while True:
        line = s.readline().decode('ascii', 'replace').strip()
        if line.startswith('SHOT '): hdr = line.split(); break
        if not line: raise SystemExit('no SHOT header')
    w, h, rot = int(hdr[1]), int(hdr[2]), int(hdr[3])
    pix = []
    for y in range(h):
        row = bytes.fromhex(s.readline().decode('ascii').strip())
        pix.append([255 if row[x >> 3] & (0x80 >> (x & 7)) else 0 for x in range(w)])
    s.readline()
    if rot == 3: out = [[pix[h - 1 - y][x] for y in range(h)] for x in range(w)]; w, h = h, w
    elif rot == 1: out = [[pix[y][w - 1 - x] for y in range(h)] for x in range(w)]; w, h = h, w
    elif rot == 2: out = [[pix[h - 1 - y][w - 1 - x] for x in range(w)] for y in range(h)]
    else: out = pix
    png(path, w, h, [bytes(r) for r in out]); print(f'{path}: {w}x{h}')

s = serial.Serial(sys.argv[1], 115200, timeout=10)
time.sleep(0.2)
for a in sys.argv[2:]:
    if a.startswith('shot:'): shot(s, a[5:]); continue
    s.reset_input_buffer(); s.write((a + '\n').encode()); time.sleep(1.6)
    out = s.read(8000).decode('utf-8', 'replace').strip()
    if out: print(f'> {a}\n{out}')
