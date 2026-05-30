// minimal.ino — smallest sketch that lights up a node with the library.
//
// No sensors; the dashboard says "no sensors" and the only thing
// publishing is a heartbeat. Useful to confirm the library + platform
// pin + W5500 wiring all work on a new build target.

#include <Arduino.h>
#include <Preferences.h>
#include <AgriNode.h>

const char *FW_NAME    = "agri-node-minimal";
const char *FW_VERSION = "0.1.0";

agri::CommonConfig g_cfg;

static void loadCfg() {
  Preferences p;
  agri::commonDefaults(g_cfg, "node_01", "agri-min-01",
                       "agri/min/01", 11);
  if (p.begin("agri-cfg", true)) { agri::commonLoad(g_cfg, p); p.end(); }
}

static void saveCfg() {
  Preferences p;
  if (p.begin("agri-cfg", false)) { agri::commonSave(g_cfg, p); p.end(); }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\n=== %s v%s ===\n", FW_NAME, FW_VERSION);

  agri::Led::begin();
  loadCfg();

  agri::Network::begin(g_cfg.hostname);
  agri::Network::waitForLease();

  agri::ccmBegin();
  agri::MQTT::begin();

  agri::WebHooks hooks;
  hooks.nodeTitle = [](){ return FW_NAME; };
  hooks.saveConfig = saveCfg;
  agri::WebUI::begin(g_cfg, hooks, FW_NAME, FW_VERSION);

  agri::mdnsBegin(g_cfg.hostname);
  agri::otaBegin(g_cfg.hostname);

  Serial.println("[BOOT] ready");
}

void loop() {
  agri::otaHandle();
  agri::WebUI::handle(agri::Network::link_up, agri::Network::have_lease);

  if (agri::networkUp() && agri::MQTT::hasHost(g_cfg)) {
    if (!agri::MQTT::connected()) {
      static uint32_t lastTry = 0;
      if (millis() - lastTry > 5000) { lastTry = millis(); agri::MQTT::reconnect(g_cfg); }
    } else agri::MQTT::loop();
  }

  agri::LedState desired = agri::networkUp() ? agri::LED_OK : agri::LED_NO_LINK;
  agri::Led::set(desired);

  delay(20);
}
