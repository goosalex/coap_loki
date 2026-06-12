# Configuration and Build System

This document describes the CMake build system, Kconfig configuration, and overlay files.

## Build System

CoAP Loki uses CMake via Zephyr's `west` meta-tool. The top-level `CMakeLists.txt` collects sources from `src/`, `src/displays/`, `src/motors/`, and `drivers/motor/` using `FILE(GLOB ...)`.

### Building

```bash
# Basic build for nRF52840 DK
west build -b nrf52840dk_nrf52840 -- -DOVERLAY_CONFIG="loki_app.conf"

# Build for nRF52840 Dongle with debug logging
west build -b nrf52840dongle_nrf52840 -- \
  -DOVERLAY_CONFIG="loki_app.conf;overlay-logging.conf;overlay-debug.conf"
```

### CMake Presets

`CMakePresets.json` provides predefined build configurations:

- **`build_dongle`** — nRF52840 Dongle with debug and logging overlays enabled

### Firmware Versioning

The build system generates `app_version.h` from `app_version.h.in` at configure time:

- `APP_VERSION_MAJOR` / `MINOR` / `PATCH` — set manually in CMakeLists.txt
- `APP_BUILD_NUMBER` — auto-generated UTC timestamp (`YYYYMMDDHHMMSS`)

### Sysbuild

`sysbuild.cmake` conditionally applies a static partition manager configuration for the nRF52840 Dongle (`nrf52840dongle_nrf52840_pm_static.yml`).

## Kconfig Options

### Application-Level (`Kconfig`)

| Option | Type | Default | Description |
|---|---|---|---|
| `LOKI_MOTOR1PIN` | int | Board-dependent | Motor PWM pin 1 |
| `LOKI_MOTOR2PIN` | int | Board-dependent | Motor PWM pin 2 |
| `LOKI_MOTOR0PIN` | int | Board-dependent | Motor sleep/enable pin |
| `LVGL_DISPLAY_UPDATE_PERIOD_MS` | int | 0 | Display refresh interval |
| `CLEAR_SETTINGS_NEW_BUILD` | bool | n | Clear NVS settings on new build |
| `CLEAR_SETTINGS_NEW_VERSION` | bool | n | Clear NVS settings on version change |

### Motor Drivers (`drivers/motor/Kconfig`)

| Option | Type | Default | Description |
|---|---|---|---|
| `DC_MOTOR` | bool | y | Enable motor driver subsystem |
| `MOTOR_INIT_PRIORITY` | int | 90 | Device init priority |
| `MOTOR_LOG_LEVEL` | int | 3 | Log level (0=off, 4=debug) |
| `MOTOR_TB67H453` | bool | n | Enable TB67H453 driver |
| `MOTOR_TB67H450` | bool | n | Enable TB67H450 driver |
| `MOTOR_DUMMY` | bool | auto | Dummy driver (fallback) |

## Configuration Files

### Core Configs

| File | Purpose |
|---|---|
| `prj.conf` | Default project config: OpenThread, CoAP, PWM, shell, networking |
| `loki_app.conf` | BLE, NVS, settings, SRP, joiner, GPIO, pin control |
| `prj-ncs3.conf` | Variant for nRF Connect SDK v3.x |

### Overlay Configs

| File | Purpose |
|---|---|
| `overlay-logging.conf` | Enable RTT logging backend |
| `overlay-debug.conf` | Debug build optimizations |
| `overlay-logging-usb.conf` | USB-based logging output |
| `overay-uart-logging.conf` | UART logging (note: filename has typo) |

### Display Configs

| File | Purpose |
|---|---|
| `display-ssd1306.conf` | Enable SSD1306 + LVGL |
| `display-ssd1306-ncs3.conf` | SSD1306 for ncs3 |
| `display-ssd1306.overlay` | SSD1306 device tree overlay |
| `display-ssd1306-promicro.overlay` | Pro Micro-specific display overlay |
| `ssd1306_default.overlay` | Default SSD1306 settings |

### Device Tree Overlays

| File | Purpose |
|---|---|
| `flash_storage.overlay` | NVS flash partition definition |
| `flash_storage_nrf54.overlay` | NVS partition for nRF54 |
| `motor-dummy.overlay` | Dummy motor device tree node |

## Zephyr Module

The `zephyr/module.yml` file registers CoAP Loki as a Zephyr module, enabling its device tree bindings and drivers to be discovered by the build system.

## Persistent Storage (NVS)

Settings are stored in the flash-backed NVS partition:

| Key | Type | Description |
|---|---|---|
| `loki/shortname` | string | BLE short name (max 8 chars) |
| `loki/longname` | string | Full device name (max 63 chars) |
| `loki/dcc` | uint16 | DCC locomotive address |
| `loki/init` | bool | First-initialization flag |

The firmware tracks its version and build number in NVS. When `CLEAR_SETTINGS_NEW_BUILD` or `CLEAR_SETTINGS_NEW_VERSION` is enabled, settings are automatically cleared on firmware updates.
