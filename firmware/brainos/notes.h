// Notes: the list kept on LittleFS as /notes.jsonl, one JSON object a line, newest last.
#pragma once
#include <Arduino.h>
#include <vector>
#include "psalloc.h"

struct Note {
  String id;          // 8 hex digits, stable for the note's life
  String text;        // empty while the audio waits for transcription
  String audio;       // /queue/<id>.wav while a recording waits; "" afterwards
  uint32_t created = 0, modified = 0;
  uint16_t secs = 0;  // recording length, for pending audio
  bool pinned = false, archived = false, exported = false;
};

void notesBegin();                                   // mount and load
void notesSave();                                    // write the whole file atomically
std::vector<Note, PsAlloc<Note>> &notesAll();
Note *notesFind(const String &id);
void notesPin(Note &n);   // pins (or unpins); with three already pinned, the oldest pin is let go
Note &notesAdd(const String &text, const String &audio, uint16_t secs, const String &id = "");   // a recording is named after its id
void notesRemove(const String &id);                  // and its audio file
int notesCount(bool archived);                       // how many in the list (or in the archive)
int notesPending();                                  // recordings waiting for transcription
String notesNewId();
