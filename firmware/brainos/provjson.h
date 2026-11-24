// A strict little JSON reader and string escaper: notes.jsonl, the transcription reply, the
// QR payload and the JSON download. pj::parse checks the whole document first (it fails on
// anything malformed, nested past MAX_DEPTH, or with trailing bytes) and only then reads it.
// Values are views into the caller's buffer: no allocation until a string is decoded.
#pragma once
#include <Arduino.h>

namespace pj {

static const int MAX_DEPTH = 16;   // OpenRouter's answers nest a few levels deep (citations, usage details)

enum Type : char { NONE = 0, OBJ = 'o', ARR = 'a', STR = 's', NUM = 'n', BOOL = 'b', NUL = 'z' };

struct Val {
  const char *p = nullptr;  // first byte of the value
  size_t n = 0;             // its length in bytes
  Type type = NONE;
  bool ok() const { return type != NONE; }
};

// The whole text must be one JSON value (whitespace around it is fine).
bool parse(const char *s, size_t len, Val &out);

// obj["key"] (first match); false if obj isn't an object or has no such key.
bool member(const Val &obj, const char *key, Val &out);
// arr[i]; false past the end.
bool at(const Val &arr, size_t i, Val &out);
size_t size(const Val &arrOrObj);

// Typed reads; false if the type doesn't match. str() decodes escapes,
// \uXXXX included (surrogate pairs too), into UTF-8.
bool str(const Val &v, String &out);
bool num(const Val &v, long &out);  // integers only: no fraction, no exponent
bool boolean(const Val &v, bool &out);

// For building responses.
String esc(const String &s);  // with the quotes: "a\"b"

}  // namespace pj
