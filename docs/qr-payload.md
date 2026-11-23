# BrainOS QR export: the payload

BrainOS exports notes as a loop of QR codes on its e-paper screen. The reader page
(`brainos-reader`, a static web page) reads them with the phone's camera, in any order,
and puts the batch back together.

## One QR code

Byte mode, UTF-8, at most about 950 bytes, QR version up to 25, error correction M. Each
code holds one JSON object:

```json
{"v":1,"b":"a1b2","i":2,"n":3,"dev":"Brain do Eduardo",
 "notes":[{"id":"8f3c2a1e","c":1763500000,"p":0,"t":"Buy milk and coffee filters"}],
 "pad":"      "}
```

| Field | Meaning |
|---|---|
| `v` | payload version, 1 |
| `b` | batch id, 4 hex digits; every part of one export carries the same `b` |
| `i` | this part's number, 1 to `n` |
| `n` | how many parts the batch has |
| `dev` | the board's name |
| `notes[]` | the notes in this part, oldest first |
| `notes[].id` | 8 hex digits, stable for the note's life |
| `notes[].c` | created, unix seconds (UTC) |
| `notes[].p` | 1 if the note is pinned on the board |
| `notes[].q` | 1 if the note was a question to the assistant (the text is the question, a blank line, the answer) |
| `notes[].t` | the text (UTF-8, JSON-escaped) |
| `pad` | spaces, ignored: every part of a batch is padded to the same length, so all its codes have the same density |

The export holds every note that is not in the Archive, oldest first. The board shows the
parts in order, about 1 second each, and starts again from part 1 until Done is tapped
(or OK is pressed). A note that would not fit a part on its own is cut and ends with `…`.

## Reading

1. Decode each QR as UTF-8 JSON. Ignore anything that is not an object with `v` = 1.
2. Keep parts in a set keyed by `b` + `i`. A part seen twice is the same part.
3. The batch is complete when parts 1..`n` are all present. Show "k of n" meanwhile.
4. A code with a different `b` is another export: ask before replacing what was gathered.
5. Join the `notes` arrays in part order. Notes already seen in an earlier batch (same `id`)
   may be skipped.

Done on the board marks the notes as exported and, with "Archive after exporting"
on (the default), moves them to the Archive. Leaving with the back arrow changes nothing.

A guide for adding BrainOS import to another app lives in the reader repository:
`INTEGRATION.md` in github.com/carcaralivre/brainos-reader.
