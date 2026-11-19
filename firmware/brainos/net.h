// Wi-Fi: on for a while after the last interaction (wifi.idle_min) or while recordings wait,
// off otherwise. The clock, brain.local and TLS helpers live here too.
#pragma once
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <vector>

void netBegin();
void netTouch();                 // an interaction: keeps the radio on for another wifi.idle_min
void netTick();                  // call from loop(): joins, times out, switches off
bool netUp();
bool netWanted();                // the window is open (or work is pending)
bool netBusy();                  // a request is in flight: don't switch the radio off
void netSetBusy(bool);
String netName();
String netAddress();
int netSignal();
bool timeKnown();

struct NetSeen { String ssid; int rssi; bool secure; };
bool netScan(std::vector<NetSeen> &out, String &why);                       // blocking, a few seconds
bool netJoin(const String &ssid, const String &pass, String &why, uint32_t timeoutMs = 15000);

static const int NET_HANDSHAKE_S = 8;
static const int NET_CONNECT_MS = 5000;
static const int NET_ERR_BAD_BODY = -200;
void netTrust(WiFiClientSecure &tls);
class HTTPClient;
bool netReadBody(HTTPClient &http, String &out, size_t max);
String netFailText(const char *who, int code);

// HTTPS POST written by hand (the core's HTTPClient fails to send bodies on this board):
// returns the HTTP status, or a negative HTTPC_ERROR_* code. `headers`: extra lines, each ending in \r\n.
int netPost(const char *host, const char *path, const String &headers, const uint8_t *body, size_t len, String &reply, uint32_t timeoutMs);
