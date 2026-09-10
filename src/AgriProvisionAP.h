// AgriProvisionAP.h — SoftAP provisioning fallback for the wired -poe fleet.
//
// The chicken-and-egg problem these nodes have: all of their config (hostname
// = mDNS .local name, MQTT host, CCM envelope, …) is edited through the WebUI,
// which is only reachable over Ethernet. So if the node can't get on the LAN —
// cable out, no DHCP, or a just-unboxed unit whose IP nobody knows — there is
// no way to configure it short of a USB serial console.
//
// This module closes that gap WITHOUT a second config system. The ESP32-S3 on
// the AtomS3 already has a WiFi radio sitting idle (these nodes run on wired
// Ethernet via the W5500). When Ethernet has no DHCP lease for a grace period,
// we bring up a WPA2 SoftAP and a catch-all DNS, flip WebUI::captive on, and
// serve the SAME AgriWebUI the wired node serves. A phone joins the AP, its OS
// captive-detection probe is bounced to /config, and you set the hostname /
// MQTT host right there. The moment Ethernet gets a lease, the AP is torn down.
//
// WiFi (esp_wifi) and Ethernet (esp_eth) are independent netifs and coexist
// fine on arduino-esp32 3.x; the lwIP WebUI server listens on 0.0.0.0 so it
// already accepts on the SoftAP interface with no extra wiring.
//
// Usage (see agri-temp-poe main.cpp):
//   setup(): nothing — begin() is lazy, driven entirely from poll().
//   loop():  agri::ProvisionAP::poll(agri::Network::have_lease,
//                                    g_cfg.common.hostname);
//
// The SSID is the node hostname (e.g. agri-temp-poe-01). The WPA2 password is
// a fleet-wide fixed default (document it in the node README); override at
// build time with -DAGRI_AP_PASSWORD=\"...\" if a site needs its own.

#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include "AgriWebUI.h"   // for agri::WebUI::captive

namespace agri {

// WPA2 requires an 8..63 char passphrase. Fleet default; overridable per build.
#ifndef AGRI_AP_PASSWORD
#define AGRI_AP_PASSWORD "agrinode"
#endif

struct ProvisionAP {
  static DNSServer dns;
  static bool      active;          // AP + DNS currently up
  static uint32_t  noLeaseSinceMs;  // millis() of first observed no-lease, 0 = have lease
  static uint32_t  graceMs;         // tolerate this much no-lease before raising the AP

  // Raise the SoftAP + catch-all DNS and put the WebUI into captive mode.
  static void start(const char *ssid, const char *password) {
    if (active) return;
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssid, password);
    IPAddress ip = WiFi.softAPIP();              // 192.168.4.1 by default
    dns.setErrorReplyCode(DNSReplyCode::NoError);
    dns.start(53, "*", ip);                      // resolve every name to the AP
    WebUI::captive = true;
    active = true;
    Serial.printf("[AP] provisioning SoftAP up — SSID=%s  http://%s/\n",
                  ssid, ip.toString().c_str());
  }

  // Tear the AP down and return the WebUI to normal (wired) behaviour.
  static void stop() {
    if (!active) return;
    dns.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    WebUI::captive = false;
    active = false;
    Serial.println("[AP] provisioning SoftAP down — Ethernet lease restored");
  }

  // Call every loop(). Brings the AP up after graceMs of continuous no-lease,
  // and tears it down the instant a DHCP lease returns. The grace window keeps
  // a brief link flap from thrashing the radio.
  static void poll(bool haveLease, const char *ssid,
                   const char *password = AGRI_AP_PASSWORD) {
    uint32_t now = millis();
    if (haveLease) {
      noLeaseSinceMs = 0;
      if (active) stop();
      return;
    }
    if (noLeaseSinceMs == 0) noLeaseSinceMs = now ? now : 1;  // arm (0 is sentinel)
    if (!active && now - noLeaseSinceMs >= graceMs) start(ssid, password);
    if (active) dns.processNextRequest();
  }
};

inline DNSServer ProvisionAP::dns;
inline bool      ProvisionAP::active         = false;
inline uint32_t  ProvisionAP::noLeaseSinceMs = 0;
inline uint32_t  ProvisionAP::graceMs        = 15000;  // 15 s after boot / link loss

} // namespace agri
