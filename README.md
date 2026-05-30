# agri-node-poe-core

Shared infrastructure for **M5Stack ATOM PoE** sensor / relay nodes that feed
[OGMS](https://github.com/yasunorioi/OGMS) and other UECS-CCM consumers.

What this library handles for you so the per-node sketch can focus on
sensor wrangling:

- **W5500 Ethernet** via the ESP-IDF `spi_w5500` driver + ESP-IDF lwIP
  (i.e. mDNS / ArduinoOTA actually work — the arduino-libraries/Ethernet
  path bypasses lwIP, which is why mDNS was dead in v0.2 of the
  downstream nodes).
- **NVS-backed config** with the fields every node shares (node id /
  hostname / MQTT host+port+user+pass+prefix+interval / CCM
  enable+interval+room+region+priority). Per-node sensor channels live
  in the project's own struct.
- **Embedded HTTP UI** — Dashboard / Config / About + JSON APIs at
  `/api/status` and `/api/config`. The library renders the chrome and
  the MQTT/CCM rows; the project supplies the sensor block via
  hook callbacks (`std::function`).
- **MQTT publisher base** built on PubSubClient, with LWT.
- **UECS-CCM envelope builder** (UDP 224.0.0.1:16520, XML in the same
  shape as `ccm_rp2350_relay` and OGMS).
- **mDNS** (`_http._tcp`) and **ArduinoOTA** (`_arduino._tcp`).
- **Single-pixel WS2812 LED** state machine (blue boot / red no-link /
  magenta no-sensor / yellow no-MQTT / green OK / white publish-flash).

## Install

PlatformIO (`platformio.ini`):

```ini
platform = https://github.com/pioarduino/platform-espressif32.git#55.03.38
board = m5stack-atom
framework = arduino

lib_deps =
    https://github.com/yasunorioi/agri-node-poe-core.git
```

The library pulls in `ArduinoJson`, `PubSubClient`, `FastLED` automatically.
(The pioarduino fork is required: arduino-esp32 3.x ships
`ETH.begin(ETH_PHY_W5500, …)` — the official PlatformIO `espressif32@6.x`
still uses arduino-esp32 2.x which doesn't.)

## Minimal example

See [`examples/minimal/minimal.ino`](examples/minimal/minimal.ino) for a
working node that boots, gets DHCP, advertises `agri-min-01.local`, and
serves the dashboard — no sensors required. From there, add a
`sensors.h` and wire the `agri::WebHooks` callbacks to render your
dashboard rows and CCM channel form fields.

## Downstream nodes using this library

- [`agri-rain-poe`](https://github.com/yasunorioi/agri-rain-poe) —
  DFRobot SEN0575 rain gauge (Modbus RTU) → `WRainfallAmt.cMC`
- [`agri-env-poe`](https://github.com/yasunorioi/agri-env-poe) —
  M5 ENV III (SHT30+QMP6988) + SCD41 CO₂ → `InAirTemp.cMC`,
  `InAirHumid.cMC`, `InAirPressure.cMC`, `InAirCO2.cMC`
- [`agri-flow-poe`](https://github.com/yasunorioi/agri-flow-poe) —
  DIGITEN hall-effect flow meter →
  `WaterFlow.cMC` (L/min) + `WaterCons.cMC` (L cumulative)
- [`agri-solar-poe`](https://github.com/yasunorioi/agri-solar-poe) —
  M5 ADC Unit v1.1 (ADS1110) + PVSS-03 pyranometer →
  `InRadiation.cMC`

## License

0BSD — copy and adapt freely.
