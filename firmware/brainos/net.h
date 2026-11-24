// Wi-Fi: always on while the board is awake (asleep, everything is off). The clock, brain.local
// and TLS helpers live here too.
#pragma once
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <vector>

void netBegin();
void netTick();                  // call from loop(): joins, times out, switches off
bool netUp();
String netName();
String netAddress();
int netSignal();

struct NetSeen { String ssid; int rssi; bool secure; };
bool netScan(std::vector<NetSeen> &out, String &why);                       // blocking, a few seconds
bool netJoin(const String &ssid, const String &pass, String &why, bool (*cancel)() = nullptr, uint32_t timeoutMs = 15000);   // cancel(): polled while waiting; true gives up

static const int NET_HANDSHAKE_S = 8;
static const int NET_CONNECT_MS = 5000;
void netTrust(WiFiClientSecure &tls);
class HTTPClient;
bool netReadBody(HTTPClient &http, String &out, size_t max);
String netFailText(const char *who, int code);

// HTTPS POST written by hand (the core's HTTPClient fails to send bodies on this board):
// returns the HTTP status, or a negative HTTPC_ERROR_* code. `headers`: extra lines, each ending in \r\n.
int netPost(const char *host, const char *path, const String &headers, const uint8_t *body, size_t len, String &reply, uint32_t timeoutMs);
