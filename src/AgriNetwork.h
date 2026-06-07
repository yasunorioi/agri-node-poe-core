// AgriNetwork.h — bring up the W5500 via ESP-IDF's spi_w5500 driver,
// register mDNS + ArduinoOTA, and expose link / DHCP state to the rest
// of the node. Works on arduino-esp32 3.x (pioarduino fork).

#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <ETH.h>
#include <Network.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>

namespace agri {

struct W5500Pins {
  int sck   = 22;
  int miso  = 23;
  int mosi  = 33;
  int cs    = 19;
  int irq   = -1;
  int rst   = -1;
  uint8_t spi_freq_mhz = 20;
};

struct Network {
  // Filled in by the event callback installed by begin().
  static bool        link_up;
  static bool        have_lease;
  // ETH.setHostname() needs the hostname at ETH_START. Stash it here.
  static const char *hostname;

  static void onEvent(arduino_event_id_t event) {
    switch (event) {
      case ARDUINO_EVENT_ETH_START:
        if (hostname && *hostname) ETH.setHostname(hostname);
        Serial.println("[ETH] start");
        break;
      case ARDUINO_EVENT_ETH_CONNECTED:
        link_up = true;
        Serial.println("[ETH] link UP");
        break;
      case ARDUINO_EVENT_ETH_DISCONNECTED:
        link_up = false;
        have_lease = false;
        Serial.println("[ETH] link DOWN");
        break;
      case ARDUINO_EVENT_ETH_GOT_IP:
        have_lease = true;
        Serial.printf("[ETH] IP %s\n", ETH.localIP().toString().c_str());
        break;
      case ARDUINO_EVENT_ETH_LOST_IP:
        have_lease = false;
        Serial.println("[ETH] lost IP");
        break;
      default: break;
    }
  }

  // Configure SPI + start the ESP-IDF W5500 driver. Non-blocking.
  static void begin(const char *node_hostname, const W5500Pins &pins = W5500Pins{}) {
    hostname = node_hostname;
    ::Network.onEvent(onEvent);
    SPI.begin(pins.sck, pins.miso, pins.mosi);
    if (!ETH.begin(ETH_PHY_W5500, /*phy_addr*/1,
                   pins.cs, pins.irq, pins.rst,
                   SPI, pins.spi_freq_mhz)) {
      Serial.println("[ETH] ETH.begin failed");
    }
  }

  // Spin for up to ms milliseconds waiting for a DHCP lease. Useful so
  // dependent services (MDNS / MQTT) come up at a sensible moment.
  static void waitForLease(uint32_t ms = 8000) {
    uint32_t t0 = millis();
    while (!have_lease && millis() - t0 < ms) delay(50);
    if (!have_lease) Serial.println("[NET] no DHCP yet — continuing");
  }
};

inline bool Network::link_up         = false;
inline bool Network::have_lease      = false;
inline const char *Network::hostname = nullptr;

inline bool networkUp() {
  return Network::link_up && Network::have_lease;
}

// ---- mDNS + ArduinoOTA -----------------------------------------------------
inline void mdnsBegin(const char *hostname) {
  if (!MDNS.begin(hostname)) {
    Serial.println("[MDNS] begin failed");
    return;
  }
  MDNS.addService("http", "tcp", 80);
  Serial.printf("[MDNS] %s.local\n", hostname);
}

inline void otaBegin(const char *hostname) {
  ArduinoOTA.setHostname(hostname);
  ArduinoOTA.onStart([] { Serial.println("[OTA] start"); });
  ArduinoOTA.onEnd  ([] { Serial.println("[OTA] end");   });
  ArduinoOTA.onError([](ota_error_t e) { Serial.printf("[OTA] err %u\n", e); });
  ArduinoOTA.begin();
}

inline void otaHandle() { ArduinoOTA.handle(); }

// Re-announce mDNS + OTA under a new hostname without a reboot (used by the
// WebUI when /config changes the hostname). Note the DHCP-registered name
// (ETH.setHostname) still only updates on the next lease / reboot.
inline void mdnsRestart(const char *hostname) {
  ArduinoOTA.end();
  MDNS.end();
  mdnsBegin(hostname);
  otaBegin(hostname);
}

} // namespace agri
