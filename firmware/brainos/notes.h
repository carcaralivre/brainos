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
  bool busy() const { return audio.length() || asking || tidying; }   // waiting for the cloud: not edited, archived or appended to
};

void notesBegin();                                   // mount and load
void notesSave();                                    // write the whole file atomically
std::vector<Note, PsAlloc<Note>> &notesAll();
Note *notesFind(const String &id);
void notesPin(Note &n);   // pins (or unpins); with three already pinned, the oldest pin is let go
Note &notesAdd(const String &text, const String &audio, uint16_t secs, const String &id = "");   // a recording is named after its id
void notesRemove(const String &id, bool keepAudio = false);   // and its audio file, unless it is kept (brain.local's Undo)
int notesCount(bool archived);                       // how many in the list (or in the archive)
int notesPending();                                  // recordings waiting for transcription
String notesNewId();
