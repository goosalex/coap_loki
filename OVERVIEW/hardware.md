# Hardware Support

This document describes the supported boards, pin mappings, and board-specific configuration.

## Supported Boards

| Board | SoC | Core | Thread | BLE | Status |
|---|---|---|---|---|---|
| nRF52840 DK | nRF52840 | Cortex-M4F | ✅ | ✅ | Full support |
| nRF52840 Dongle | nRF52840 | Cortex-M4F | ✅ | ✅ | Full support |
| Pro Micro (nRF52840) | nRF52840 | Cortex-M4F | ✅ | ✅ | Full support |
| nRF5340 DK | nRF5340 | Cortex-M33 + M4 | ✅ | ✅ | Full support |
| nRF21540 DK | nRF52840 | Cortex-M4F | ✅ | ✅ | Full (with FEM) |
| nRF54L15 DK | nRF54L15 | — | ✅ | ✅ | Experimental |

All boards use Nordic Semiconductor SoCs with integrated 802.15.4 (Thread) and BLE radios.

## Board-Specific Files

Board configurations are in the `boards/` directory:

### nRF52840 DK (`nrf52840dk_nrf52840`)

| File | Purpose |
|---|---|
| `nrf52840dk_nrf52840.overlay` | Motor PWM pin mapping (pins 3, 4, 5) |

### nRF52840 Dongle (`nrf52840dongle_nrf52840`)

| File | Purpose |
|---|---|
| `nrf52840dongle_nrf52840.overlay` | Motor PWM pin mapping (pins 13, 14, 15) |
| `nrf52840dongle_nrf52840.conf` | Board-specific Kconfig |
| `nrf52840dongle_nrf52840_pm_static.yml` | Partition manager layout |

### nRF5340 DK (`nrf5340dk_nrf5340_cpuapp`)

| File | Purpose |
|---|---|
| `nrf5340dk_nrf5340_cpuapp.conf` | Application core configuration |
| `nrf5340dk_nrf5340_cpuapp_ns.conf` | Non-secure variant configuration |
| `nrf5340dk_nrf5340ns.overlay` | Non-secure device tree overlay |

### nRF21540 DK (`nrf21540dk_nrf52840`)

| File | Purpose |
|---|---|
| `nrf21540dk_nrf52840.overlay` | Motor pin mapping with FEM support |

### Pro Micro (`promicro_nrf52840`)

| File | Purpose |
|---|---|
| `promicro_nrf52840.conf` | Board-specific configuration |
| `promicro_nrf52840.overlay` | Motor and peripheral pin mapping |

### nRF54L15 DK (`nrf54L15dk_nrf54L15`)

| File | Purpose |
|---|---|
| `nrf54L15dk_nrf54L15.overlay` | Motor pin mapping |
| `nrf54-overlay.conf` | nRF54-specific Kconfig |

## Motor Wiring

The motor driver connects to the SoC via three signals:

| Signal | Function | Direction |
|---|---|---|
| IN1 | PWM channel 1 (speed/direction) | SoC → Driver |
| IN2 | PWM channel 2 (speed/direction) | SoC → Driver |
| SLEEP | Enable/disable driver | SoC → Driver |

### Pin Assignments

| Board | IN1 Pin | IN2 Pin | SLEEP Pin |
|---|---|---|---|
| nRF52840 DK | P0.03 | P0.05 | P0.04 |
| nRF52840 Dongle | P0.13 | P0.15 | P0.14 |
| Others | See board overlay files | — | — |

## Display Wiring (Optional)

The SSD1306 OLED connects via I2C:

| Signal | Function |
|---|---|
| SDA | I2C data |
| SCL | I2C clock |

I2C pin assignments vary by board and are defined in the display overlay files (`display-ssd1306.overlay`, `display-ssd1306-promicro.overlay`).

## Adding a New Board

To add support for a new Nordic board:

1. Create a device tree overlay in `boards/` (e.g., `<board_name>.overlay`) defining the `motor0` alias and PWM pin mapping
2. Optionally create a `.conf` file for board-specific Kconfig settings
3. Add the board to `sample.yaml` under `platform_allow` if it should be included in CI
4. Build with: `west build -b <board_name> -- -DOVERLAY_CONFIG="loki_app.conf"`
