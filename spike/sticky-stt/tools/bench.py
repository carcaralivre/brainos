#!/usr/bin/env python3
"""Score the ten phrases, read on the board with the OK button. Run with ESP-IDF's python (pyserial):
   bench.py <port> <engine> <outdir> [count]
Listens to the board. Each time the board finishes a transcription (a STAT line), the script pulls
the audio as outdir/<engine>_NN.wav, scores it against phrase NN of phrases.txt and prints the
running word error rate. Stops after `count` recordings (default: all phrases). Ctrl-C to stop early.
"""
import base64, json, os, re, struct, sys, serial

def norm(s):
    s = s.lower().replace('e-ink', 'eink'); return re.sub(r'[^a-z0-9 ]+', ' ', s).split()

def wer(ref, hyp):
    r, h = norm(ref), norm(hyp)
    d = list(range(len(h) + 1))
    for i in range(1, len(r) + 1):
        p, d[0] = d[0], i
        for j in range(1, len(h) + 1):
            cur = min(d[j] + 1, d[j - 1] + 1, p + (r[i - 1] != h[j - 1]))
            p, d[j] = d[j], cur
    return d[len(h)], len(r)

def pull(s, path):
    data, n = [], None
    s.write(b'last\n')
    while True:
        line = s.readline().decode('utf-8', 'replace').rstrip()
        if line.startswith('WAV '): n = int(line.split()[1]); continue
        if line == 'END': break
        if n is not None and line and ' ' not in line: data.append(line)
    pcm = base64.b64decode(''.join(data))
    hdr = b'RIFF' + struct.pack('<I', 36 + len(pcm)) + b'WAVEfmt ' + struct.pack('<IHHIIHH', 16, 1, 1, 16000, 32000, 2, 16) + b'data' + struct.pack('<I', len(pcm))
    open(path, 'wb').write(hdr + pcm)
    return len(pcm) // 2

def main():
    port, engine, out = sys.argv[1], sys.argv[2], sys.argv[3]
    phrases = [l.strip() for l in open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'phrases.txt')) if l.strip()]
    count = int(sys.argv[4]) if len(sys.argv) > 4 else len(phrases)
    os.makedirs(out, exist_ok=True)
    s = serial.Serial(port, 115200, timeout=None)
    results, errs, words, text = [], 0, 0, ''
    print(f'listening on {port}; hold OK and read phrase 1:\n    {phrases[0]}', flush=True)
    while len(results) < count:
        line = s.readline().decode('utf-8', 'replace').rstrip()
        if line.startswith('TEXT: '): text = line[6:]
        if line.startswith('LISTENING') or line.startswith('STAT: ') or line.startswith('MEM ') or line.startswith('TIMING'): print('   ', line, flush=True)
        if not line.startswith('MEM '): continue
        i = len(results) + 1
        ref = phrases[i - 1] if i <= len(phrases) else ''
        wav = os.path.join(out, f'{engine}_{i:02d}.wav')
        n = pull(s, wav)
        e, w = wer(ref, text); errs += e; words += w
        results.append(dict(i=i, ref=ref, hyp=text, errors=e, words=w, samples=n, wav=wav))
        json.dump(results, open(os.path.join(out, f'{engine}.json'), 'w'), indent=1)
        print(f'[{i}] ref:   {ref}\n[{i}] heard: {text}\n[{i}] errors {e}/{w}, running WER {100.0 * errs / max(words, 1):.1f}%', flush=True)
        if i < len(phrases): print(f'\nnext, phrase {i + 1}:\n    {phrases[i]}', flush=True)
    print(f'\nWER {engine}: {100.0 * errs / max(words, 1):.1f}% ({errs} errors in {words} words)')

main()
