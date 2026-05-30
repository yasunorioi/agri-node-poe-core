// AgriCommonConfig.h — fields shared by every agri-node (network, MQTT,
// CCM envelope). The project extends with its own sensor-specific
// channels and is responsible for stitching the two together at
// load / save / form / json time.

#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>

namespace agri {

struct CommonConfig {
  // identity
  char     node_id[16];
  char     hostname[32];

  // MQTT
  char     mqtt_host[64];         // empty disables MQTT
  uint16_t mqtt_port;
  char     mqtt_user[32];
  char     mqtt_pass[32];
  char     mqtt_topic_prefix[64];
  uint16_t mqtt_interval_s;

  // UECS-CCM envelope. Per-sensor "order" stays in the project config.
  bool     ccm_enabled;
  uint16_t ccm_interval_s;
  int16_t  ccm_room;
  int16_t  ccm_region;
  int16_t  ccm_priority;
};

inline void commonDefaults(CommonConfig &c,
                           const char *default_node_id,
                           const char *default_hostname,
                           const char *default_mqtt_prefix,
                           int16_t default_ccm_region) {
  strlcpy(c.node_id,           default_node_id,     sizeof(c.node_id));
  strlcpy(c.hostname,          default_hostname,    sizeof(c.hostname));
  strlcpy(c.mqtt_host,         "",                  sizeof(c.mqtt_host));
  c.mqtt_port           = 1883;
  strlcpy(c.mqtt_user,         "",                  sizeof(c.mqtt_user));
  strlcpy(c.mqtt_pass,         "",                  sizeof(c.mqtt_pass));
  strlcpy(c.mqtt_topic_prefix, default_mqtt_prefix, sizeof(c.mqtt_topic_prefix));
  c.mqtt_interval_s     = 30;
  c.ccm_enabled         = false;
  c.ccm_interval_s      = 10;
  c.ccm_room            = 1;
  c.ccm_region          = default_ccm_region;
  c.ccm_priority        = 29;
}

inline void commonLoad(CommonConfig &c, Preferences &p) {
  p.getString("node_id",  c.node_id,           sizeof(c.node_id));
  p.getString("hostname", c.hostname,          sizeof(c.hostname));
  p.getString("mq_host",  c.mqtt_host,         sizeof(c.mqtt_host));
  c.mqtt_port = p.getUShort("mq_port", c.mqtt_port);
  p.getString("mq_user",  c.mqtt_user,         sizeof(c.mqtt_user));
  p.getString("mq_pass",  c.mqtt_pass,         sizeof(c.mqtt_pass));
  p.getString("mq_pfx",   c.mqtt_topic_prefix, sizeof(c.mqtt_topic_prefix));
  c.mqtt_interval_s = p.getUShort("mq_int", c.mqtt_interval_s);
  c.ccm_enabled     = p.getBool  ("ccm_en",  c.ccm_enabled);
  c.ccm_interval_s  = p.getUShort("ccm_int", c.ccm_interval_s);
  c.ccm_room        = p.getShort ("ccm_room", c.ccm_room);
  c.ccm_region      = p.getShort ("ccm_reg",  c.ccm_region);
  c.ccm_priority    = p.getShort ("ccm_pri",  c.ccm_priority);
}

inline void commonSave(const CommonConfig &c, Preferences &p) {
  p.putString("node_id",  c.node_id);
  p.putString("hostname", c.hostname);
  p.putString("mq_host",  c.mqtt_host);
  p.putUShort("mq_port",  c.mqtt_port);
  p.putString("mq_user",  c.mqtt_user);
  p.putString("mq_pass",  c.mqtt_pass);
  p.putString("mq_pfx",   c.mqtt_topic_prefix);
  p.putUShort("mq_int",   c.mqtt_interval_s);
  p.putBool  ("ccm_en",   c.ccm_enabled);
  p.putUShort("ccm_int",  c.ccm_interval_s);
  p.putShort ("ccm_room", c.ccm_room);
  p.putShort ("ccm_reg",  c.ccm_region);
  p.putShort ("ccm_pri",  c.ccm_priority);
}

inline void commonToJson(const CommonConfig &c, JsonObject root) {
  root["node_id"]   = c.node_id;
  root["hostname"]  = c.hostname;
  JsonObject mqtt = root["mqtt"].to<JsonObject>();
  mqtt["host"]       = c.mqtt_host;
  mqtt["port"]       = c.mqtt_port;
  mqtt["user"]       = c.mqtt_user;
  mqtt["prefix"]     = c.mqtt_topic_prefix;
  mqtt["interval_s"] = c.mqtt_interval_s;
  JsonObject ccm = root["ccm"].to<JsonObject>();
  ccm["enabled"]    = c.ccm_enabled;
  ccm["interval_s"] = c.ccm_interval_s;
  ccm["room"]       = c.ccm_room;
  ccm["region"]     = c.ccm_region;
  ccm["priority"]   = c.ccm_priority;
}

} // namespace agri
