// AgriOTA.h — Self-update from GitHub release binary assets.
//
// Boot-time:  agri::OTA::begin(repo, binName, currentVersion);
//             agri::OTA::checkLatest();         // after network is up
//
// Loop:       agri::OTA::poll();                // runs OTA when state==PENDING,
//                                               // and re-checks GitHub once a day
//
// Semi-automatic by design: the device polls the latest release tag at boot and
// then once every 24 h, surfaces a dashboard button when a newer version exists,
// and only flashes when the user clicks it (POST /api/update). It never applies
// an update on its own.
//
// Dashboard:  when latest > current, a yellow "Update to vX.X.X" button
//             appears (rendered by AgriWebUI). Click it -> POST /api/update ->
//             AgriOTA streams the .bin from the release asset, flashes to the
//             OTA partition via the Update.h API, ESP.restart().
//
// URL convention (project-controlled):
//   GET https://api.github.com/repos/<repo>/releases/latest    -> tag_name
//   GET https://github.com/<repo>/releases/download/v<tag>/<binName>
//
// TLS: WiFiClientSecure / NetworkClientSecure with setInsecure(). No cert
// pinning — fine for hobby use over a home LAN to github.com.

#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <Update.h>

namespace agri {
namespace OTA {

enum State { IDLE, PENDING, RUNNING, DONE, FAILED };

inline volatile State state           = IDLE;
inline String         latestVersion;
inline String         releaseUrl;
inline String         error;
inline bool           updateAvailable = false;
inline int            progress        = 0;
inline const char    *repo            = nullptr;
inline const char    *binName         = nullptr;
inline const char    *currentVersion  = nullptr;

// Daily background re-check (semi-automatic). checkLatest() stamps lastCheckMs;
// poll() re-runs it once recheckIntervalMs has elapsed. Set to 0 to disable.
inline uint32_t       recheckIntervalMs = 24UL * 60 * 60 * 1000;
inline uint32_t       lastCheckMs       = 0;

inline const char *stateString() {
  switch (state) {
    case PENDING: return "pending";
    case RUNNING: return "running";
    case DONE:    return "done";
    case FAILED:  return "failed";
    default:      return "idle";
  }
}

inline bool semverNewer(const String &a, const String &b) {
  int ai[3] = {0, 0, 0}, bi[3] = {0, 0, 0};
  sscanf(a.c_str(), "%d.%d.%d", &ai[0], &ai[1], &ai[2]);
  sscanf(b.c_str(), "%d.%d.%d", &bi[0], &bi[1], &bi[2]);
  for (int i = 0; i < 3; i++) {
    if (ai[i] > bi[i]) return true;
    if (ai[i] < bi[i]) return false;
  }
  return false;
}

inline String extractJsonString(const String &body, const char *key) {
  String needle = String("\"") + key + "\":\"";
  int p = body.indexOf(needle);
  if (p < 0) return "";
  p += needle.length();
  int e = body.indexOf('"', p);
  if (e <= p) return "";
  return body.substring(p, e);
}

inline void begin(const char *repo_, const char *binName_, const char *currentVersion_) {
  repo           = repo_;
  binName        = binName_;
  currentVersion = currentVersion_;
}

inline void checkLatest() {
  if (!repo || !currentVersion) return;
  lastCheckMs = millis();          // stamp up-front so a failed check waits a full interval
  NetworkClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(5000);
  String url = String("https://api.github.com/repos/") + repo + "/releases/latest";
  if (!http.begin(client, url)) {
    Serial.println("[OTA] http.begin failed");
    return;
  }
  http.addHeader("User-Agent", "agri-node-poe-core");
  int code = http.GET();
  if (code == 200) {
    String body = http.getString();
    String tag  = extractJsonString(body, "tag_name");
    String html = extractJsonString(body, "html_url");
    if (tag.length() > 0) {
      String ver = (tag.startsWith("v") || tag.startsWith("V")) ? tag.substring(1) : tag;
      latestVersion   = ver;
      releaseUrl      = html;
      updateAvailable = semverNewer(ver, currentVersion);
      Serial.printf("[OTA] latest=%s current=%s update=%d\n",
                    ver.c_str(), currentVersion, updateAvailable ? 1 : 0);
    } else {
      Serial.println("[OTA] tag_name not found");
    }
  } else {
    Serial.printf("[OTA] check HTTP %d\n", code);
  }
  http.end();
}

inline void schedule() {
  if (state == IDLE || state == FAILED) state = PENDING;
}

inline void run() {
  if (!repo || !binName || !latestVersion.length()) {
    error = "not configured";
    state = FAILED;
    return;
  }
  state    = RUNNING;
  progress = 0;
  error    = "";

  String url = String("https://github.com/") + repo +
               "/releases/download/v" + latestVersion + "/" + binName;
  Serial.printf("[OTA] GET %s\n", url.c_str());

  NetworkClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(15000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) {
    error = "http.begin failed";
    state = FAILED;
    return;
  }
  http.addHeader("User-Agent", "agri-node-poe-core-OTA");
  int code = http.GET();
  if (code != 200) {
    error = "HTTP " + String(code);
    state = FAILED;
    http.end();
    return;
  }
  int total = http.getSize();
  if (total <= 0) {
    error = "no content-length";
    state = FAILED;
    http.end();
    return;
  }
  if (!Update.begin((size_t)total)) {
    error = String("Update.begin: ") + Update.errorString();
    state = FAILED;
    http.end();
    return;
  }
  NetworkClient *stream = http.getStreamPtr();
  uint8_t buf[1024];
  int written = 0;
  unsigned long lastYield = millis();
  while (http.connected() && written < total) {
    size_t avail = stream->available();
    if (avail) {
      size_t toRead = avail > sizeof(buf) ? sizeof(buf) : avail;
      int r = stream->readBytes(buf, toRead);
      if (r <= 0) break;
      if (Update.write(buf, r) != (size_t)r) {
        error = String("write: ") + Update.errorString();
        Update.abort();
        state = FAILED;
        http.end();
        return;
      }
      written += r;
      progress = (int)((int64_t)written * 100 / total);
    } else {
      delay(1);
    }
    if (millis() - lastYield > 50) { yield(); lastYield = millis(); }
  }
  http.end();
  if (written != total) {
    error = "short read";
    Update.abort();
    state = FAILED;
    return;
  }
  if (!Update.end(true)) {
    error = String("Update.end: ") + Update.errorString();
    state = FAILED;
    return;
  }
  Serial.println("[OTA] success, restarting");
  state = DONE;
  delay(500);
  ESP.restart();
}

inline void poll() {
  if (state == PENDING) { run(); return; }
  if (state == RUNNING) return;
  // Daily background re-check. Skips while an update is mid-flight (guarded
  // above); harmless to keep re-confirming an already-available update.
  if (repo && currentVersion && recheckIntervalMs &&
      (uint32_t)(millis() - lastCheckMs) >= recheckIntervalMs) {
    checkLatest();
  }
}

}  // namespace OTA
}  // namespace agri
