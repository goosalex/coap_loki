# 04 — Machine-readable descriptors: where to put them

Goal: ship machine-readable descriptors for the **GATT** service and the **CoAP**
resource set so that clients (and CI, and this repo's docs) have a single,
authoritative contract — and so the firmware's runtime discovery
(`/.well-known/core`, DNS-SD TXT records) can be generated from it rather than
drifting.

## The core problem: where does the contract live today?

The interface facts are currently spread across source files:

| Fact | Lives in |
|---|---|
| GATT UUIDs | `#define` macros in [../src/main_ble_utils.c:51-109](../src/main_ble_utils.c#L51-L109) |
| GATT properties / handlers | `BT_GATT_SERVICE_DEFINE` in [../src/main_ble_utils.c:304-353](../src/main_ble_utils.c#L304-L353) |
| CoAP URI paths + enums | [../interface/loki_server_client_interface.h](../interface/loki_server_client_interface.h) |
| CoAP port | `COAP_PORT` in the same header |

The CoAP paths/port already live in a shared `interface/` header that is included
by both server and (hypothetical) client. **That makes `interface/` the natural
home for the descriptors too.** GATT, by contrast, has no shared header — its
UUIDs are private to `main_ble_utils.c`.

## Proposed layout

```
interface/
├── README.md                         # explains these contract artifacts
├── coap_server_client_interface.h    # (existing)
├── loki_server_client_interface.h    # (existing)
├── gatt.yaml                         # NEW — GATT service descriptor
└── coap.yaml                         # NEW — CoAP resource descriptor
tools/
└── gen_descriptors.py                # NEW (optional) — codegen / validation
```

Rationale:

- `interface/` is already the "shared contract" directory and is consumed by
  builds — descriptors belong next to the headers that define the same contract.
- YAML over JSON for the hand-edited source (comments, readability); emit JSON
  too if a web/mobile client wants it.
- `tools/` keeps build-time scripts out of `src/`.

The YAML *content* is already drafted in
[../INTERFACES.md](../INTERFACES.md#proposed-formats) — move those blocks into
`interface/gatt.yaml` and `interface/coap.yaml` verbatim as the starting point.

## Single-source-of-truth: three strategies

The real question is the relationship between the YAML and the C. Pick one:

### Option A — Docs-first (descriptors are documentation)
- `interface/*.yaml` are hand-maintained, treated as the human/tooling-facing
  spec. Firmware stays as-is.
- **Pro:** zero build complexity, immediate value. **Con:** can drift from code.
- **Mitigation:** a CI check (below) that fails if YAML and headers disagree.

### Option B — Generate firmware artifacts from YAML (YAML is source of truth)
- `tools/gen_descriptors.py` runs at build time (CMake `add_custom_command`) and
  emits:
  - `loki_gatt_uuids.h` — the `BT_UUID_*` / `*_UUID_VAL` macros,
  - `loki_coap_resources.h` — the `*_URI_PATH` defines + `COAP_PORT`,
  - `loki_well_known_core.inc` — the CoRE Link Format string for
    [`/.well-known/core`](02-coap-fixes.md#25-no-coap-resource-discovery-well-knowncore),
  - the `rt=` TXT value for [DNS-SD](03-dns-sd-discovery.md#33-no-txt-records).
- **Pro:** firmware discovery output cannot drift from the spec. **Con:** adds a
  Python build dependency and codegen step; bigger up-front change.

### Option C — Extract YAML from annotated headers (code is source of truth)
- Annotate the headers/service definition and have a script emit the YAML.
- **Pro:** code stays canonical. **Con:** parsing C macros reliably is fiddly.

### Recommendation
**Start with A, design for B.** Land `interface/gatt.yaml` + `interface/coap.yaml`
now as documentation, add the CI consistency check, and adopt Option B
incrementally — first generating only the runtime-discovery strings
(`/.well-known/core` + TXT `rt=`), which is exactly where drift hurts most, before
generating the UUID/path headers.

## CI consistency check (cheap, high value)

A small test that greps the headers/macros and asserts they match the YAML:

- `coap.yaml` paths/port == `*_URI_PATH` / `COAP_PORT` in
  [../interface/loki_server_client_interface.h](../interface/loki_server_client_interface.h).
- `gatt.yaml` UUIDs == the `*_UUID_VAL` macros in
  [../src/main_ble_utils.c](../src/main_ble_utils.c).

Run it in the existing build/test workflow so a divergence breaks the build.
This makes Option A safe and is reusable as the validation half of Option B.

## Suggested schema notes

- **GATT** (`gatt.yaml`): per characteristic record `name`, `uuid`, `properties`,
  `type` (`uint8`/`int8`/`uint16le`/`utf8`), `range`/`enum`/`unit`, `max_len`.
  This is also the input for adding CUD/CPF descriptors in firmware (see
  [01](01-ble-gatt-fixes.md)) so nRF Connect shows names/units.
- **CoAP** (`coap.yaml`): top-level `port`; per resource `path`, `methods`,
  `request`/`response` payload encoding, `rt`/`if`/`ct` attributes for the
  link-format output.

## Work items

- [ ] Create `interface/gatt.yaml` from the INTERFACES.md draft.
- [ ] Create `interface/coap.yaml` from the INTERFACES.md draft.
- [ ] Add `interface/README.md` describing the artifacts + chosen SoT strategy.
- [ ] Add CI consistency check (Option A safety net).
- [ ] (Later / Option B) `tools/gen_descriptors.py` + CMake hook to emit
      `/.well-known/core` string and DNS-SD `rt=` from `coap.yaml`.

**Effort:** S for A (+ check); M–L for B. **Risk:** low (additive; no runtime
behaviour change until B).
