# Motor Control Subsystem

This document describes the motor control architecture, driver layers, and device tree integration.

## Overview

CoAP Loki controls DC motors via PWM signals through a multi-layered driver architecture. The system supports multiple motor driver ICs and provides smooth acceleration/deceleration through timer-based speed ramping.

## Driver Architecture

The motor subsystem has three layers:

### Layer 1: Abstract API (`src/motors/motor.h`)

Defines a generic motor driver interface:

```
struct motor_driver_api {
    motor_set_speed_t set_speed;         // Set speed 0–100%
    motor_set_direction_t set_direction; // Set direction pattern
};
```

All motor drivers implement this API, allowing the application logic to be hardware-agnostic.

### Layer 2: Application Logic (`src/main_loki.c`)

Manages the high-level motor behavior:

- **Speed**: 0–255 integer steps mapped to PWM duty cycle
- **Acceleration**: Signed value (-128 to +127), applied once per second via `k_timer`
- **Direction**: Three states — `0` (Stop), `1` (Forward), `2` (Reverse)
- **PWM base frequency**: Configurable, default 1000 Hz

When acceleration is non-zero, a periodic timer fires every second and adjusts the speed by the acceleration delta, clamping to the 0–255 range.

### Layer 3: Zephyr Device Drivers (`drivers/motor/`)

Kernel-level drivers registered through the Zephyr device model using `DT_INST_FOREACH_STATUS_OKAY()`.

## Supported Motor Drivers

### Toshiba TB67H453

- **Compatible**: `toshiba,tb67h453`
- **Type**: Dual-PWM H-Bridge with sleep GPIO
- **Pins**: IN1 (PWM), IN2 (PWM), SLEEP (GPIO)
- **Logic**:
  - IN1=Low, IN2=Low → Stop
  - IN1=High, IN2=Low → Forward
  - IN1=Low, IN2=High → Reverse
  - IN1=High, IN2=High → Brake

### Toshiba TB67H450

- **Compatible**: `toshiba,tb67h450`
- **Type**: Pure PWM H-Bridge (no sleep pin)
- **Pins**: IN1 (PWM), IN2 (PWM)
- **Logic**: Same as TB67H453 without the sleep control

### Dummy Motor

- **Compatible**: `generic,dummy-motor`
- **Type**: No-op driver for testing and development
- **Usage**: Automatically selected when no real motor driver is configured

## Device Tree Integration

Motor drivers are instantiated via device tree. Each board overlay defines a `motor0` alias pointing to the motor node:

```dts
/ {
    aliases {
        motor0 = &motor0;
    };
};

&motor0 {
    compatible = "toshiba,tb67h453";
    in1-pwms = <&pwm0 0 PWM_MSEC(1) PWM_POLARITY_NORMAL>;
    in2-pwms = <&pwm0 1 PWM_MSEC(1) PWM_POLARITY_NORMAL>;
    sleep-gpios = <&gpio0 14 GPIO_ACTIVE_HIGH>;
};
```

## Device Tree Bindings

Binding YAML files are located in `dts/bindings/motor/`:

- `toshiba,tb67h453.yaml` — Requires `in1-pwms`, `in2-pwms`, `sleep-gpios`
- `toshiba,tb67h450.yaml` — Requires `in1-pwms`, `in2-pwms`
- `generic,dummy-motor.yaml` — No required properties

## Kconfig Options

| Option | Default | Description |
|---|---|---|
| `CONFIG_DC_MOTOR` | y | Enable motor driver subsystem |
| `CONFIG_MOTOR_INIT_PRIORITY` | 90 | Driver initialization priority |
| `CONFIG_MOTOR_LOG_LEVEL` | 3 | Logging verbosity (0–4) |
| `CONFIG_MOTOR_TB67H453` | n | Enable TB67H453 driver (requires PWM) |
| `CONFIG_MOTOR_TB67H450` | n | Enable TB67H450 driver (requires PWM) |
| `CONFIG_MOTOR_DUMMY` | auto | Dummy driver (auto-enabled if no others selected) |

## Pin Mapping by Board

| Board | Motor Pin 1 | Motor Pin 2 | Sleep Pin |
|---|---|---|---|
| nRF52840 Dongle | P0.13 | P0.15 | P0.14 |
| nRF52840 DK | P0.03 | P0.05 | P0.04 |
| Pro Micro | varies | varies | varies |

Pin assignments are configured through board-specific device tree overlays in the `boards/` directory.
