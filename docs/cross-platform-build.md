# クロスプラットフォーム・ビルド（Windows / Linux 共用）

`agri-*` firmware（この core を使う `-poe` ノード、および `agri-temp-wifi` /
`agri-temp-poe` など）を **Windows と Linux の両方**で同じように
ビルド／書き込みするための一次情報。各リポジトリの README はここを参照する。

## 大前提：`platformio.ini` は OS 非依存

- `platform` / `lib_deps` は git URL、絶対パスは無い
- `upload_port` は基本 **未指定＝自動検出**（Win=`COMx` / Linux=`/dev/ttyACM*`・`/dev/ttyUSB*`）
- `.pio/` は各リポジトリで gitignore 済み

→ **同じリポジトリを Win / Linux で clone すれば、そのまま両方でビルドできる。**
OS を縛るのは「手順の書き方」だけ。`pio` を PATH に通せば以下は共通：

```bash
pio run -e <env>              # ビルド
pio run -e <env> -t upload    # USB 書き込み（ポート自動検出）
pio run -e <env> -t upload --upload-port <host>.local   # OTA（ArduinoOTA）
pio device monitor            # シリアルモニタ
```

`<env>` は各リポジトリの `platformio.ini` の `[env:...]` 名（例 `m5atom-poe`,
`m5atoms3-poe`, `m5atomu-wifi`）。

## ⚠ `~/.platformio`（toolchain 本体）は OS 間で共有しない

espressif32 / pioarduino の toolchain は **OS 別ネイティブバイナリ**。Win と Linux で
同じ `~/.platformio`（NAS / 共有フォルダ等）を指すと壊れる。**共有するのはリポジトリだけ**、
toolchain は各マシンが個別に持つ（Linux 初回ビルドで数百 MB を DL する）。

## Linux 初回セットアップ

```bash
# 1) PlatformIO Core
pipx install platformio          # もしくは VSCode の PlatformIO 拡張

# 2) USB 書き込み権限：PlatformIO 公式 udev rules（推奨。dialout だけより確実）
#    FTDI / CP210x / CH34x / ESP32-S3 native USB(VID 0x303a) 等を一括カバー
curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core/develop/platformio/assets/system/99-platformio-udev.rules \
  | sudo tee /etc/udev/rules.d/99-platformio-udev.rules
sudo udevadm control --reload-rules && sudo udevadm trigger
sudo usermod -a -G dialout $USER   # 反映には再ログイン（またはリブート）
```

- ルール導入＋グループ反映後、基板を挿し直す
- 公式ドキュメント: https://docs.platformio.org/en/stable/core/installation/udev-rules.html
- ボード別の Linux デバイス名の目安:
  - AtomS3 / AtomS3 Lite（ESP32-S3 native USB）→ `/dev/ttyACM*`
  - 旧 ATOM / ATOM U（FTDI・CP210x・CH9102 等の USB-UART）→ `/dev/ttyUSB*`

## Windows の注意

- **git-bash からは `pio` を叩けない**（idf_tools が MSys 非対応）。**ネイティブ
  PowerShell / cmd で**実行する。
- `pio` を PATH に通すか、フルパス
  `& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e <env>` で呼ぶ。
- 文字化けするなら `$env:PYTHONIOENCODING="utf-8"`。
- OTA の `curl` は Windows では `curl.exe`（PowerShell の `curl` エイリアスは
  `Invoke-WebRequest` なので別物）。

## HTTP OTA（初回 USB 後は無線）

USB 書き込みは初回だけ。以後は Ethernet / WiFi 経由で更新できる：

```bash
# ArduinoOTA（pio 経由）
pio run -e <env> -t upload --upload-port <host>.local

# もしくは HTTP POST（WebUI の /api/ota。Win では curl.exe）
curl -F firmware=@.pio/build/<env>/firmware.bin http://<host>.local/api/ota
```

## GitHub Release（セルフ更新用の .bin）

`AgriOTA` のセルフ更新は `v<FW_VERSION>` タグと、`FW_BIN_NAME` に完全一致する
asset 名を要求する。`gh` の `path#name` 記法なら **コピー不要・Win/Linux 共通**：

```bash
pio run -e <env>
gh release create v<X.Y.Z> ".pio/build/<env>/firmware.bin#<FW_BIN_NAME>" \
  --title v<X.Y.Z> --notes "..."
```

（`#<FW_BIN_NAME>` が asset 名を保証するので、`Copy-Item` / `cp` でリネームする
必要はない。）
