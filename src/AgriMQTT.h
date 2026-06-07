// AgriMQTT.h — thin wrapper around PubSubClient driven by CommonConfig.
//
// The project owns what / when to publish; this header just handles
// connect / reconnect, LWT on "<prefix>/sys/<node_id>/online" (1/0),
// and the socket plumbing.

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

    // Per-node liveness under the agriha sys category; node_id in the path
    // so several nodes can share one house prefix (e.g. "agriha/2").
    char will[128];
    snprintf(will, sizeof(will), "%s/sys/%s/online",
             cfg.mqtt_topic_prefix, cfg.node_id);

    bool ok;
    if (cfg.mqtt_user[0]) {
      ok = mqtt.connect(cfg.node_id, cfg.mqtt_user, cfg.mqtt_pass,
                        will, 0, true, "0");
    } else {
      ok = mqtt.connect(cfg.node_id, nullptr, nullptr,
                        will, 0, true, "0");
    }
    Serial.printf("[MQTT] connect(%s:%u) = %s\n",
                  cfg.mqtt_host, cfg.mqtt_port, ok ? "OK" : "FAIL");
    if (ok) mqtt.publish(will, "1", true);
    return ok;
  }

  static bool connected() { return mqtt.connected(); }
  static void loop()      { mqtt.loop(); }
};

inline NetworkClient MQTT::client = NetworkClient{};
inline PubSubClient  MQTT::mqtt   = PubSubClient{};

} // namespace agri
