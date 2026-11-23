// The assistant's side: the passing chat (Down held outside a note: nothing is saved unless
// asked) and the request bodies for every question (chat notes, the passing chat, Tidy).
#pragma once
#include <Arduino.h>
#include <vector>
#include "notes.h"

struct PassChat {
  std::vector<String> turns;   // question, answer, question...
  String audio;                // a recorded question waiting to be transcribed
  uint16_t secs = 0;
  bool asking = false;         // the last question is transcribed, its answer not in yet
  String note;                 // a line under the turns: "(not transcribed: …)"
  uint32_t cost = 0;           // millionths of a dollar so far (goes with it when saved as a note)
  int gen = 0;                 // bumped by chatClear(): a result for an older chat is dropped
  bool waiting() const { return audio.length() || asking; }
};
extern PassChat chat;
void chatClear();                                   // forget it (and its recording)
void chatRecorded(const String &path, uint16_t secs);   // a new question to transcribe
Note *chatSave();                                   // as a chat note; nullptr while it waits or is empty

std::vector<String> chatTurnsOf(const String &text);   // a chat note's text, split at its blank lines
String chatJoin(const std::vector<String> &turns);
String chatAnswerText(String answer);               // no blank lines inside an answer: they separate the turns
String askBodyFor(const std::vector<String> &turns, bool withNotes);   // the last 10 exchanges and the question
String tidyBodyFor(const String &text);
