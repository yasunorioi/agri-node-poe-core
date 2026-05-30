// AgriMQTT.h — thin wrapper around PubSubClient driven by CommonConfig.
//
// The project owns what / when to publish; this header just handles
// connect / reconnect, LWT on "<prefix>/status", and the socket plumbing.

#pragma once

#include <Arduino.h>
#include <NetworkClient.h>
#include <PubSubClient.h>
#include "AgriCommonConfig.h"

namespace agri {

struct MQTT {
  static NetworkClient client;
  static PubSubClient  mqtt;

  static void begin(uint16_t buffer = 512, uint16_t keepalive_s = 30) {
    mqtt.setClient(client);
    mqtt.setBufferSize(buffer);
    mqtt.setKeepAlive(keepalive_s);
  }

  static bool hasHost(const CommonConfig &cfg) {
    return cfg.mqtt_host[0] != '\0';
  }

  // Connect (or reconnect) using the common config + LWT.
  // Returns true if connected at exit.
  static bool reconnect(const CommonConfig &cfg) {
    if (!hasHost(cfg)) return false;
    if (mqtt.connected()) return true;
    mqtt.setServer(cfg.mqtt_host, cfg.mqtt_port);

    char will[96];
    snprintf(will, sizeof(will), "%s/status", cfg.mqtt_topic_prefix);

    bool ok;
    if (cfg.mqtt_user[0]) {
      ok = mqtt.connect(cfg.node_id, cfg.mqtt_user, cfg.mqtt_pass,
                        will, 0, true, "offline");
    } else {
      ok = mqtt.connect(cfg.node_id, nullptr, nullptr,
                        will, 0, true, "offline");
    }
    Serial.printf("[MQTT] connect(%s:%u) = %s\n",
                  cfg.mqtt_host, cfg.mqtt_port, ok ? "OK" : "FAIL");
    if (ok) mqtt.publish(will, "online", true);
    return ok;
  }

  static bool connected() { return mqtt.connected(); }
  static void loop()      { mqtt.loop(); }
};

inline NetworkClient MQTT::client = NetworkClient{};
inline PubSubClient  MQTT::mqtt   = PubSubClient{};

} // namespace agri
