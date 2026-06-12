# Architecture

This document describes the code organization, source tree layout, and initialization sequence of CoAP Loki.

## Source Tree

```
coap_loki/
├── src/                        Application source code
│   ├── main.c                  Entry point and initialization sequence
│   ├── main_loki.c / .h        Motor logic (speed, acceleration, direction)
│   ├── loki_coap_utils.c / .h  CoAP server, resource handlers
│   ├── main_ble_utils.c / .h   BLE GATT service and characteristics
│   ├── main_ot_utils.c / .h    OpenThread, SRP client, LocoNet UDP
│   ├── displays/               Display drivers
│   │   ├── main_display.h      Display API (shared header)
│   │   ├── 1306_display.c      SSD1306 OLED via LVGL
│   │   └── no_display.c        Stub for display-less builds
│   └── motors/                 Application-level motor drivers
│       ├── motor.h             Abstract motor API
│       ├── motorTB67H453.c     TB67H453 H-Bridge driver
│       ├── motorTB67driver.c   Generic TB67 implementation
│       └── motor_dummy.c       No-op motor for testing
├── drivers/motor/              Zephyr kernel-level device drivers
│   ├── tb67h453.c              TB67H453 Zephyr driver
│   ├── tb67h450.c              TB67H450 Zephyr driver
│   ├── dummy_motor.c           Dummy Zephyr driver
│   ├── Kconfig                 Motor driver configuration
│   └── CMakeLists.txt          Driver build rules
├── dts/bindings/motor/         Device tree bindings (YAML)
│   ├── toshiba,tb67h453.yaml
│   ├── toshiba,tb67h450.yaml
│   └── generic,dummy-motor.yaml
├── interface/                  Shared protocol definitions
│   ├── loki_server_client_interface.h   CoAP URI paths, direction enum
│   └── coap_server_client_interface.h   Legacy interface header
├── boards/                     Board-specific overlays and configs
├── CMakeLists.txt              Top-level build file
├── Kconfig                     Application-level Kconfig
├── prj.conf                    Default project configuration
├── loki_app.conf               BLE/NVS/SRP configuration
└── zephyr/module.yml           Zephyr module registration
```

## Module Responsibilities

| Module | File(s) | Responsibility |
|---|---|---|
| **Main** | `main.c` | Initialization, settings load/save, NVS management |
| **Motor Logic** | `main_loki.c` | Speed/acceleration control, timer-based ramping |
| **CoAP Server** | `loki_coap_utils.c` | CoAP resource handlers (`/speed`, `/acceleration`, `/direction`, `/stop`) |
| **BLE Service** | `main_ble_utils.c` | GATT service definition, characteristic callbacks, advertising |
| **OpenThread** | `main_ot_utils.c` | Thread state management, SRP registration, LocoNet UDP |
| **Display** | `displays/` | OLED status rendering (connection, speed, direction, name, IPv6) |
| **Motor Drivers** | `motors/`, `drivers/motor/` | Hardware abstraction for Toshiba H-Bridge and dummy motors |

## Initialization Sequence

The `main()` function in `main.c` follows this order:

1. **Motor initialization** — Verify the `motor0` device tree alias and check device readiness
2. **NVS mount** — Open the flash-backed non-volatile storage partition
3. **Version check** — Optionally clear settings if firmware version or build has changed
4. **Default values** — Set speed = 0, acceleration = 0, direction = stop, PWM base = 1000 Hz
5. **Display init** — Initialize LVGL and SSD1306 (if `CONFIG_LV_Z_MEM_POOL_NUMBER_BLOCKS` is set)
6. **Bluetooth enable** — Start the BLE stack
7. **Settings load** — Read persisted names and DCC address from NVS
8. **OpenThread check** — If a Thread dataset is commissioned:
   - Bring up the Thread interface
   - Initialize the SRP client and register services
   - Start the CoAP server and bind resource handlers
   - Register the LocoNet UDP listener
9. **BLE registration** — Register GATT services and characteristics
10. **BLE advertising** — Begin advertising with the configured device name

## Naming and Identity

Each device derives a default short name from its hardware EUI-64 identifier (e.g., `TREN0234`). Users can set a custom long name (up to 63 characters) and a BLE short name via BLE characteristics. These are persisted in NVS under the keys `loki/shortname`, `loki/longname`, and `loki/dcc`.

## Threading Model

The application uses Zephyr's cooperative threading:

- **Main thread** — initialization, settings, BLE callbacks
- **System workqueue** — OpenThread processing, CoAP handlers
- **Motor acceleration timer** — `k_timer` fires every second to apply acceleration deltas
- **LVGL display task** — periodic timer for display refresh (configurable interval)
