// Over-the-air updates from GitHub Releases (carcaralivre/brainos).
//
// A check reads the feed's newest release (GitHub's releases/latest JSON:
// tag_name, body, and the assets brainos.bin and brainos.bin.sha256).
// An install streams brainos.bin into the app slot that isn't running,
// checks its size and SHA-256, makes it the boot slot and restarts. A
// download that fails or doesn't match leaves the slot unbootable and the
// running firmware as it was.
//
// Rollback: the core's bootloader has CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE,
// so a new firmware boots "pending verify" and updMarkGood() keeps it. A
// firmware that restarts before that (a crash on boot) goes back to the
// previous one at the next start. The sketch calls updMarkGood() once WiFi
// is up and the first screen is drawn, after 60 s at the latest, and
// before any restart it makes on purpose.
//
// updCheck() and updInstall() block, and run on the loop task (the screen says "Checking"
// or "Installing" meanwhile). Nothing here ever restarts the board except a finished install.
#pragma once
#include <Arduino.h>

enum UpdState : uint8_t { UPD_IDLE, UPD_CHECKING, UPD_INSTALLING, UPD_RESTARTING };

struct UpdInfo {
  uint8_t state;      // UpdState
  String status;      // one line for the screen: "You're up to date", "v0.2.0 is available"...
  bool failed;        // the last check or install failed: status says why
  String latest;      // the newest release's tag, "" when none is known
  bool available;     // it can be installed: newer than this build, or this is a dev build
  uint32_t checkedMs; // millis() of the last check, 0: none
};

const char *updVersion();  // this build's version: "v0.1.0", or "dev-<commit>"
void updBegin();           // early in setup(): logs the slot and its rollback state
bool updPendingVerify();   // this firmware is on trial: a restart now would go back
void updMarkGood(const char *why);  // keeps this firmware (no-op when not on trial)
UpdInfo updInfo();
void updNoWifi();          // a check or install asked for without Wi-Fi: the status says "No Wi-Fi"

// updCheck: true if the feed answered (even "no release").
bool updCheck();
// Installs the release the last check found. On success it restarts and
// doesn't return; false: updInfo().status says why.
bool updInstall();

