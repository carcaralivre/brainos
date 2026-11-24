// Host tests for version tags and the page-password record (no Arduino, no board).
#include "semver.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static int fails;

static void expect(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL %s\n", what);
    fails++;
  }
}

int main() {
  long v[3];
  const char *pre = "";
  expect(parseVersion("v1.2.3", v, &pre) && v[0] == 1 && v[1] == 2 && v[2] == 3 && !*pre, "parse v1.2.3");
  expect(parseVersion("0.7.6", v, &pre) && v[0] == 0 && v[1] == 7 && v[2] == 6, "parse 0.7.6");
  expect(parseVersion("v1.0.0-rc1", v, &pre) && !strcmp(pre, "rc1"), "parse pre");
  expect(!parseVersion("v1.2.3.4", v, &pre), "refuse four parts");
  expect(!parseVersion("dev-abc", v, &pre), "refuse dev");
  expect(!parseVersion("v1.2", v, &pre), "refuse two parts");
  expect(updCompare("v0.7.5", "v0.7.6") < 0, "0.7.5 < 0.7.6");
  expect(updCompare("v0.7.6", "v0.7.6") == 0, "equal");
  expect(updCompare("v1.0.0-rc1", "v1.0.0") < 0, "rc < release");
  expect(updCompare("v1.0.0", "v1.0.0-rc1") > 0, "release > rc");
  expect(updCompare("v1.0.0-rc1", "v1.0.0-rc2") < 0, "rc1 < rc2");
  expect(updCompare("not-a-version", "v0.1.0") < 0, "unparsable sorts lowest");

  int cut = 0;
  expect(webPassSplit("abcd$hash", &cut) && cut == 4, "split salt$hash");
  expect(!webPassSplit("", &cut), "empty record");
  expect(!webPassSplit("nosep", &cut), "no dollar");
  expect(!webPassSplit("$onlyhash", &cut), "empty salt");
  expect(!webPassSplit("salt$", &cut), "empty hash");
  expect(!webPassSplit(nullptr, &cut), "null record");

  if (fails) {
    std::fprintf(stderr, "%d failed\n", fails);
    return 1;
  }
  std::puts("ok");
  return 0;
}
