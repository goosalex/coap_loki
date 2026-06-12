# Communication Protocols

CoAP Loki supports four communication interfaces for locomotive control.

## 1. Bluetooth Low Energy (BLE)

### GATT Service

The Loki BLE service is registered under UUID `fcbd0001-5e25-4387-99b7-53a5495a0c35`.

### Characteristics

| UUID | Name | Permissions | Description |
|---|---|---|---|
| `fcbd0002` | Speed | Read, Write, Notify | Motor speed (0–255) |
| `fcbd0003` | Acceleration | Read, Write | Acceleration rate (-128 to +127) |
| `fcbd0004` | Direction | Read, Write | Direction (0=Stop, 1=Forward, 2=Reverse) |
| `fcbd0005` | PWM Base | Read, Write | PWM base frequency |
| `fcbd0006` | Long Name | Read, Write | Full device name (up to 63 chars) |
| `fcbd0007` | DCC Address | Read, Write | DCC locomotive address (uint16) |
| `fcbd000a` | OT Joiner | Write | OpenThread joiner credential |
| `fcbd000b` | BLE Name | Read, Write | Short BLE advertised name |

### Advertising

The device advertises with a short name derived from its EUI-64 hardware identifier (e.g., `TREN0234`). The name can be changed via the BLE Name characteristic and is persisted in NVS.

### Implementation

BLE logic is in `src/main_ble_utils.c` (~500 lines). Characteristics use Zephyr's `BT_GATT_SERVICE_DEFINE` macro with read/write callbacks that delegate to the motor control functions in `main_loki.c`.

---

## 2. CoAP (Constrained Application Protocol)

### Resources

| URI Path | Methods | Description |
|---|---|---|
| `/speed` | GET, PUT | Read or set motor speed (0–255) |
| `/acceleration` | GET, PUT | Read or set acceleration |
| `/direction` | GET, PUT | Read or set direction |
| `/stop` | PUT | Immediately stop the motor |
| `/name` | PUT | Change device name |

### Details

- **Port**: 5683 (standard CoAP)
- **Content format**: Plain text
- **Transport**: UDP over OpenThread mesh
- **API**: Native OpenThread CoAP API (`otCoapNewMessage`, `otCoapSendResponse`, etc.)

### Implementation

CoAP handlers are in `src/loki_coap_utils.c`. Each resource is registered with the OpenThread CoAP stack. Handlers parse incoming payloads (plain text via `sscanf`) and call the corresponding `main_loki` functions.

URI paths are defined in `interface/loki_server_client_interface.h`:

```c
#define SPEED_URI_PATH      "speed"
#define ACC_URI_PATH        "acceleration"
#define DIRECTION_URI_PATH  "direction"
#define STOP_URI_PATH       "stop"
#define NAME_URI_PATH       "name"
```

---

## 3. OpenThread (Mesh Network)

### Overview

OpenThread provides the IPv6 mesh networking layer. When a Thread dataset is commissioned (via CLI or BLE joiner credential), the device:

1. Brings up the Thread interface
2. Registers services via the SRP (Service Registration Protocol) client
3. Starts the CoAP server
4. Binds the LocoNet UDP listener

### SRP Service Registration

The device registers four services on the Thread mesh for discovery:

| Service Name | Service Type | Purpose |
|---|---|---|
| (short name) | `_ble._loki_coap._udp` | BLE short name advertisement |
| (full name) | `_name._loki_coap._udp` | Full device name |
| (DCC addr) | `_dcc._loki_dcc._udp` | DCC locomotive address |
| (LocoNet) | `_loconet._loki_loconet._udp` | LocoNet UDP endpoint |

### Thread Configuration

Default Thread parameters can be set via the OpenThread CLI shell:

```
ot channel 11
ot panid 0xabcd
ot networkkey 00112233445566778899aabbccddeeff
ot ifconfig up
ot thread start
```

### Implementation

OpenThread utilities are in `src/main_ot_utils.c`. This handles Thread state callbacks, SRP client setup, and IPv6 address management.

---

## 4. LocoNet / DCC (Digital Command Control)

### Overview

LocoNet is a standard model-railway communication bus. CoAP Loki receives LocoNet messages over UDP, enabling integration with DCC-based train control systems.

### Protocol Details

- **UDP Port**: 1234
- **Message format**: LocoNet OPC_WR_SL_DATA (`0xEF`)
- **Parsed fields**:
  - `SPD` — Locomotive speed
  - `DIRF` — Direction and function bits
  - `STAT1` — Status byte
  - `ADR` / `ADR2` — 14-bit DCC locomotive address

### How It Works

1. The device registers its DCC address via BLE or at startup from NVS
2. A UDP socket listens on port 1234 on the Thread mesh
3. Incoming LocoNet slot data messages are parsed
4. If the DCC address matches, speed and direction commands are applied to the motor

### Implementation

The LocoNet UDP handler (`on_udp_loconet_receive`) is in `src/main_ot_utils.c`. It extracts the DCC address and speed/direction from the LocoNet frame and calls the motor control functions.
