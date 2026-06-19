// AgriWebUI.h — Dashboard / Config / About + JSON API HTTP server.
//
// Owned by the library:
//   - page chrome (style, nav, head/foot)
//   - common-config form rows (MQTT + CCM envelope)
//   - request dispatch loop and the response/redirect helpers
//   - /api/config endpoint (common fields only)
//   - /api/dashboard endpoint (re-renders the project's sensor block for
//     the dashboard's no-reload refresh)
//
// Owned by the project (via the Hooks struct passed to begin()):
//   - dashboard sensor block HTML
//   - sensor-specific config form rows (CCM channel orders, sensor toggles, …)
//   - form-body parser for those extra fields (and a save callback)
//   - status JSON augmentation (sensor values + connection state)
//   - node title shown in the header

#pragma once

#include <Arduino.h>
#include <functional>
#include <ETH.h>
#include <Network.h>
#include <NetworkServer.h>
#include <NetworkClient.h>
#include <Update.h>
#include <ArduinoJson.h>
#include "AgriCommonConfig.h"
#include "AgriNetwork.h"
#include "AgriMQTT.h"

namespace agri {

struct WebHooks {
  // Title shown as <h2> in the navbar. Defaults to "agri-node".
  std::function<const char*()> nodeTitle = [](){ return "agri-node"; };
  // Dashboard sensor section (HTML fragment, no <div class=sec> wrapper).
  std::function<String()>      renderDashboardSensors = [](){ return String("(no sensors registered)"); };
  // Extra rows appended to the Config page's CCM <table>. Returned string
  // is concatenated verbatim, so use <tr><th>…</th><td>…</td></tr>.
  std::function<String()>      renderConfigSensorRows = [](){ return String(""); };
  // Called when /config receives POST. Body is the raw URL-encoded form.
  // Use the agri::parseForm* helpers below to pull fields.
  std::function<void(const String&)> applyConfigSensorForm = [](const String&){};
  // Augment the /api/status JSON with sensor / app values.
  std::function<void(JsonObject)>    addStatusFields       = [](JsonObject){};
  // Project's NVS save (called after applyConfigSensorForm so common
  // fields and sensor fields commit atomically).
  std::function<void()>              saveConfig             = [](){};
  // For the JS dashboard to know "MQTT host configured" / "CCM enabled".
  // The library fills these from CommonConfig if you don't override.
};

// ---- form helpers (also useful inside applyConfigSensorForm) -------------
inline String urlDecode(const String &s) {
  String out;
  out.reserve(s.length());
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    if (c == '+') { out += ' '; }
    else if (c == '%' && i + 2 < s.length()) {
      char hex[3] = {s[i + 1], s[i + 2], 0};
      out += (char)strtol(hex, nullptr, 16);
      i += 2;
    } else out += c;
  }
  return out;
}

inline void parseFormStr(const String &body, const char *key, char *dst, size_t n) {
  String needle = String(key) + "=";
  int p = body.indexOf(needle);
  if (p < 0) return;
  p += needle.length();
  int e = body.indexOf('&', p);
  String v = (e < 0) ? body.substring(p) : body.substring(p, e);
  String d = urlDecode(v);
  strlcpy(dst, d.c_str(), n);
}

inline int parseFormInt(const String &body, const char *key, int defv) {
  String needle = String(key) + "=";
  int p = body.indexOf(needle);
  if (p < 0) return defv;
  p += needle.length();
  int e = body.indexOf('&', p);
  String v = (e < 0) ? body.substring(p) : body.substring(p, e);
  return v.toInt();
}

inline bool parseFormBool(const String &body, const char *key) {
  return body.indexOf(String(key) + "=") >= 0;
}

// ---- HTTP response primitives --------------------------------------------
inline void sendResponse(NetworkClient &c, int status, const char *contentType,
                         const String &body) {
  c.print("HTTP/1.0 "); c.print(status);
  c.println(status == 200 ? " OK"
            : (status == 303 ? " See Other" : " Error"));
  c.print("Content-Type: "); c.println(contentType);
  c.print("Content-Length: "); c.println(body.length());
  c.println("Connection: close");
  c.println();
  c.print(body);
}

inline void sendRedirect(NetworkClient &c, const char *location) {
  c.println("HTTP/1.0 303 See Other");
  c.print("Location: "); c.println(location);
  c.println("Content-Length: 0");
  c.println("Connection: close");
  c.println();
}

// ---- page chrome ---------------------------------------------------------
inline String pageHead(const char *title, const char *navTitle) {
  String s; s.reserve(700);
  s  = F("<!DOCTYPE html><html lang=\"en\"><head><meta charset=UTF-8>"
         "<meta name=viewport content='width=device-width,initial-scale=1'>"
         "<title>");
  s += title; s += F("</title><style>"
         "body{font-family:sans-serif;margin:16px;background:#0f1011;color:#f7f8f8}"
         "h2{color:#5e6ad2}h3{color:#d0d6e0}"
         ".sec{background:#191a1b;border-radius:6px;padding:12px;margin:8px 0}"
         "table{border-collapse:collapse;width:100%;margin:6px 0}"
         "th,td{border:1px solid #2e2e2e;padding:5px 8px}"
         "th{background:#191a1b;color:#d0d6e0}"
         "input,select{padding:5px;background:#1a1a1f;color:#eee;border:1px solid #3e3e44;border-radius:3px}"
         "input[type=submit]{background:#1976d2;color:#fff;border:none;padding:8px 20px;cursor:pointer}"
         "a{color:#d0d6e0}"
         "</style></head><body>");
  s += "<h2>"; s += navTitle; s += F("</h2>"
         "<p><a href='/'>Dashboard</a> | <a href='/config'>Config</a> | <a href='/ota'>OTA</a> | <a href='/about'>About</a></p>");
  return s;
}

inline String pageDashboard(const WebHooks &h) {
  const char *t = h.nodeTitle();
  String s = pageHead(t, t);
  s += F("<div id=ota style='display:none;margin:8px 0;padding:8px 12px;background:#3a2a0a;border-left:4px solid #fc6;border-radius:4px'></div>");
  s += "<div class=sec id=sens>"; s += h.renderDashboardSensors(); s += "</div>";
  s += F("<div class=sec id=net>Loading…</div>"
         "<script>"
         "async function tick(){"
         "try{"
         "let r=await fetch('/api/status');let d=await r.json();"
         "let n='<h3>Network</h3><table>'"
         "+'<tr><th>Firmware</th><td>'+(d.fw_name||'')+' '+(d.fw_version||'')+'</td></tr>'"
         "+'<tr><th>IP</th><td>'+d.ip+'</td></tr>'"
         "+'<tr><th>Link</th><td>'+d.link+'</td></tr>'"
         "+'<tr><th>MQTT</th><td>'+(d.mqtt_connected?'connected':d.mqtt_host?'not connected':'not configured')+'</td></tr>'"
         "+'<tr><th>CCM</th><td>'+(d.ccm_enabled?'enabled':'disabled')+'</td></tr>'"
         "+'<tr><th>Uptime</th><td>'+d.uptime_s+' s</td></tr>'"
         "+'</table>';"
         "document.getElementById('net').innerHTML=n;"
         "let o=document.getElementById('ota');"
         "if(d.ota&&d.ota.state=='running'){o.style.display='block';o.innerHTML='Updating… '+d.ota.progress+'%';}"
         "else if(d.ota&&d.ota.state=='pending'){o.style.display='block';o.innerHTML='Update pending…';}"
         "else if(d.ota&&d.ota.state=='done'){o.style.display='block';o.innerHTML='Update complete. Restarting…';}"
         "else if(d.ota&&d.ota.state=='failed'){o.style.display='block';o.innerHTML='Update failed: '+(d.ota.error||'unknown');}"
         "else if(d.ota&&d.ota.latest){o.style.display='block';"
         "o.innerHTML='<b>New version '+d.ota.latest+' available.</b> <button onclick=\"doUpdate(\\''+d.ota.latest+'\\')\" style=\"margin-left:8px;background:#fc6;color:#222;border:0;padding:6px 14px;border-radius:3px;cursor:pointer;font-weight:bold\">Update</button>';}"
         "else{o.style.display='none';}"
         "}catch(e){}"
         "try{"
         "let f=await fetch('/api/dashboard');"
         "if(f.ok)document.getElementById('sens').innerHTML=await f.text();"
         "}catch(e){}"
         "}"
         "async function doUpdate(v){"
         "if(!confirm('Download v'+v+' from GitHub and flash?'))return;"
         "await fetch('/api/update',{method:'POST'});tick();"
         "}"
         "tick();setInterval(tick,3000);"
         "</script></body></html>");
  return s;
}

inline String pageConfig(const CommonConfig &c, const WebHooks &h) {
  String s = pageHead("Config", h.nodeTitle());
  auto row = [&](const char *label, const String &input) {
    s += "<tr><th>"; s += label; s += "</th><td>"; s += input; s += "</td></tr>";
  };
  s += F("<div class=sec><h3>MQTT</h3><form method=POST action='/config'><table>");
  row("Node ID",     "<input name=node_id value='"  + String(c.node_id)  + "'>");
  row("Hostname (mDNS .local)", "<input name=hostname value='" + String(c.hostname) + "'>");
  row("MQTT Host",   "<input name=mq_host value='"  + String(c.mqtt_host) + "'>");
  row("MQTT Port",   "<input type=number name=mq_port value='" + String(c.mqtt_port) + "'>");
  row("MQTT User",   "<input name=mq_user value='"  + String(c.mqtt_user) + "'>");
  row("MQTT Pass",   "<input type=password name=mq_pass value='" + String(c.mqtt_pass) + "'>");
  row("Topic / Prefix","<input name=mq_pfx value='" + String(c.mqtt_topic_prefix) + "'>");
  row("MQTT interval (s)", "<input type=number name=mq_int value='" + String(c.mqtt_interval_s) + "'>");
  s += F("</table><h3>UECS-CCM</h3><table>");
  row("CCM enabled", String("<input type=checkbox name=ccm_en ") + (c.ccm_enabled ? "checked" : "") + ">");
  row("CCM interval (s)", "<input type=number name=ccm_int value='" + String(c.ccm_interval_s) + "'>");
  row("Room",     "<input type=number name=ccm_room value='" + String(c.ccm_room) + "'>");
  row("Region",   "<input type=number name=ccm_reg value='"  + String(c.ccm_region) + "'>");
  row("Priority", "<input type=number name=ccm_pri value='"  + String(c.ccm_priority) + "'>");
  // ArSprout CCM-receive "ノード種別": the wire type is sent as
  // "<Type>.<ntype>". Default cMC. Empty = bare type.
  row("Node type (ArSprout ノード種別)",
      "<input name=ccm_nt value='" + String(c.ccm_ntype) + "'>");
  s += h.renderConfigSensorRows();
  s += F("</table><p><input type=submit value='Save'></p></form></div></body></html>");
  return s;
}

inline String pageAbout(const CommonConfig &c, const WebHooks &h,
                        const char *fwName, const char *fwVersion) {
  String s = pageHead("About", h.nodeTitle());
  s += F("<div class=sec><h3>About</h3><table>");
  s += "<tr><th>Firmware</th><td>"; s += fwName; s += " "; s += fwVersion; s += "</td></tr>";
  s += "<tr><th>Node ID</th><td>";  s += c.node_id; s += "</td></tr>";
  s += "<tr><th>Hostname</th><td>"; s += c.hostname; s += ".local</td></tr>";
  s += "<tr><th>MAC</th><td>";      s += ETH.macAddress(); s += "</td></tr>";
  s += "<tr><th>IP</th><td>";       s += ETH.localIP().toString(); s += "</td></tr>";
  s += "<tr><th>Uptime</th><td>";   s += String(millis() / 1000); s += " s</td></tr>";
  s += F("</table></div></body></html>");
  return s;
}

// Library applies its own form fields, then defers to the project for the
// sensor-specific ones, then calls saveConfig().
inline void applyCommonConfigForm(const String &body, CommonConfig &c) {
  parseFormStr(body, "node_id",  c.node_id,           sizeof(c.node_id));
  parseFormStr(body, "hostname", c.hostname,          sizeof(c.hostname));
  parseFormStr(body, "mq_host",  c.mqtt_host,         sizeof(c.mqtt_host));
  c.mqtt_port           = (uint16_t)parseFormInt(body, "mq_port", c.mqtt_port);
  parseFormStr(body, "mq_user",  c.mqtt_user,         sizeof(c.mqtt_user));
  parseFormStr(body, "mq_pass",  c.mqtt_pass,         sizeof(c.mqtt_pass));
  parseFormStr(body, "mq_pfx",   c.mqtt_topic_prefix, sizeof(c.mqtt_topic_prefix));
  c.mqtt_interval_s     = (uint16_t)parseFormInt(body, "mq_int",  c.mqtt_interval_s);
  c.ccm_enabled         = parseFormBool(body, "ccm_en");
  c.ccm_interval_s      = (uint16_t)parseFormInt(body, "ccm_int", c.ccm_interval_s);
  c.ccm_room            = (int16_t) parseFormInt(body, "ccm_room", c.ccm_room);
  c.ccm_region          = (int16_t) parseFormInt(body, "ccm_reg",  c.ccm_region);
  c.ccm_priority        = (int16_t) parseFormInt(body, "ccm_pri",  c.ccm_priority);
  parseFormStr(body, "ccm_nt", c.ccm_ntype, sizeof(c.ccm_ntype));
}

// ---- OTA (HTTP firmware upload) ------------------------------------------
inline String pageOta(const WebHooks &h) {
  String s = pageHead("OTA", h.nodeTitle());
  s += F("<div class=sec><h3>Firmware Update (OTA)</h3>"
         "<p>Pick a <code>firmware.bin</code> and upload. The node reboots after a verified flash. "
         "Do not power off during the update.</p>"
         "<input type=file id=f accept='.bin'> <input type=submit value='Update' onclick='up()'>"
         "<p id=st></p>"
         "<script>"
         "async function up(){var f=document.getElementById('f').files[0];if(!f){st.textContent='choose a .bin first';return;}"
         "var st=document.getElementById('st');st.textContent='Uploading '+f.size+' bytes...';"
         "try{var r=await fetch('/api/ota',{method:'POST',body:f});st.textContent=await r.text();}"
         "catch(e){st.textContent='done / device rebooting — reconnect in ~10s';}}"
         "</script></div></body></html>");
  return s;
}

// Stream the raw request body straight into the Update partition. Must run
// BEFORE the generic POST-body-into-String read (a ~800 KB image would never
// fit a String). Body = raw firmware.bin bytes (fetch(...,{body:file})).
inline void handleOtaUpload(NetworkClient &client, int contentLen) {
  if (contentLen <= 0 || !Update.begin(contentLen)) {
    sendResponse(client, 500, "text/plain", "OTA begin failed\n");
    return;
  }
  uint8_t buf[1024];
  int written = 0;
  uint32_t last = millis();
  while (written < contentLen) {
    int avail = client.available();
    if (avail > 0) {
      int n = client.read(buf, min(avail, (int)sizeof(buf)));
      if (n > 0) { Update.write(buf, n); written += n; last = millis(); }
    } else if (millis() - last > 10000) {
      break;  // stalled
    } else {
      delay(1);
    }
  }
  if (written == contentLen && Update.end(true)) {
    sendResponse(client, 200, "text/plain", "OK — flashed, rebooting\n");
    client.flush();
    delay(500);
    ESP.restart();
  } else {
    Update.abort();
    sendResponse(client, 500, "text/plain", "OTA failed (incomplete or verify error)\n");
  }
}

// ---- request loop --------------------------------------------------------
struct WebUI {
  static NetworkServer server;
  static const CommonConfig *cfg;
  static WebHooks            hooks;
  static const char         *fwName;
  static const char         *fwVersion;

  static void begin(const CommonConfig &c, const WebHooks &h,
                    const char *fw_name, const char *fw_version,
                    uint16_t port = 80) {
    cfg = &c; hooks = h; fwName = fw_name; fwVersion = fw_version;
    server.begin(port);
  }

  static void handle(bool linkUp, bool haveLease) {
    NetworkClient client = server.accept();
    if (!client) return;

    String reqLine; uint32_t t0 = millis();
    while (client.connected() && millis() - t0 < 500) {
      if (client.available()) {
        char ch = client.read();
        if (ch == '\n') break;
        if (ch != '\r') reqLine += ch;
      }
    }
    int sp1 = reqLine.indexOf(' ');
    int sp2 = (sp1 >= 0) ? reqLine.indexOf(' ', sp1 + 1) : -1;
    if (sp1 < 0 || sp2 < 0) { client.stop(); return; }
    String method = reqLine.substring(0, sp1);
    String path   = reqLine.substring(sp1 + 1, sp2);

    int contentLen = 0;
    while (client.connected() && millis() - t0 < 1000) {
      String h;
      while (client.available()) {
        char ch = client.read();
        if (ch == '\n') break;
        if (ch != '\r') h += ch;
      }
      if (h.length() == 0) break;
      if (h.startsWith("Content-Length:")) contentLen = h.substring(15).toInt();
    }

    // OTA upload: stream raw body to flash (skip the String body read below)
    if (method == "POST" && path == "/api/ota") {
      handleOtaUpload(client, contentLen);
      client.flush();
      client.stop();
      return;
    }

    String body;
    if (method == "POST" && contentLen > 0) {
      body.reserve(contentLen);
      while ((int)body.length() < contentLen && millis() - t0 < 2000) {
        if (client.available()) body += (char)client.read();
      }
    }

    if (method == "GET" && path == "/") {
      sendResponse(client, 200, "text/html; charset=utf-8", pageDashboard(hooks));
    } else if (method == "GET" && path == "/config") {
      sendResponse(client, 200, "text/html; charset=utf-8", pageConfig(*cfg, hooks));
    } else if (method == "POST" && path == "/config") {
      // const-cast: the writable config lives in the project; we accept
      // a const reference for read paths but mutate at POST time.
      char oldHostname[32];
      strlcpy(oldHostname, cfg->hostname, sizeof(oldHostname));
      applyCommonConfigForm(body, const_cast<CommonConfig&>(*cfg));
      hooks.applyConfigSensorForm(body);
      hooks.saveConfig();
      // hostname changed → re-announce <new>.local (mDNS + OTA) right away
      if (cfg->hostname[0] && strcmp(oldHostname, cfg->hostname) != 0)
        mdnsRestart(cfg->hostname);
      sendRedirect(client, "/config");
    } else if (method == "GET" && path == "/ota") {
      sendResponse(client, 200, "text/html; charset=utf-8", pageOta(hooks));
    } else if (method == "GET" && path == "/about") {
      sendResponse(client, 200, "text/html; charset=utf-8",
                   pageAbout(*cfg, hooks, fwName, fwVersion));
    } else if (method == "GET" && path == "/api/dashboard") {
      // HTML fragment for the dashboard's live sensor block (same renderer
      // as the initial page load, so projects need no extra code)
      sendResponse(client, 200, "text/html; charset=utf-8",
                   hooks.renderDashboardSensors());
    } else if (method == "GET" && path == "/api/status") {
      JsonDocument doc;
      JsonObject root = doc.to<JsonObject>();
      root["fw_name"]        = fwName;
      root["fw_version"]     = fwVersion;
      root["uptime_s"]       = millis() / 1000;
      root["ip"]             = ETH.localIP().toString();
      root["link"]           = linkUp ? (haveLease ? "up" : "no-lease") : "down";
      root["mqtt_host"]      = cfg->mqtt_host[0] ? cfg->mqtt_host : "";
      root["mqtt_connected"] = MQTT::connected();
      root["ccm_enabled"]    = cfg->ccm_enabled;
      JsonObject ota = root["ota"].to<JsonObject>();
      ota["state"]    = OTA::stateString();
      ota["progress"] = OTA::progress;
      if (OTA::updateAvailable) ota["latest"] = OTA::latestVersion;
      if (OTA::error.length())  ota["error"]  = OTA::error;
      hooks.addStatusFields(root);
      String out; serializeJson(doc, out);
      sendResponse(client, 200, "application/json", out);
    } else if (method == "POST" && path == "/api/update") {
      if (!OTA::updateAvailable) {
        sendResponse(client, 409, "text/plain", "no update available\n");
      } else if (OTA::state != OTA::IDLE && OTA::state != OTA::FAILED) {
        sendResponse(client, 409, "text/plain", "ota in progress\n");
      } else {
        OTA::schedule();
        sendResponse(client, 202, "text/plain", "ota scheduled\n");
      }
    } else if (method == "GET" && path == "/api/config") {
      JsonDocument doc;
      commonToJson(*cfg, doc.to<JsonObject>());
      String out; serializeJson(doc, out);
      sendResponse(client, 200, "application/json", out);
    } else {
      sendResponse(client, 404, "text/plain", "not found\n");
    }
    client.flush();
    client.stop();
  }
};

inline NetworkServer       WebUI::server    = NetworkServer{80};
inline const CommonConfig *WebUI::cfg       = nullptr;
inline WebHooks            WebUI::hooks     = WebHooks{};
inline const char         *WebUI::fwName    = "agri-node";
inline const char         *WebUI::fwVersion = "0.0.0";

} // namespace agri
