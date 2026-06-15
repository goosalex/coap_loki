# `interface/` — Loki control contract

This directory holds the **source-of-truth descriptors** for the loco's two
external interfaces, plus the generated C headers that the firmware actually
compiles against.

```
interface/
├── gatt.yaml            ← edit this: BLE GATT service + characteristics
├── coap.yaml            ← edit this: CoAP port, resources, direction enum
├── generated/
│   ├── loki_gatt.h      ← AUTO-GENERATED, do not edit
│   └── loki_coap.h      ← AUTO-GENERATED, do not edit
├── loki_server_client_interface.h   ← thin wrapper, just #includes loki_coap.h
└── coap_server_client_interface.h   ← legacy demo (light/provisioning)
```

## What lives where

- **YAML is the source of truth.** All UUIDs, URI paths, the CoAP port, the
  BLE-name length caps and the direction enum values live in `gatt.yaml` and
  `coap.yaml`. They are the contract the firmware and any external client
  agree on.
- **Generated `.h` files mirror the YAML.** They are committed to the repo so
  the build still works on machines without Python/PyYAML — but they are
  rewritten on every full build that *does* have Python.
- **Firmware sources include the generated headers transitively.**
  `main_ble_utils.h` pulls in `loki_gatt.h`; `loki_server_client_interface.h`
  pulls in `loki_coap.h`. None of the firmware C files contain the values
  directly.

## Optional GATT features

### Characteristic User Description (CUD) descriptors

By default the generator emits a **Characteristic User Description** (CUD,
UUID `0x2901`) descriptor per BLE characteristic — generic debuggers like nRF
Connect and BlueZ's `bluetoothctl` render the label next to the raw UUID, so
`fcbd0002-…` shows up as **Speed** instead of an opaque blob.

Toggle in [gatt.yaml](gatt.yaml):

```yaml
service:
  …
  cud_enabled: true     # default; set to false to drop CUDs entirely
```

Label source per characteristic, in priority order:

1. An explicit `cud:` field on the characteristic (verbatim string, used for
   acronyms — `cud: "PWM Base"` keeps it from rendering as "Pwm Base").
2. Otherwise, `name:` with underscores replaced by spaces and Title-Cased.

How the toggle reaches the firmware (without `#ifdef` noise in the C):

- When **enabled**, the generated header carries two things — a per-character
  string macro `LOKI_<SYM>_CUD` and a wrapper macro
  `LOKI_GATT_CUD_ITEM(label)` whose body is
  `, BT_GATT_CUD(label, BT_GATT_PERM_READ)` (leading comma deliberate).
- When **disabled**, only the wrapper macro is emitted, with an empty body.
  No `LOKI_<SYM>_CUD` labels are emitted at all.

The firmware's [main_ble_utils.c](../src/main_ble_utils.c) uses
`LOKI_GATT_CUD_ITEM(LOKI_<SYM>_CUD)` inline after each
`BT_GATT_CHARACTERISTIC(...)` entry. Because the wrapper is function-like and
its body never references the argument when disabled, the call site compiles
identically in both states — the YAML toggle is the single point of control
and no source files need editing when it flips.

Trade-off: each CUD descriptor adds one attribute to the service's
attribute table. Disable if you're tight on `CONFIG_BT_ATT_PREPARE_COUNT` or
attr-table memory.

## How to change a value

1. Edit `gatt.yaml` or `coap.yaml`.
2. Run a build — CMake invokes `tools/gen_descriptors.py` automatically and
   the headers are refreshed before compilation.
3. Commit both the YAML edit and the regenerated header.

To regenerate by hand (e.g. to inspect a diff before committing):

```sh
python3 tools/gen_descriptors.py \
    --gatt    interface/gatt.yaml \
    --coap    interface/coap.yaml \
    --out-dir interface/generated
```

## Build-time behaviour

The CMake block in the root `CMakeLists.txt`:

- Looks for **Python ≥ 3.8** via `find_package(Python3 3.8 …)`.
- If found, registers `loki_gen_descriptors` as an `ALL` target that depends
  on `loki_gatt.h` / `loki_coap.h`. The custom command re-runs the script
  whenever a YAML file, the script itself, or one of the outputs changes.
- If Python is missing or too old, emits a `CMake WARNING` and skips the
  codegen target — the build proceeds with whatever headers are committed.
- The script itself **also** skips with a warning (and exit 0) if PyYAML is
  not installed — so a system with Python 3.8+ but no PyYAML degrades the
  same way as one without Python.

The net guarantee: **the build never fails because of the codegen layer**.
Worst case is "the YAML you edited didn't get applied to the generated
headers" — caught visibly via the warning lines.

## Why generate, instead of hand-editing C macros?

- One value, one place. UUIDs and URI paths used to live as `#define`s in
  `src/main_ble_utils.c` and `interface/loki_server_client_interface.h`,
  separate from each other and from the documentation. Drift was easy.
- The YAML files double as machine-readable documentation — they can be the
  input to a `/.well-known/core` link-format generator (TODO/02 §2.5), an
  SRP TXT record builder (TODO/03 §3.3), or a host-side client library.
- Adding a new characteristic or CoAP resource is one YAML stanza, not three
  edits across two files plus a forgotten doc update.

See also [TODO/04-machine-readable-descriptors.md](../TODO/04-machine-readable-descriptors.md)
for the original plan that led to this layout.
