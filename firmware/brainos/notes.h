// Notes: the list kept on LittleFS as /notes.jsonl, one JSON object a line, newest last.
#pragma once
#include <Arduino.h>
#include <vector>
#include "psalloc.h"

struct Note {
  String id;          // 8 hex digits, stable for the note's life
  String text;        // empty while the first recording waits for transcription
  String audio;       // /queue/<name>.wav while a recording waits (the first one, or one to append); "" afterwards
  String orig;        // the text before Tidy, "" when never tidied
  uint32_t created = 0, modified = 0;
  uint32_t cost = 0;  // what the note cost so far, in millionths of a US dollar (transcriptions, answers, Tidy)
  uint16_t secs = 0;  // recording length, for pending audio
  bool pinned = false, archived = false;
  bool asked = false;     // a chat note (Up held): question, blank line, answer, blank line, question...
  bool asking = false;    // the last question transcribed, its answer not in yet
  bool tidying = false;   // Tidy asked for, the tidied text not in yet
  String stuck;           // why the service refused its recording, "" when it didn't; not saved: a restart or a change of settings tries again
  bool busy() const { return audio.length() || asking || tidying; }   // waiting for the cloud: not edited, archived or appended to
};

void notesBegin();                                   // mount and load
bool notesSave();                                    // write the whole file atomically; false (the old file kept) when the flash is full
static const char *const NOTES_SAVE_FAIL = "Not saved: storage full";
std::vector<Note, PsAlloc<Note>> &notesAll();
Note *notesFind(const String &id);
bool notesPin(Note &n);   // pins (or unpins); with three already pinned, the oldest pin is let go; false if not saved
Note *notesAdd(const String &text, const String &audio, uint16_t secs, const String &id = "");   // nullptr if not saved; a recording is named after its id
bool notesRemove(const String &id, bool keepAudio = false);   // and its audio file, unless it is kept (brain.local's Undo)
// pin, archive, restore, delete, tidy, cancel, original, edit. false only when not saved.
bool notesApply(const String &id, const char *op, const String &text = "", bool keepAudio = false);
int notesCount(bool archived);                       // how many in the list (or in the archive)
int notesPending();                                  // notes waiting for the cloud
uint32_t notesVersion();                             // goes up with every save: the page asks for it to know when to reload
bool notesRemoveArchived();                          // the whole archive, in one save
inline bool notesExportable(const Note &n) { return n.text.length() && !n.busy(); }   // a note still waiting for the cloud stays for the next export
String notesNewId();
std::vector<int> notesSorted(int which, bool newestFirst);   // indexes into notesAll(): which 0 the list, 1 the archive, 2 all
String notesWaitText(const Note &n);                 // what the note waits for, and why when something is in the way: "Recording, 12 s, waiting for Wi-Fi", "(asking…)", "(tidying: OpenRouter: no credit, trying again)", or ""
void notesFixClock(uint32_t bootTime);               // the clock was just set: notes made before it (dated 1970) move to this boot's real time
