# BrainOS

A voice notebook for the [Seeed reTerminal Sticky](https://www.seeedstudio.com/sticky/), the 3.97" magnetic e-paper board. Hold the button, say the idea, let go: the board keeps the recording and turns it into text through OpenRouter (Whisper) when it has Wi-Fi, in Portuguese or English. The notes live on the board, on a page in your home network (`http://brain.local`), and travel to a phone as QR codes.

Built on [Sticky OS](https://github.com/Sticky-OS/sticky-os) (MIT): its screen, touch and button plumbing, keyboard, settings store and update mechanism. Everything else was removed or rewritten. Not affiliated with Seeed Studio.

![Lock screen](docs/shots/lock.png) ![Notes](docs/shots/notes.png) ![Export](docs/shots/export.png)

## What it does

- **Two states**: awake, Wi-Fi is on, recordings and questions go out, and `brain.local` answers. Asleep, everything is off and the lock screen shows a small padlock; only OK wakes it. On battery it sleeps 3 minutes after the last touch or button (a setting on `brain.local`); plugged in, never. At midnight it wakes for a moment to change the date.
- **Lock screen**: battery, the weekday and the date, up to three pinned notes, how many notes there are. A tap opens the list, a tap on a pinned note opens it, OK goes back to where you were.
- **Hold a button** (anywhere):
  - **OK**: a new note. Speak, release; the note opens and its text arrives when there is Wi-Fi and an OpenRouter key.
  - **Up**: a new chat note: the question, then the assistant's answer (model and instructions on `brain.local`).
  - **Down**: in a note, a recording added at its end (in a chat note, the next question, with the conversation so far); anywhere else, a passing **Chat** about your notes, saved only if you tap *Save as note*.
- **Notes**: grouped by day. Open one to pin it, edit it, *Tidy* it (the assistant fixes punctuation and drops filler words; the original is kept), archive or delete it. A long note scrolls by pages with Up/Down; a swipe sideways goes to the next note.
- **Costs**: each note shows what it cost (transcriptions and answers); the board's About and `brain.local` show the totals by day, week and month.
- **brain.local**: a page on your network to paste the OpenRouter key, read, pin, tidy, archive and delete notes (with *Undo*), download them as Markdown, add Wi-Fi networks, and change settings.
- **Export**: a loop of QR codes with the notes on the list; the [reader page](https://carcaralivre.github.io/brainos-reader/) on the phone reads them in any order. Format: [docs/qr-payload.md](docs/qr-payload.md).
- **Archive**, **Settings** on the board (Wi-Fi with a scan and a keyboard, export, archive, about), **updates over Wi-Fi** from this repository's releases, with rollback.

## Install

Open the [Install page](https://carcaralivre.github.io/brainos/) in Chrome or Edge with the board on USB. Or by hand, from a [release](https://github.com/carcaralivre/brainos/releases):

```bash
esptool --chip esp32s3 --baud 115200 write-flash 0x0 brainos-full.bin
```

115200 baud only: the board's USB chip drops faster transfers.

## Build

arduino-cli, ESP32 core 3.3.11, Adafruit GFX 1.12.6 and BusIO 1.17.4 (see `.github/workflows/build.yml`). Then:

```bash
firmware/build.sh build   # into ~/.cache/brainos-build
firmware/build.sh         # build and flash over USB
```

Serial commands at 115200 (for development): `shot` (a screenshot; `firmware/tools/drive.py <port> shot:file.png` saves it as PNG and runs any other commands in order), `notes`, `note add <text>`, `wifi`, `wifi add <ssid> <password>`, `time YYYY-MM-DD HH:MM`, `tap x y`, `ok`, `move n`, `lock`, `sleep`.

`spike/sticky-stt/` is the on-chip speech-to-text experiment that decided against local transcription ([docs/stt-spike.md](docs/stt-spike.md)). It is not part of the firmware.

## Layout

| Where | What |
|---|---|
| `firmware/brainos/` | the sketch: `board`, `screen`, `touch`, `rtc` (hardware); `notes`, `capture`, `stt`, `chat`, `cost`, `net`, `web`, `export`, `ota` (features); `ui`, `keyboard` (screens) |
| `firmware/build.sh`, `firmware/tools/` | build, flash, screenshots, serial driving |
| `site/` | the Install page (esp-web-tools), deployed to GitHub Pages by the Install-page workflow after each release |
| `docs/` | the QR payload spec, the guide, the spike's record, screenshots |

MIT, see LICENSE.
