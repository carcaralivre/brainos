// Parse and compare [v]MAJOR.MINOR.PATCH with an optional pre-release.
// Host tests include this file; ota.cpp uses it for the GitHub tag.
#pragma once
#include <cctype>
#include <cstring>
#include <cstdlib>

inline bool parseVersion(const char *s, long v[3], const char **pre = nullptr) {
  if (!s) return false;
  if (*s == 'v' || *s == 'V') s++;
  for (int i = 0; i < 3; i++) {
    if (!isdigit((unsigned char)*s)) return false;
    char *end;
    v[i] = strtol(s, &end, 10);
    s = end;
    if (i < 2 && *s++ != '.') return false;
  }
  const char *p = "";
  if (*s == '-') {
    p = ++s;
    if (!*s) return false;
    for (; *s; s++)
      if (!isalnum((unsigned char)*s) && *s != '.') return false;
    if (p[0] == '.' || s[-1] == '.' || strstr(p, "..")) return false;
  } else if (*s) {
    return false;
  }
  if (pre) *pre = p;
  return true;
}

inline int comparePre(const char *a, const char *b) {
  while (*a && *b) {
    size_t la = strcspn(a, "."), lb = strcspn(b, ".");
    bool na = la && strspn(a, "0123456789") >= la, nb = lb && strspn(b, "0123456789") >= lb;
    int c;
    if (na && nb) c = strtol(a, nullptr, 10) < strtol(b, nullptr, 10) ? -1 : strtol(a, nullptr, 10) > strtol(b, nullptr, 10);
    else if (na != nb) c = na ? -1 : 1;
    else {
      c = strncmp(a, b, la < lb ? la : lb);
      if (!c && la != lb) c = la < lb ? -1 : 1;
    }
    if (c) return c < 0 ? -1 : 1;
    a += la, b += lb;
    if (*a == '.') a++;
    if (*b == '.') b++;
  }
  return *a ? 1 : *b ? -1 : 0;
}

inline int updCompare(const char *a, const char *b) {
  long x[3], y[3];
  const char *pa, *pb;
  bool okA = parseVersion(a, x, &pa), okB = parseVersion(b, y, &pb);
  if (!okA || !okB) return okA - okB;
  for (int i = 0; i < 3; i++)
    if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
  if (!*pa || !*pb) return *pa ? -1 : *pb ? 1 : 0;
  return comparePre(pa, pb);
}

inline bool webPassSplit(const char *kept, int *cut) {
  if (!kept || !cut) return false;
  const char *d = strchr(kept, '$');
  if (!d || d == kept || !d[1]) return false;
  *cut = (int)(d - kept);
  return true;
}
