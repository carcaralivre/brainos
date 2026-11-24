# Reading BrainOS QR exports in another app

This file is for a person or an AI assistant adding BrainOS import to some other app
(a notes app, a shortcut, a script). Everything needed is here; the reader page in this
repository (`site/reader/index.html`) is a working reference in about 100 lines of plain JavaScript.

## What the board does

A [BrainOS](https://github.com/carcaralivre/brainos) board (Seeed reTerminal Sticky, e-paper)
exports its notes as a **loop of QR codes**: Settings → Export shows part 1, then part 2, …,
about 1 second each, then starts again from part 1, until the user taps Archive all. Every part of
one export is the same size (the JSON is padded), so the codes keep one density. The export
holds **every note that is not in the Archive**, oldest first. Done moves them to the Archive
(default) or just marks them exported; leaving with the back arrow changes nothing.

## One QR code

Byte mode, UTF-8, at most about 950 bytes, QR version up to 25, error correction M.
Each code is one JSON object:

```json
{"v":1,"b":"a1b2","i":2,"n":3,"dev":"Brain's Brain",
 "notes":[{"id":"8f3c2a1e","c":1763500000,"p":0,"q":0,"t":"Buy milk and coffee filters"}],
 "pad":"      "}
```

| Field | Meaning |
|---|---|
| `v` | payload version, always `1` |
| `b` | batch id, 4 hex digits; every part of one export carries the same `b` |
| `i` | this part's number, 1 to `n` |
| `n` | how many parts the batch has |
| `dev` | the board's name |
| `notes[]` | the notes in this part, oldest first |
| `notes[].id` | 8 hex digits, stable for the note's life |
| `notes[].c` | created, unix seconds (UTC) |
| `notes[].p` | `1` if the note is pinned on the board, else `0` |
| `notes[].q` | `1` if the note was a question to the assistant: the text is the question, a blank line, the answer |
| `notes[].t` | the text, UTF-8, JSON-escaped |
| `notes[].k`, `notes[].kn` | only on a note too long for one code: this is piece `k` of `kn`. The pieces come in order, one after the other (across parts too); join their `t` |
| `pad` | spaces, ignore it (it makes every part the same length) |

A note that would not fit one part on its own is cut and ends with `…`.

## How to put a batch together

1. Decode each QR as UTF-8 text and parse it as JSON. Ignore anything that is not an object
   with `v === 1`, a string `b`, numbers `i` and `n`, and an array `notes`.
2. Keep parts in a map keyed by `i`, for the current `b`. The same part read twice is the
   same part; keep the first.
3. The batch is complete when parts `1..n` are all present. Show "k of n read" meanwhile;
   the loop means any order works and a missed part comes round again in about `n` seconds.
4. A code with a different `b` is another export. Ask before dropping what was gathered.
5. Concatenate the `notes` arrays in part order (1, 2, … n), joining the pieces of a long note
   (same `id`, `k` 2 and up) onto the piece before. Dedupe by `id` if the app keeps notes from
   earlier imports.
6. Nothing goes back to the board: it does not know the import happened. The user taps Done
   on the board when the app says it has everything.

## Minimal JavaScript

```js
// With jsQR (https://github.com/cozmo/jsQR): feed it camera frames via a canvas.
const parts = {}; let batch = null, total = 0;
function onQrText(text) {
  let o; try { o = JSON.parse(text); } catch { return; }
  if (!o || o.v !== 1 || typeof o.b !== 'string' || !o.i || !o.n || !Array.isArray(o.notes)) return;
  if (batch && o.b !== batch) return;          // another export: ask the user first
  batch = o.b; total = o.n;
  parts[o.i] ??= o.notes;
  if (Object.keys(parts).length < total) return; // keep scanning
  const notes = [];
  for (let i = 1; i <= total; i++)
    for (const n of parts[i]) {
      const last = notes[notes.length - 1];
      if (n.kn > 1 && n.k > 1 && last && last.id === n.id) last.t += n.t;   // a long note's next piece
      else notes.push({ ...n });
    }
  // notes: [{id, c, p, q, t}], oldest first. Done.
}
```

Reading the frames: draw the `<video>` onto a canvas, `getImageData`, then
`jsQR(data, width, height, { inversionAttempts: 'dontInvert' })` about 6 times a second.
A mirrored preview (front camera) does not matter: decode the unmirrored frame.

## Testing without a board

Make a code from any payload above with a QR tool (for example `qrencode -o part1.png
-l M '<json>'`) and point the app at the screen, or use the reader page's "Read an image".
The firmware's own copy of this spec is `docs/qr-payload.md` in the brainos repository; the
firmware (`firmware/brainos/export.cpp`) is the source of truth if the two ever differ.
