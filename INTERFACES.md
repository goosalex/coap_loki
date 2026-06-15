# Loki Locomotive — Control Interfaces

This document describes the two external control interfaces exposed by the
locomotive firmware:

1. **BLE GATT** — offered while the loco is *unprovisioned*. Used to drive the
   loco directly, to rename it, and to provision it onto a Thread network.
2. **CoAP over OpenThread** — offered once the loco has joined a Thread network.
   Used for normal operation over the mesh.

It also documents the **SRP / DNS-SD service registrations** used for discovery,
and proposes cleaner, machine-readable descriptor formats for both interfaces
(see [Proposed formats](#proposed-formats)).

> Source of truth: this document is derived from the firmware. Key files are
> [main_ble_utils.c](src/main_ble_utils.c), [loki_coap_utils.c](src/loki_coap_utils.c),
> [main_ot_utils.c](src/main_ot_utils.c), [main_loki.c](src/main_loki.c),
> [main.c](src/main.c), and the shared interface headers in
> [interface/](interface/).

---

## Operating modes & lifecycle

```
power on
   │
   ├─ load settings from NVM (short name, long name, DCC address)
   │
   ├─ otDatasetIsCommissioned() ?
   │        │
   │        ├─ yes ──► enable Thread ──► init SRP client ──► start CoAP server
   │        │          register SRP services (discovery)
   │        │          if DCC set: bind Loconet UDP listener on :1234
   │        │
   │        └─ no  ──► (Thread stays down)
   │
   └─ enable BLE  ──►  advertise Loki GATT service
                          │
                          ├─ Thread attached (CHILD/ROUTER/LEADER)
                          │     └─ schedule stop in N min (default 5)
                          │           └─ on timer ──► stop advertising
                          │
                          ├─ Thread detached     ──► resume advertising
                          ├─ /ble-recovery (CoAP) ──► reopen window (fresh N min)
                          └─ SRP registration fails (if gated on)
                                                 ──► reopen window (fresh N min)
```

The design is *BLE while unprovisioned, CoAP once joined to Thread*, with a
grace window so a freshly-attached loco stays briefly reachable over GATT.

> **BLE lifecycle.** Implemented in [main_ble_utils.c](src/main_ble_utils.c)
> via an `atomic_t ble_should_advertise` flag + `K_WORK_DELAYABLE` stop timer.
> Three knobs:
>
> - `CONFIG_LOKI_BLE_OFF_AFTER_ATTACH_MINUTES` (default `5`, range `0..1440`):
>   minutes after a successful Thread attach before advertising stops. `0`
>   disables the auto-stop (BLE stays on).
> - `CONFIG_LOKI_BLE_RECOVERY_ON_SRP_FAIL` (default `y`): reopen the window
>   automatically when SRP host-name / auto-host-address / service-allocation
>   calls fail.
> - The CoAP `PUT /ble-recovery` endpoint is **not** gated by the Kconfig — a
>   human asking for recovery always reopens the window.
>
> When the timer fires while a BLE client is connected, only new advertising
> is stopped; the existing session is not kicked. Re-advertising resumes on
> the next Thread detach, recovery request, or (gated) SRP failure.

### Identity & naming

| Concept | Default | Max length | Where |
|---|---|---|---|
| Short name | `TREN` + tail of EUI64, e.g. `TREN34FD` | 8 chars | [main_ble_utils.h:6](src/main_ble_utils.h#L6) |
| Long (full) name | copy of short name | 63 chars | [main_ble_utils.h:5](src/main_ble_utils.h#L5) |
| DCC address | `0` (unset) | `uint16` | [main_loki.h:19](src/main_loki.h#L19) |
| EUI64 | factory-assigned | 8 bytes / 16 hex chars | [main_ot_utils.c:323](src/main_ot_utils.c#L323) |

Short name, long name, and DCC address are persisted to NVM under the `loki/`
settings subtree ([main.c:252-267](src/main.c#L252-L267)).

---

## BLE GATT interface (unprovisioned)

### Advertising

Defined in [main_ble_utils.c:361-404](src/main_ble_utils.c#L361-L404).

The 31-byte advertising payload contains:

| Field | Type | Value |
|---|---|---|
| Flags | `0x01` | `GENERAL` + `NO_BREDR` |
| Complete 128-bit UUID list | `0x07` | `fcbd0001-5e25-4387-99b7-53a5495a0c35` |
| Shortened name | `0x08` | short name (up to 8 bytes), e.g. `TREN34FD` |

The scan response carries the complete device name (the long name).

> Note: the 128-bit service UUID alone consumes 18 of the 31 bytes, which is why
> the short name is capped at 8 characters and the long name is delivered via the
> scan response.

### Standard service: Device Information (`0x180A`)

In addition to the custom Loki Control service below, the firmware exposes the
standard **Device Information Service** so generic BLE clients render a readable
identity card next to the opaque `fcbd0001-…` service. Wired up via
`CONFIG_BT_DIS*` in [loki_app.conf](loki_app.conf) and a runtime override in
[main.c](src/main.c).

| Field | Value | Source |
|---|---|---|
| Manufacturer Name | `Loki` | `CONFIG_BT_DIS_MANUF_NAME_STR` |
| Model Number | `Locomotive Control` | `CONFIG_BT_DIS_MODEL_NUMBER_STR` |
| Firmware Revision | `<MAJOR.MINOR.PATCH>` of the running build | `APP_VERSION_*` via `settings_runtime_set("bt/dis/fw", …)` at boot |
| Software Revision | same as Firmware Revision | `APP_VERSION_*` via `settings_runtime_set("bt/dis/sw", …)` at boot |

Implementation note: this NCS has no `CONFIG_BT_DIS_STR_USER` / `bt_dis_set_str()`
— `CONFIG_BT_DIS_SETTINGS=y` is the supported way to mutate DIS strings at
runtime. The override happens *after* `settings_load()`, so it also wins over
any stale value persisted in NVS from an earlier build; it is intentionally not
persisted (no `settings_save_one`) so the next boot re-derives from
`APP_VERSION_*` again.

### Service: Loki Control

**Primary service UUID:** `fcbd0001-5e25-4387-99b7-53a5495a0c35`

All custom characteristics share the base `fcbd00XX-5e25-4387-99b7-53a5495a0c35`.
Declaration order is in [main_ble_utils.c:304-353](src/main_ble_utils.c#L304-L353).

| Characteristic | 16/128-bit UUID (short) | Props | Payload | Semantics |
|---|---|---|---|---|
| Acceleration | `fcbd0003` | R / W | `int8` | Write: rate per second applied by a 1 Hz ramp timer (negative = brake). Read: current `accel_order`. |
| Speed | `fcbd0002` | R / W / Notify | `uint8` (0–255) | Write: set speed immediately. Read: current speed. Notify: pushed when motion changes (if CCC enabled). |
| PWM base | `fcbd0004` | R / W | `uint16` LE (Hz) | Motor PWM carrier frequency. Write recomputes period/pulse and re-applies current speed. |
| Direction | `fcbd0005` | R / W | `uint8` (bit pattern) | Write: change direction; if moving, treated as a timed emergency brake instead of an instant flip. |
| Long name | `fcbd0006` | R / W | UTF-8, ≤63 | Read: current device long name. Write: rename (updates GATT name + scan response). |
| DCC address | `fcbd0007` | R / W | `uint16` LE | Virtual DCC / Loconet address. *Write is not persisted or re-registered (see caveats).* |
| OT joiner credential | `fcbd000a` | R / W | see below | **Provisioning.** Read: EUI64 hex string. Write: Thread joining passphrase → starts joiner. |
| BLE short name | `fcbd000b` | R / W | UTF-8, ≤8 | Read: advertised short name. Write: rename short name (persisted + SRP re-registered). |

#### Speed notifications (CCC)

The Speed characteristic has a Client Characteristic Configuration Descriptor.
When a client enables notifications, `speed_notify_enabled` is set
([main_ble_utils.c:189-198](src/main_ble_utils.c#L189-L198)) and the firmware
calls `bt_notify_speed()` on motion changes.

> **Caveat — notify target attribute.** `bt_notify_speed()`
> ([main_ble_utils.c:503-508](src/main_ble_utils.c#L503-L508)) notifies on
> `loki_service.attrs[1]`. Given the current declaration order, `attrs[1]` is the
> Acceleration characteristic *declaration*, not the Speed value attribute
> (which is `attrs[4]`). Verify this index before relying on notifications.

### Provisioning via the credential characteristic (`fcbd000a`)

This is the bridge from BLE into the Thread network.

- **Read** returns the factory-assigned **EUI64** as a 16-character uppercase hex
  string ([main_ble_utils.c:276-286](src/main_ble_utils.c#L276-L286),
  [main_ot_utils.c:323-337](src/main_ot_utils.c#L323-L337)).
- **Write** is interpreted as the OpenThread **joiner passphrase (PSKd)** and
  triggers `start_thread_joiner()`
  ([main_ot_utils.c:120-173](src/main_ot_utils.c#L120-L173)):
  - length `< 6` → rejected (too short);
  - length `6–32` → detach, reset dataset, bring IPv6 up, `otJoinerStart(...)`;
  - length `> 32` → rejected (a full Active Dataset is *not yet* implemented).

On a successful join, `HandleJoinerCallback`
([main_ot_utils.c:56-66](src/main_ot_utils.c#L56-L66)) enables Thread and the SRP
client, after which the CoAP interface below becomes reachable.

The commissioner needs the EUI64 + passphrase pair. The QR payload convention
(from [CLI.md](CLI.md)) is:

```
v=1&&eui=<eui64>&&cc=<passphrase>
```

### GATT caveats / known issues

- **DCC write is volatile:** `write_dcc` ([main_ble_utils.c:261-274](src/main_ble_utils.c#L261-L274))
  sets `dcc_address` in RAM only — it is not saved to NVM nor re-registered with
  SRP, unlike the short-name path.
- **Long-name write does not re-register SRP:** the BLE long-name write calls
  `updateBleLongName()` (BLE only). The SRP-aware `modify_full_name()` is only
  reached through the (currently unregistered) CoAP `name` resource.
- **Read of Direction** uses `sizeof(speed_value)` as the length
  ([main_ble_utils.c:155-160](src/main_ble_utils.c#L155-L160)); harmless today
  because both are 1 byte, but fragile if either type changes.
- **`write_name` bounds:** writes `new_name[len] = '\0'` after a `len > sizeof`
  check, allowing a one-byte overflow when `len == 63`
  ([main_ble_utils.c:207-227](src/main_ble_utils.c#L207-L227)).

---

## CoAP interface (provisioned / on Thread)

### Transport

| Property | Value |
|---|---|
| Protocol | CoAP (OpenThread native CoAP API) |
| Underlying transport | UDP / IPv6 over Thread (6LoWPAN) |
| Port | `5683` (`COAP_PORT`, [interface/loki_server_client_interface.h](interface/loki_server_client_interface.h)) |
| Message type | **Non-confirmable only** — handlers reject anything else |
| Addressing | the loco's Thread IPv6 address (discoverable via SRP/DNS-SD, below) |

Server start and resource registration: `loki_coap_init()`
([loki_coap_utils.c:515-568](src/loki_coap_utils.c#L515-L568)).

### Resources

| URI path | Methods | Request payload | Response | Handler |
|---|---|---|---|---|
| `/speed` | GET, PUT | PUT: ASCII decimal string (≤4 bytes), e.g. `"120"` | GET: current speed as ASCII decimal (`text/plain`) | [loki_coap_utils.c:195-308](src/loki_coap_utils.c#L195-L308) |
| `/acceleration` | GET, PUT | PUT: 1 byte `int8` | GET: 1 byte (intended accel — see caveat) | [loki_coap_utils.c:335-420](src/loki_coap_utils.c#L335-L420) |
| `/direction` | PUT | 1 byte | — | [loki_coap_utils.c:422-451](src/loki_coap_utils.c#L422-L451) |
| `/stop` | (any) | ignored | — | [loki_coap_utils.c:453-463](src/loki_coap_utils.c#L453-L463) |
| `/name` | PUT | UTF-8 bytes | — | **defined but NOT registered — see caveat** |
| `/ble-recovery` | PUT | ignored | — | reopens BLE advertising window for `CONFIG_LOKI_BLE_OFF_AFTER_ATTACH_MINUTES` (default 5 min). Not gated by `CONFIG_LOKI_BLE_RECOVERY_ON_SRP_FAIL` — always works. |

Direction command values are enumerated in
[interface/loki_server_client_interface.h](interface/loki_server_client_interface.h):
`'0'` stop, `'1'` forward, `'2'` reverse.

### Example (using `coap-client` / libcoap)

```sh
# set speed to 120 (ASCII payload, non-confirmable)
coap-client -N -m put -e "120" "coap://[<loco-ipv6>]:5683/speed"

# read current speed
coap-client -N -m get "coap://[<loco-ipv6>]:5683/speed"

# emergency stop
coap-client -N -m put "coap://[<loco-ipv6>]:5683/stop"
```

### CoAP caveats / known issues

- **`/name` is unreachable.** `name_resource` and `name_request_handler` exist,
  but `loki_coap_init()` neither assigns `srv_context.on_name_request` nor calls
  `otCoapAddResource()` for it (only speed, acceleration, direction, stop are
  added — [loki_coap_utils.c:549-552](src/loki_coap_utils.c#L549-L552)). Renaming
  over CoAP is therefore currently a no-op path.
- **`/acceleration` GET returns the wrong value.** The handler sets
  `payload = speed_value;` (assigning an integer to a `const void *`) and appends
  it ([loki_coap_utils.c:379-382](src/loki_coap_utils.c#L379-L382)), so a GET on
  acceleration effectively returns speed, not the acceleration order.
- **Content-format negotiation is stubbed.** The Speed GET path hard-codes the
  text/plain branch (`0 == OT_COAP_OPTION_CONTENT_FORMAT_TEXT_PLAIN`,
  [loki_coap_utils.c:255](src/loki_coap_utils.c#L255)); `getContentFormat()`
  exists but is bypassed, and it also dereferences an uninitialized iterator.
- **No CoAP resource discovery.** There is no `/.well-known/core` resource, so a
  client cannot enumerate resources over CoAP (see proposal below).
- **`name_request_handler` leaks** the buffer it `malloc`s
  ([loki_coap_utils.c:465-483](src/loki_coap_utils.c#L465-L483)).

---

## Service discovery (SRP / DNS-SD)

Once on Thread, the SRP client registers the loco with the network's SRP server
(typically the OpenThread Border Router), which makes it browsable via DNS-SD.

- **Host name** = the BLE short name (e.g. `TREN34FD`), with auto host address
  (AAAA filled from the Thread addresses) — `init_srp()`
  ([main_ot_utils.c:355-434](src/main_ot_utils.c#L355-L434)).
- **Registered services** (service type → instance, port):

| Service type string | Instance name | Port | Registered in |
|---|---|---|---|
| `_ble._loki_coap._udp` (`SRP_SHORTNAME_SERVICE`) | short name | 5683 | [main.c:442](src/main.c#L442), `init_srp` |
| `_name._loki_coap._udp` (`SRP_LONGNAME_SERVICE`) | long name | 5683 | [main.c:441](src/main.c#L441) |
| `_loconet._loki_loconet._udp` (`SRP_LCN_SERVICE`) | DCC address (string) | 1234 | [main.c:452](src/main.c#L452) |

When a DCC address is set, the loco also opens a **UDP listener on port 1234**
for Loconet `OPC_WR_SL_DATA` messages addressed to its DCC number
([main.c:454-455](src/main.c#L454-L455),
[main_loki.c:166-236](src/main_loki.c#L166-L236)).

### Discovery caveats / known issues

- **Non-conformant DNS-SD service types.** A DNS-SD service type must be exactly
  two labels: `_<application>._<transport>` (RFC 6763). The strings above have
  three labels (`_ble._loki_coap._udp`), so standard browsers will not treat them
  as a single well-formed service type. See the proposal below.
- **`SRP_DCC_SERVICE` (`_dcc._loki_dcc._udp`) is defined but only used in log
  strings**; the DCC entry is actually registered under the *Loconet* service
  type/port, which is likely unintended ([main_ot_utils.h:23-28](src/main_ot_utils.h#L23-L28)).
- **No TXT records** are populated, so discovery yields a name + address + port
  but no machine-readable description of the available resources.

---

## Proposed formats

The interfaces work, but neither is *self-describing*: a generic BLE client shows
raw UUIDs, and a CoAP client cannot enumerate resources. Two low-effort,
standards-based improvements would make both discoverable.

### 1. GATT — add descriptors + a machine-readable descriptor file

**In firmware**, attach standard descriptors so tools like nRF Connect render
human-readable names and units:

```c
/* Characteristic User Description (CUD) — human label */
BT_GATT_CUD("PWM base frequency", BT_GATT_PERM_READ),

/* Characteristic Presentation Format (CPF) — type + unit, e.g. uint16 Hz */
BT_GATT_CPF(&(struct bt_gatt_cpf){
    .format      = 0x06,    /* uint16            */
    .exponent    = 0,
    .unit        = 0x2722,  /* org.bluetooth.unit.frequency.hertz */
    .name_space  = 0x01,
    .description = 0x0000,
}),
```

**As a deliverable**, ship a machine-readable descriptor (YAML/JSON) that
documents the service. Example `interface/gatt.yaml`:

```yaml
service:
  name: Loki Control
  uuid: fcbd0001-5e25-4387-99b7-53a5495a0c35
  characteristics:
    - name: speed
      uuid: fcbd0002-5e25-4387-99b7-53a5495a0c35
      properties: [read, write, notify]
      type: uint8
      range: [0, 255]
    - name: acceleration
      uuid: fcbd0003-5e25-4387-99b7-53a5495a0c35
      properties: [read, write]
      type: int8
      unit: steps_per_second
    - name: pwm_base
      uuid: fcbd0004-5e25-4387-99b7-53a5495a0c35
      properties: [read, write]
      type: uint16le
      unit: hertz
    - name: direction
      uuid: fcbd0005-5e25-4387-99b7-53a5495a0c35
      properties: [read, write]
      type: uint8
      enum: { stop: 0, forward: 1, reverse: 2 }
    - name: long_name
      uuid: fcbd0006-5e25-4387-99b7-53a5495a0c35
      properties: [read, write]
      type: utf8
      max_len: 63
    - name: dcc_address
      uuid: fcbd0007-5e25-4387-99b7-53a5495a0c35
      properties: [read, write]
      type: uint16le
    - name: joiner_credential
      uuid: fcbd000a-5e25-4387-99b7-53a5495a0c35
      properties: [read, write]
      read: eui64_hex
      write: thread_joiner_passphrase   # 6..32 chars
    - name: short_name
      uuid: fcbd000b-5e25-4387-99b7-53a5495a0c35
      properties: [read, write]
      type: utf8
      max_len: 8
```

### 2. CoAP — implement `/.well-known/core` (CoRE Link Format, RFC 6690)

The standard way for a CoAP client to discover resources. The firmware would
register a `.well-known/core` resource returning `application/link-format`
(content-format `40`):

```
GET coap://[<loco>]/.well-known/core

</speed>;rt="loki.speed";if="core.p";ct=0,
</acceleration>;rt="loki.accel";if="core.p";ct=0,
</direction>;rt="loki.direction";if="core.p",
</stop>;rt="loki.stop";if="core.a",
</name>;rt="loki.name";if="core.p";ct=0
```

This makes the CoAP surface self-describing with no out-of-band documentation.

### 3. Fix & enrich the DNS-SD registration

Use a single conformant service type plus TXT records that advertise the
capabilities, so a browser (`dns-sd -B _loki._udp`, Avahi, etc.) gets everything
in one shot:

```
Service type: _loki._udp        # two labels, RFC 6763 conformant
Instance:     <short name>       # e.g. TREN34FD
Port:         5683
TXT records:
    txtvers=1
    name=<full/long name>
    dcc=<dcc address or 0>
    rt=speed,acceleration,direction,stop,name   # CoAP resources
    eui64=<eui64 hex>
```

Optionally expose role/variant via DNS-SD **subtypes** (`_ble._sub._loki._udp`)
rather than encoding them into the base service type. Keep the Loconet listener
as its own conformant type, e.g. `_loconet._udp` on port 1234.

---

## Quick reference

| Mode | Channel | Endpoint | Discovery |
|---|---|---|---|
| Unprovisioned | BLE GATT | service `fcbd0001-…`, 8 characteristics | BLE advertisement (name + 128-bit UUID) |
| Provisioning | BLE GATT | write passphrase to `fcbd000a` (read EUI64) | — |
| Provisioned | CoAP/UDP/Thread | `coap://[<ipv6>]:5683/{speed,acceleration,direction,stop}` | SRP → DNS-SD |
| Provisioned (DCC) | Loconet/UDP | `udp://[<ipv6>]:1234` | SRP (Loconet service) |
