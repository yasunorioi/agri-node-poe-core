# agri-node-poe-core

**日本語** · [🇬🇧 English](README_en.md)

[OGMS](https://github.com/yasunorioi/OGMS) やその他の UECS-CCM コンシューマーへ
データを送る **M5Stack ATOM PoE** センサー／リレーノード向けの共通基盤です。

各ノードのスケッチがセンサー処理に専念できるよう、このライブラリが以下を
引き受けます。

- **W5500 Ethernet** — ESP-IDF `spi_w5500` ドライバ + ESP-IDF lwIP 経由
  （つまり mDNS / ArduinoOTA が実際に動作します。arduino-libraries/Ethernet
  の経路は lwIP をバイパスするため、下流ノードの v0.2 では mDNS が
  死んでいました）。
- **NVS ベースの設定** — 全ノードが共有するフィールド（node id /
  hostname / MQTT host+port+user+pass+prefix+interval / CCM
  enable+interval+room+region+priority）を保持します。ノード固有の
  センサーチャンネルはプロジェクト独自の struct に置きます。
- **組み込み HTTP UI** — Dashboard / Config / About と、`/api/status`
  および `/api/config` の JSON API。ライブラリが外枠と MQTT/CCM の行を
  レンダリングし、プロジェクトはフックコールバック（`std::function`）で
  センサーブロックを供給します。
- **MQTT パブリッシャー基盤** — PubSubClient 上に構築、LWT 対応。
- **UECS-CCM エンベロープビルダー**（UDP 224.0.0.1:16520、`ccm_rp2350_relay`
  や OGMS と同じ形の XML）。
- **mDNS**（`_http._tcp`）と **ArduinoOTA**（`_arduino._tcp`）。
- **単一ピクセル WS2812 LED** のステートマシン（青=起動 / 赤=リンク無し /
  マゼンタ=センサー無し / 黄=MQTT 無し / 緑=正常 / 白=送信フラッシュ）。

## インストール

PlatformIO（`platformio.ini`）:

```ini
platform = https://github.com/pioarduino/platform-espressif32.git#55.03.38
board = m5stack-atom
framework = arduino

lib_deps =
    https://github.com/yasunorioi/agri-node-poe-core.git
```

ライブラリは `ArduinoJson`、`PubSubClient`、`FastLED` を自動的に取り込みます。
（pioarduino フォークが必須です。arduino-esp32 3.x は
`ETH.begin(ETH_PHY_W5500, …)` を提供しますが、公式 PlatformIO の
`espressif32@6.x` は依然として arduino-esp32 2.x を使っており、これに
対応していません。）

## 最小サンプル

[`examples/minimal/minimal.ino`](examples/minimal/minimal.ino) を参照して
ください。起動して DHCP を取得し、`agri-min-01.local` をアドバタイズし、
ダッシュボードを配信する動作するノードです。センサーは不要です。そこから
`sensors.h` を追加し、`agri::WebHooks` コールバックを配線して、ダッシュ
ボードの行や CCM チャンネルのフォームフィールドをレンダリングします。

## このライブラリを使う下流ノード

- [`agri-rain-poe`](https://github.com/yasunorioi/agri-rain-poe) —
  DFRobot SEN0575 雨量計（Modbus RTU）→ `WRainfallAmt.cMC`
- [`agri-env-poe`](https://github.com/yasunorioi/agri-env-poe) —
  M5 ENV III（SHT30+QMP6988）+ SCD41 CO₂ → `InAirTemp.cMC`、
  `InAirHumid.cMC`、`InAirPressure.cMC`、`InAirCO2.cMC`
- [`agri-flow-poe`](https://github.com/yasunorioi/agri-flow-poe) —
  DIGITEN ホール効果流量計 →
  `WaterFlow.cMC`（L/min）+ `WaterCons.cMC`（L 累積）
- [`agri-solar-poe`](https://github.com/yasunorioi/agri-solar-poe) —
  M5 ADC Unit v1.1（ADS1110）+ PVSS-03 日射計 →
  `InRadiation.cMC`

## プロビジョニング用 SoftAP フォールバック（`AgriProvisionAP`）

有線ノードは、設定（hostname = mDNS 名、MQTT host、CCM エンベロープ）を
すべて WebUI から編集しますが、これは Ethernet 経由でしか到達できません。
ノードが LAN に乗れないときには、これがニワトリと卵の問題になります。
`AgriProvisionAP` がそのギャップを埋めます。一定の猶予期間 DHCP リースが
得られない場合、WPA2 SoftAP（SSID = hostname）を立ち上げ、キャッチオール
DNS を開始し、`WebUI::captive` をオンにして、**同じ** `AgriWebUI` を配信
します。これにより、スマホが AP に接続すると `/config` へ直接リダイレクト
されます。AP は Ethernet がリースを取得すると自動的に破棄されます。
2つ目の設定システムは不要で、WiFi と W5500 は ESP32-S3 上で共存します。

```cpp
#include <AgriProvisionAP.h>
void loop() {
  // ... WebUI::handle(...) ...
  agri::ProvisionAP::poll(agri::Network::have_lease, g_cfg.common.hostname);
}
```

WPA2 パスワードのデフォルトは `agrinode`（8 文字）です。ビルドごとに
`-DAGRI_AP_PASSWORD=\"...\"` で上書きします。動作する配線は `agri-temp-poe`
を参照してください。

## 下流スケッチ向けの注意

**ISR は `main.cpp` に置き、ヘッダには置かないこと。** ESP32 は割り込み
ハンドラを IRAM（`IRAM_ATTR`）に置くことを要求しますが、ヘッダ内で
`inline IRAM_ATTR` 関数を定義すると、ISR がロードするリテラルプールが
flash 側に配置されてしまい、リンカが
`dangerous relocation: l32r: literal placed after use` で拒否します。
ISR は `sensors.h` で `extern` 宣言し、`main.cpp` 内で一度だけ
`void IRAM_ATTR onMyPulse() { ... }` として定義してください。動作する
パターンは `agri-flow-poe` を参照してください。

## ライセンス

0BSD — 自由にコピー・改変してください。
