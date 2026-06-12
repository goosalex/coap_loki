# Display Subsystem

This document describes the SSD1306 OLED display integration and LVGL rendering.

## Overview

CoAP Loki optionally supports an SSD1306 128×64 OLED display for real-time status visualization. The display is driven by [LVGL](https://lvgl.io/) (Light and Versatile Graphics Library) through Zephyr's display subsystem.

When no display hardware is present, a stub driver (`no_display.c`) is compiled instead, making display support fully optional.

## Display Layout

The 128×64 pixel display is organized into four text lines:

```
┌────────────────────────────────┐
│ BT +OT +SRP +CoAP             │  Line 0: Connection status
│  > 128                         │  Line 1: Direction + Speed
│ TREN0234                       │  Line 2: Device short name
│ fd12:3456::1 (scrolling)       │  Line 3: IPv6 address
└────────────────────────────────┘
```

| Line | Y Position | Content |
|---|---|---|
| 0 | 8px | Connection status indicators (BT, OT, SRP, CoAP) |
| 1 | 16px | Direction symbol (`>` forward, `<` reverse) and speed value |
| 2 | 24px | Device short name (from EUI-64 or user-set) |
| 3 | 32px | IPv6 address (circular scrolling for long addresses) |

## API

The display API is defined in `src/displays/main_display.h`:

| Function | Description |
|---|---|
| `display_initDisplay()` | Initialize LVGL, acquire display device, create labels |
| `display_updateConnectionStatus(status)` | Update BT connection indicator |
| `display_updateBTConnectionStatus(status)` | Update Bluetooth-specific status |
| `display_updateOTConnectionStatus(status)` | Update OpenThread status (`+S` for SRP, `+CoAP`) |
| `display_updateDirectionAndSpeed(dir, speed)` | Update direction symbol and speed value |
| `display_updateName(name)` | Update the displayed device name |
| `display_updateIPv6Address(ipv6)` | Update IPv6 address (with circular scrolling) |

## Implementation

### With Display (`src/displays/1306_display.c`)

- Uses Zephyr's `device_get_binding()` to acquire the SSD1306
- Creates LVGL label objects for each display line
- Uses a mutex (`lvgl_mutex`) for thread-safe LVGL operations
- IPv6 address rendering uses circular scrolling to handle long addresses
- Display refresh is driven by a periodic timer (`CONFIG_LVGL_DISPLAY_UPDATE_PERIOD_MS`)

### Without Display (`src/displays/no_display.c`)

All API functions are defined as no-ops, allowing the firmware to compile and run without display hardware.

## Enabling the Display

To enable SSD1306 display support, add the display configuration and overlay when building:

```bash
west build -b <board> -- \
  -DOVERLAY_CONFIG="loki_app.conf;display-ssd1306.conf" \
  -DDTC_OVERLAY_FILE="display-ssd1306.overlay"
```

### Configuration Files

| File | Purpose |
|---|---|
| `display-ssd1306.conf` | Enables LVGL, display subsystem, I2C, SSD1306 driver |
| `display-ssd1306-ncs3.conf` | Variant for nRF Connect SDK v3 |
| `display-ssd1306.overlay` | Device tree overlay for SSD1306 on I2C bus |
| `display-ssd1306-promicro.overlay` | Pro Micro-specific display overlay |

### Kconfig

| Option | Default | Description |
|---|---|---|
| `CONFIG_LVGL_DISPLAY_UPDATE_PERIOD_MS` | 0 | Display refresh interval in milliseconds (0 = disabled) |
