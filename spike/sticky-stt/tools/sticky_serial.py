#!/usr/bin/env python3
"""Talk to the spike over its UART. Run with ESP-IDF's python (it has pyserial):
   sticky_serial.py <port> log                  print everything (Ctrl-C to stop)
   sticky_serial.py <port> rec <secs> <out.wav> record on the board, transcribe, then pull the audio as a WAV
   sticky_serial.py <port> last <out.wav>       pull the last recording as a WAV
"""
import base64, struct, sys, time, serial

def wav(path, pcm):
    n = len(pcm)
    hdr = b'RIFF' + struct.pack('<I', 36 + n) + b'WAVEfmt ' + struct.pack('<IHHIIHH', 16, 1, 1, 16000, 32000, 2, 16) + b'data' + struct.pack('<I', n)
    open(path, 'wb').write(hdr + pcm)

def pull(s, path):
    data, n = [], None
    while True:
        line = s.readline().decode('utf-8', 'replace').rstrip()
        if line.startswith('WAV '): n = int(line.split()[1]); continue
        if line == 'END': break
        if n is not None and line: data.append(line)
        if not line and n is None: continue
    pcm = base64.b64decode(''.join(data))
    wav(path, pcm)
    print(f'{path}: {len(pcm)//2} samples ({len(pcm)/32000:.2f} s)')

def main():
    port, cmd = sys.argv[1], sys.argv[2]
    s = serial.Serial(port, 115200, timeout=60)
    time.sleep(0.3); s.reset_input_buffer()
    if cmd == 'log':
        while True: sys.stdout.write(s.readline().decode('utf-8', 'replace')); sys.stdout.flush()
    if cmd == 'rec':
        s.write(f'rec {sys.argv[3]}\n'.encode())
        while True:
            line = s.readline().decode('utf-8', 'replace').rstrip()
            print(line)
            if line.startswith('MEM ') : break
        s.write(b'last\n'); pull(s, sys.argv[4])
    if cmd == 'last':
        s.write(b'last\n'); pull(s, sys.argv[3])
    if cmd == 'pcm':
        import wave
        for path in sys.argv[3:]:
            w = wave.open(path); n = w.getnframes(); pcm = w.readframes(n)
            assert w.getframerate() == 16000 and w.getnchannels() == 1 and w.getsampwidth() == 2, path
            s.reset_input_buffer(); s.write(f'pcm {n}\n'.encode())
            while s.readline().strip() != b'SEND': pass
            b64 = base64.b64encode(pcm).decode()
            for i in range(0, len(b64), 76): s.write((b64[i:i+76] + '\n').encode())
            s.write(b'END\n'); s.flush()
            text = stat = ''
            while True:
                line = s.readline().decode('utf-8', 'replace').rstrip()
                if line.startswith('TEXT: '): text = line[6:]
                if line.startswith('STAT: '): stat = line[6:]
                if line.startswith('MEM ') or line.startswith('TEXT: (') : break
            print(f'{path}\t{text}\t{stat}', flush=True)

main()
