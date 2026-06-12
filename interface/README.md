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
