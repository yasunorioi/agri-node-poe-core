// AgriNode.h — single include for everything in agri-node-poe-core.
//
// Typical sketch shape:
//
//   #include <AgriNode.h>
//   #include "sensors.h"
//
//   struct AppConfig { agri::CommonConfig common; /* ... per-node fields ... */ };
//   AppConfig g_cfg;
//
//   void setup() {
//     Serial.begin(115200);
//     agri::Led::begin();
//     loadConfig();                       // your own NVS read
//     agri::Network::begin(g_cfg.common.hostname);
//     agri::Network::waitForLease();
//     agri::ccmBegin();
//     agri::MQTT::begin();
//     agri::WebUI::begin(g_cfg.common, hooks, "agri-myname", "1.0.0");
//     agri::mdnsBegin(g_cfg.common.hostname);
//     agri::otaBegin(g_cfg.common.hostname);
//   }
//
//   void loop() {
//     agri::otaHandle();
//     agri::WebUI::handle(agri::Network::link_up, agri::Network::have_lease);
//     // sensor poll + MQTT/CCM publish on cadence, LED update
//   }

#pragma once

#include "AgriCommonConfig.h"
#include "AgriNetwork.h"
#include "AgriLED.h"
#include "AgriMQTT.h"
#include "AgriCCM.h"
#include "AgriWebUI.h"
