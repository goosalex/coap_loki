# CoAP Loki — IoT Locomotive Controller

**CoAP Loki** is a wireless locomotive controller built on [Zephyr RTOS](https://www.zephyrproject.org/) and the [Nordic nRF Connect SDK](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/index.html). It drives DC motors over PWM and exposes control through two wireless protocols simultaneously:

- **Bluetooth Low Energy (BLE)** — direct mobile/tablet control via GATT characteristics
- **OpenThread + CoAP** — mesh-network control with service discovery (SRP)

It also integrates with **LocoNet/DCC** train networks over UDP, making it suitable for both hobby train layouts and IoT motor-control applications.

**Firmware version:** 1.4.2

---

## Key Features

| Feature | Description |
|---|---|
| Dual-protocol control | BLE for local pairing, OpenThread/CoAP for mesh networking |
| PWM motor control | Speed (0–255), acceleration ramp, direction (stop/forward/reverse) |
| Modular motor drivers | Toshiba TB67H453, TB67H450, and a dummy driver for testing |
| OLED display | SSD1306 128×64 status display via LVGL |
| LocoNet / DCC | UDP listener for standard model-railway command protocol |
| Persistent settings | NVS-backed storage for names, DCC address, and configuration |
| Service discovery | SRP client advertises services on the Thread mesh |
| Multi-board support | nRF52840, nRF5340, nRF54L15, Pro Micro, and more |

---

## Supported Hardware

| Board | SoC | Notes |
|---|---|---|
| nRF52840 DK | nRF52840 | Full support |
| nRF52840 Dongle | nRF52840 | Full support |
| Pro Micro (nRF52840) | nRF52840 | Full support |
| nRF5340 DK | nRF5340 | Dual-core (cpuapp) |
| nRF21540 DK | nRF52840 | With FEM (Frontend Module) |
| nRF54L15 DK | nRF54L15 | Experimental |

---

## Quick Start

### Prerequisites

- [nRF Connect SDK](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/installation/install_ncs.html) (includes Zephyr)
- A supported Nordic development kit
- (Optional) SSD1306 OLED display, TB67H453/TB67H450 motor driver board

### Build

```bash
west build -b nrf52840dk_nrf52840 -- -DOVERLAY_CONFIG="loki_app.conf"
```

Add overlays as needed:

```bash
# With SSD1306 display
west build -b nrf52840dk_nrf52840 -- \
  -DOVERLAY_CONFIG="loki_app.conf;display-ssd1306.conf" \
  -DDTC_OVERLAY_FILE="display-ssd1306.overlay"

# With debug logging
west build -b nrf52840dk_nrf52840 -- \
  -DOVERLAY_CONFIG="loki_app.conf;overlay-logging.conf"
```

### Flash

```bash
west flash
```

### OpenThread Setup

Once running, configure the Thread network via the shell:

```
uart:~$ ot channel 11
uart:~$ ot panid 0xabcd
uart:~$ ot networkkey 00112233445566778899aabbccddeeff
uart:~$ ot ifconfig up
uart:~$ ot thread start
```

See [CLI.md](CLI.md) for more details on OpenThread CLI commands and QR code commissioning.

---

## Control API Summary

| Function | CoAP URI | BLE Characteristic | Methods |
|---|---|---|---|
| Speed | `/speed` | `fcbd0002` | GET, PUT, Notify |
| Acceleration | `/acceleration` | `fcbd0003` | GET, PUT |
| Direction | `/direction` | `fcbd0004` | GET, PUT |
| Stop | `/stop` | — | PUT |
| PWM Base | — | `fcbd0005` | GET, PUT |
| Long Name | — | `fcbd0006` | GET, PUT |
| DCC Address | — | `fcbd0007` | GET, PUT |
| OT Joiner Credential | — | `fcbd000a` | PUT |
| BLE Device Name | — | `fcbd000b` | GET, PUT |

Direction values: `0` = Stop, `1` = Forward, `2` = Reverse

---

## Detailed Documentation

For deeper technical details, see the **[OVERVIEW](OVERVIEW/)** directory:

| Document | Description |
|---|---|
| [Architecture](OVERVIEW/architecture.md) | Code organization, source tree, and initialization flow |
| [Motor Control](OVERVIEW/motor-control.md) | PWM motor subsystem, driver layers, and device tree bindings |
| [Communication](OVERVIEW/communication.md) | BLE GATT service, CoAP resources, OpenThread, and LocoNet/DCC |
| [Display](OVERVIEW/display.md) | SSD1306 OLED display layout and LVGL integration |
| [Configuration](OVERVIEW/configuration.md) | Build system, Kconfig options, overlay files, and presets |
| [Hardware](OVERVIEW/hardware.md) | Board-specific setup, pin mappings, and overlays |

---

## Project Status

See [PROGRESS.md](PROGRESS.md) for the current feature matrix and known issues.

---

## License

Based on Nordic Semiconductor samples. See individual file headers for license details (LicenseRef-Nordic-5-Clause).
