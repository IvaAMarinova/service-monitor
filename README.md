# Service Monitor: LILYGO T-Display S3

This is an always-on desktop status board. It connects to Wi-Fi and checks a list of HTTP/HTTPS services every 10 seconds. The built-in 1.9" display shows each service's health and latency.

```
╭─ ~/services ──────── 17:42 ───────── status ─╮
│ ●  Cloudflare                          major │
│ ●  Claude                              minor │
│ ●  GitHub                                 ok │
│ ●  AWS eu-central-1                       ok │
╰─ > polled 4s ago_ ─────────── wifi -51dBm ───╯
```

Green-phosphor terminal theme, landscape 320×170, JetBrains Mono.

## Hardware

- LILYGO T-Display S3 (ESP32-S3, 16 MB flash, 8 MB OPI PSRAM), the non-touch or touch version
- Built-in ST7789 1.9" panel, 170×320, on an 8-bit parallel bus
- A USB-C cable that carries data (some cables are charge-only)

## Install PlatformIO

1. Install [VS Code](https://code.visualstudio.com/).
2. In VS Code, open Extensions, search for **PlatformIO IDE** and install it, then restart VS Code.
3. Optional CLI only: `pipx install platformio` (or `pip install platformio`).

You don't need the Arduino IDE. PlatformIO downloads the ESP32 toolchain and the libraries listed in `platformio.ini` the first time you build.

## Open the project

```bash
git clone <your-repo-url> service-monitor
code service-monitor
```

PlatformIO detects `platformio.ini` automatically.

## Create `secrets.h`

```bash
cp include/secrets.example.h include/secrets.h
```

Edit `include/secrets.h`:

```c
#define WIFI_SSID     "MyNetwork"
#define WIFI_PASSWORD "MyPassword"
#define OTA_PASSWORD  ""          // optional
```

`secrets.h` is git-ignored. The ESP32-S3 supports **2.4 GHz Wi-Fi only**.

## Configure services

All endpoints live in **`include/services.h`**. Edit the list, then push it over Wi-Fi with `pio run -e t-display-s3-ota -t upload`:

```cpp
constexpr ServiceConfig SERVICES[] = {
    statuspage("Claude",       "https://status.claude.com"),
    statuspage("GitHub",       "https://www.githubstatus.com"),
    statuspage("Cloudflare",   "https://www.cloudflarestatus.com"),
    http("AWS eu-central-1",   "https://sts.eu-central-1.amazonaws.com/"),
    http("Google",             "https://www.google.com/generate_204", 204),
    // http("Prod API",        "https://api.example.com/healthz", 200),
};
```

There are two kinds of checks:

| Check | What it does | When to use it |
|---|---|---|
| `statuspage(name, baseUrl)` | Fetches `<baseUrl>/api/v2/status.json` from an Atlassian Statuspage and maps the provider's own indicator: `none` = healthy, `minor` or `maintenance` = degraded, `major` or `critical` = down | SaaS providers that publish a Statuspage (Claude, GitHub, Cloudflare, OpenAI, Atlassian, and others). Catches incidents the provider declares even when their site still answers. |
| `http(name, url[, expectedCode])` | A single GET that records the status code and the time to the response headers on a fresh connection (DNS + TCP + TLS + server). | Your own health endpoints, and clouds without a Statuspage, such as AWS and Google. |

The checks run one after another every `CHECK_INTERVAL_MS` (10 s). With about 0.6–0.9 s per check, 10 services take roughly one interval; more than that just makes each cycle longer.

The screen fits 5 rows. Rows are sorted by severity (DOWN, then DEGRADED, then healthy), so anything wrong is always visible. When there are more services, the footer shows `+N more`.

### Status rules

| State | `http` | `statuspage` | Shown as |
|---|---|---|---|
| HEALTHY (green) | 2xx/3xx, or exactly `expectedCode`, within `DEGRADED_LATENCY_MS` | `none` | `ok` |
| DEGRADED (amber) | Slower than 1500 ms, or 4xx, or the wrong code | `minor`, `maintenance` | `minor`, `maint`, `slow`, or the code (e.g. `404`) |
| DOWN (red) | Timeout, DNS/TCP/TLS failure, or 5xx | `major`, `critical` | `DOWN`, `major`, or the code (e.g. `503`) |

Things to know about the numbers:
- **Latency (serial log only) is pessimistic.** Each check opens a fresh TLS connection, and the ESP32's handshake alone costs about 500–700 ms. That's why the DEGRADED threshold is 1500 ms. Compare trends over time, not absolute values against `curl`.
- **No certificate validation** (`setInsecure()`). This tells you whether a service is reachable, not that the TLS chain is valid.
- **Redirects are not followed**, so a 3xx counts as healthy. Use `expectedCode` if you need an exact code.
- **Checks run from this device's network**, so a local ISP or DNS problem shows as everything going red at once.

## Build

VS Code: click the **✓ Build** button in the PlatformIO toolbar. CLI:

```bash
pio run
```

On a laptop, you can limit CPU use with `pio run -j 2`.

## First upload over USB

1. Connect the board with a USB-C data cable.
2. Run `pio run -t upload` (or click the **→ Upload** button in VS Code).
3. If the upload fails with "No serial port" or "Failed to connect", enter bootloader mode (see below) and upload again.
4. After uploading in bootloader mode, press **RST** once to start the new firmware.

## Serial monitor

```bash
pio device monitor
```

Typical output:

```
[wifi] connected, ip=192.168.1.42 rssi=-52 dBm
[time] synchronized: 2026-10-09 17:42:03 EEST
[ota] ready at service-monitor.local:3232
[check] GitHub         HEALTHY  code=200 142ms
[check] Example API    DEGRADED code=200 1312ms Slow response
```

## OTA updates

After the first USB flash, with the device on the same LAN as your computer:

```bash
pio run -e t-display-s3-ota -t upload
```

This uploads to `service-monitor.local`. If mDNS doesn't resolve, use the IP shown on the Device screen:

```bash
pio run -e t-display-s3-ota -t upload --upload-port 192.168.1.42
```

If you set `OTA_PASSWORD`, export the same password first:

```bash
export SERVICE_MONITOR_OTA_PASSWORD='your-ota-password'
```

While an update runs, service checks pause and the screen shows a progress bar. On macOS, allow incoming connections for Python if the firewall asks, because espota needs the device to connect back to your computer.

## Buttons and screens

| Button | Location | Action |
|---|---|---|
| BTN1 (GPIO 0, BOOT) | Left of the USB port | Switch screens: Services ↔ Device. Skips the splash screen. |
| BTN2 (GPIO 14) | Right of the USB port | Refresh all services now |

- **Services screen:** the clock, an overall summary, one card per service (status dot, name, latency, HTTP code or error), Wi-Fi signal strength (RSSI) and the time since the last update.
- **Device screen:** Wi-Fi state, SSID, IP address, RSSI, uptime, free and minimum free heap, total checks, last refresh time and NTP status.

Startup sequence: **Connecting to Wi-Fi… → Syncing time… → dashboard**. If Wi-Fi doesn't connect within 15 seconds, the splash shows **No Wi-Fi** and then the dashboard. Reconnection keeps retrying in the background.

## Board notes

- PlatformIO board: `lilygo-t-display-s3` (official definition in `platformio/espressif32`). Platform `7.1.3` uses Arduino-ESP32 core 2.0.17.
- The pins come from LILYGO's official [`pin_config.h`](https://github.com/Xinyuan-LilyGO/T-Display-S3/blob/main/examples/factory/pin_config.h):
  - LCD data D0–D7 = 39, 40, 41, 42, 45, 46, 47, 48
  - WR 8, RD 9, DC 7, CS 6, RST 5
  - Backlight 38
  - **POWER_ON 15**, which must be HIGH or the panel stays dark on battery
  - Buttons 0 and 14
- Panel settings, matching LILYGO's factory example:
  - ST7789 at a 16 MHz write clock
  - Colour inversion on
  - 35 px column offset (170 visible columns inside 240 columns of controller RAM)
- Orientation: landscape, 320 wide × 170 tall (`DISPLAY_ROTATION = 1` in `config.h`). If the picture is upside down for how your board sits, set it to `3`.
- The backlight is simply switched on with a GPIO. Its LED driver (AW9364) uses pulse-count dimming, so PWM is not used.
- Partitions: `default_16MB.csv`, which has two 6.5 MB app slots for OTA.

## Architecture

```
core 1  loop()            core 0  svc-monitor task
├─ WifiManager.loop()     └─ every CHECK_INTERVAL_MS or on refresh:
├─ OtaManager.loop()          for each service: HTTPClient GET → classify
├─ buttons (debounced)        → write result into a mutex-protected snapshot
├─ boot phase state machine
└─ DisplayManager (LVGL) ← copies the snapshot every 250 ms
```

- Network calls never run on the UI core, so a hanging service can't freeze the screen.
- LVGL uses a fixed 64 KB memory pool. Status text lives in fixed-size char buffers, so the UI loop allocates nothing on the heap.
- The loop task watchdog is enabled.
- Safety reboots happen in only two cases: 30 minutes without Wi-Fi, or internal heap below 16 KB for over a minute.

## Fonts

`src/fonts/mono_bold_*.c` hold JetBrains Mono Bold (SIL Open Font License, see `src/fonts/OFL.txt`), ASCII only, converted with `lv_font_conv`:

```bash
npx lv_font_conv@1.5.2 --font JetBrainsMono-Bold.ttf -r 0x20-0x7E --size 18 --bpp 4 --format lvgl --no-compress -o src/fonts/mono_bold_18.c
```

## Troubleshooting

**Board not detected on macOS**
- Use a data-capable USB-C cable and connect directly, not through a hub. Try the other orientation of the USB-C plug.
- Run `ls /dev/cu.*`. The board appears as `/dev/cu.usbmodem*` because it uses native USB, so no CP210x/CH340 driver is needed.
- If nothing appears, or the port appears and disappears, enter bootloader mode (below).
- The firmware's USB serial port only exists while the firmware runs. If a crash loop prevents that, bootloader mode always works.

**Blank display**
- The backlight is on but the screen is black or shows garbage: check that you're building this project's `t-display-s3` env. GPIO 15 (POWER_ON) must be driven HIGH, which `DisplayManager::begin()` does.
- The display stays fully dark on battery power: GPIO 15 again. Check the serial log for `[display] 320x170 ready`.
- Colours look inverted, or there is a 35 px shifted stripe: your panel batch may differ. Adjust `invert` or `offset_x` in `src/lgfx_tdisplay_s3.h`.
- Check the serial log for `[display] panel init failed`.

**Forcing bootloader mode**
1. Hold **BOOT** (the button next to USB, GPIO 0).
2. Press and release **RST** (the button on the side).
3. Release **BOOT**.
4. Upload. Afterwards, press **RST** to run the new firmware.

**Wi-Fi never connects**: check that the network is 2.4 GHz and the SSID and password are correct (both are case-sensitive). Watch the serial log for `[wifi]` lines.

**OTA upload fails**: use the IP address instead of `.local`, check that your firewall allows espota, check that the OTA password matches, and make sure the device shows "Connected" on its Device screen.
