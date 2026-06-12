# 03 — DNS-SD / SRP discovery — four-goal redesign

Supersedes the original "cleanup" framing. The discovery layer should be designed
around what clients actually need to do, not around what the firmware happens to
register today. Four concrete goals drive the design.

## Design goals

| # | Goal | Caller | Lookup style |
|---|---|---|---|
| (a) | Enumerate all present locos | backbone client (UI, dispatcher) | **browse** a service type |
| (b) | "I scanned NFC tag T — who owns it?" | trackside NFC scanner | **point lookup** by tag |
| (c) | "Address loco by DCC #N" | DCC/Loconet throttle | **point lookup** by DCC# |
| (d) | "Address loco by BLE short name" | arbitrary DNS client | **name resolution** (AAAA) |

The backbone caller (a) must not have to know it's talking to IPv6-on-Thread; an
OTBR with the mDNS proxy / DNS-SD bridge already takes care of that, *provided*
the loco's registrations are conformant DNS-SD.

## Mapping goals to DNS-SD primitives

| Goal | DNS-SD primitive | Cost per loco |
|---|---|---|
| (a) | `PTR` of `_loki._udp` → instance | 1 SRV under `_loki._udp` |
| (b) | `SRV` of `<tag>._nfc._udp` → host:port | 1 SRV **per NFC tag** |
| (c) | `SRV` of `<dcc#>._dcc._udp` → host:port | 1 SRV (only if DCC set) |
| (d) | `AAAA` of `<short-name>.<domain>` | **0** — it's the SRP host record |

Two observations:

- **Goal (d) is free.** `init_srp()` already sets `hostName = ble_name`
  ([../src/main_ot_utils.c:376-397](../src/main_ot_utils.c#L376-L397)), so
  `<short-name>.default.service.arpa` already resolves directly to the loco's
  Thread IPv6 via SRP. No service registration needed for (d).
- **Each addressing scheme uses the *instance name* of a dedicated service type
  as the key.** A point lookup (b)/(c) is then a single `SRV` query — no
  enumeration, no client-side filtering, scales to large fleets.

## Why not "one service + TXT-based filtering"?

The tempting "single `_loki._udp` registration, stuff `dcc=` and `nfc=` into
TXT" reduces the registration count, but it forces every NFC scanner and every
DCC throttle to **enumerate every loco** on every lookup and read every TXT. For
a trackside reader firing hundreds of events per session, that is the wrong
tradeoff. Subtypes (`_dcc._sub._loki._udp`) don't help: they let you *browse*
DCC-addressable locos, not look up by DCC number directly.

Keep TXT for *descriptive* metadata (capability advertisement, cross-references),
not as the primary addressing channel.

## Target footprint per loco

```
Host AAAA:                  <short-name>.<domain>       → <thread-ipv6>            [goal d]

SRV  <short-name>._loki._udp     → <host>:5683
     TXT  txtvers=1
          name=<long name>
          dcc=<n or 0>
          nfc=<tag1>,<tag2>,…
          eui64=<hex>
          rt=speed,acceleration,direction,stop,name                                [goal a]

SRV  <dcc#>._dcc._udp            → <host>:1234            (when DCC set)
     TXT  short=<short-name>
          proto=loconet                                                            [goal c]

SRV  <tag>._nfc._udp             → <host>:5683            (one per tagged carriage)
     TXT  short=<short-name>                                                       [goal b]

(SRV  <short-name>._loconet._udp → <host>:1234   — optional, transport advert)
```

Minimum: **1 host + 1 service** (unconfigured loco).
Typical: **1 host + 3 services** (loco with a DCC# and one tagged carriage).
The SRP client must be sized for the expected NFC tag count — see
`OPENTHREAD_CONFIG_SRP_CLIENT_BUFFERS_MAX_SERVICES` (defaults are small; raise to
`1 (loki) + 1 (dcc) + N (nfc) + headroom`).

All entries share the **same host AAAA**, so a single Thread address change
propagates to every lookup path through one SRP host update.

## What changes from today

| Today | Target |
|---|---|
| `_ble._loki_coap._udp` (instance = short name) — 3 labels ❌ | dropped — replaced by `_loki._udp`; (d) already covered by AAAA |
| `_name._loki_coap._udp` (instance = long name) — 3 labels ❌ | dropped — long name moves to TXT `name=` on `_loki._udp` |
| `_loconet._loki_loconet._udp` (instance = DCC#) on port 1234 — 3 labels ❌ | split: `_dcc._udp` (instance = DCC#) for **addressing**, and optional `_loconet._udp` for **transport advert** |
| `SRP_DCC_SERVICE` macro defined but unused | removed |
| No TXT records | TXT on every service (descriptive, see above) |

This is fewer wire bytes and fewer concepts than the current state, not more.

---

## 3.1 Use conformant 2-label service types

**Problem.** Every current service type has three labels
([../src/main_ot_utils.h:18-28](../src/main_ot_utils.h#L18-L28)). Standard tools
(`dns-sd -B`, Avahi, the OTBR mDNS proxy) expect exactly two —
`_<application>._<transport>` per RFC 6763 §4.1.2.

**Fix.** Replace the macros with the three addressing service types plus an
optional transport advert:

```c
/* Addressing surfaces — instance name carries the lookup key. */
#define SRP_LOKI_SERVICE      "_loki._udp"     /* enumeration + control, port 5683 */
#define SRP_DCC_SERVICE       "_dcc._udp"      /* DCC#  → loco,           port 1234 */
#define SRP_NFC_SERVICE       "_nfc._udp"      /* NFC tag → loco,         port 5683 */

/* Optional: advertise the Loconet UDP transport itself. */
#define SRP_LOCONET_SERVICE   "_loconet._udp"  /*                          port 1234 */
```

Instance name on `_loki._udp` is the BLE short name (matches host name → same
identifier everywhere); on `_dcc._udp` the DCC number as a decimal string; on
`_nfc._udp` the tag UID (hex). `_name._…` is gone — the long name lives in TXT.

**Affected:** [../src/main_ot_utils.h](../src/main_ot_utils.h),
[../src/main_ot_utils.c](../src/main_ot_utils.c),
[../src/main.c](../src/main.c).
**Effort:** S (string change + call-site cleanup).
**Risk:** medium — **breaking change** for any client browsing the old 3-label
types. Coordinate with controller/UI updates.

---

## 3.2 Separate DCC addressing from Loconet transport

**Problem.** The DCC# instance is registered under `SRP_LCN_SERVICE`
([../src/main.c:452](../src/main.c#L452)), conflating two concerns: "I am
addressable as DCC N" (goal c) and "I speak Loconet UDP on port 1234" (transport
advert). Meanwhile `SRP_DCC_SERVICE` is defined but only ever appears in log
strings.

**Why it matters.** A throttle that wants to send to DCC 53 cannot do a single
`SRV` query of `53._dcc._udp` today — it has to know the address lives under the
Loconet service type, which is non-obvious and non-standard.

**Fix.**

1. Register the DCC# as the instance of `_dcc._udp` (port 1234 — the Loconet UDP
   listener) with `proto=loconet` in TXT so future protocols can be discriminated.
2. (Optional) keep one `_loconet._udp` instance under the loco's short name as a
   transport advert, with TXT linking back (`short=…`, `dcc=…`). Skip this if no
   client needs to *browse* "all Loconet-speakers" separately from "all DCC
   addresses".
3. Re-registration on DCC change goes through the helper proposed in
   [01 §1.2](01-ble-gatt-fixes.md#12-dcc-address-written-over-ble-is-volatile).

**Affected:** [../src/main_ot_utils.h](../src/main_ot_utils.h),
[../src/main.c](../src/main.c).
**Effort:** S. **Risk:** low (once 3.1 is in).

---

## 3.3 Introduce `_nfc._udp` for trackside tag addressing

**Problem.** Goal (b) is not addressed at all today. A trackside scanner reading
a tag under a passing carriage has no way to map that tag → the loco hauling it.

**Fix.** Register one `_nfc._udp` SRP service per NFC tag the loco "owns" (one
per tagged carriage). Instance name = tag UID; SRV target = the loco's host;
port = wherever the loco accepts sighting events (recommend `5683` and add a
CoAP `/sighted` resource, or pick a dedicated UDP port and document it).

Design points worth deciding before implementing:

- **Tag UID encoding.** A 7-byte NTAG UID is 14 hex chars — fits the 63-char DNS
  label limit. Pick a fixed encoding (upper-case hex, no separators) and stick
  to it; mismatched casing/separators between scanner and registrar will break
  lookups silently.
- **Where the tag set is stored on the loco.** Persist alongside the other
  `loki/…` settings ([../src/main.c:252-267](../src/main.c#L252-L267)). Add a
  way to add/remove tags (a new CoAP resource is the natural fit).
- **SRP service count budget.** Each tag is a separate service. Raise
  `CONFIG_OPENTHREAD_SRP_CLIENT_BUFFERS_SERVICES` (or the equivalent for your
  OT version) to accommodate the expected tags-per-loco maximum + headroom.
- **Scanner-side flow.** Scanner does an `SRV` query for `<uid>._nfc._udp` →
  gets `<host>:<port>` → sends the sighting message. No browse, no enumeration.

**Affected:** new code in [../src/main_ot_utils.c](../src/main_ot_utils.c),
settings handlers in [../src/main.c](../src/main.c), plus a new CoAP resource or
UDP listener for the sighting events.
**Effort:** M–L (storage, lifecycle, scanner protocol). **Risk:** low — purely
additive once 3.1 is in place.

---

## 3.4 Populate TXT records (descriptive metadata)

**Problem.** No registration currently carries TXT records, so a discoverer
learns only "this name resolves to that IPv6 on that port".

**Fix.** Populate TXT on each service via the OpenThread SRP client. The TXT
buffer is allocated through
`otSrpClientBuffersGetServiceEntryTxtBuffer()` and set on the service entry
before `otSrpClientAddService()`. Wire that into `register_service()` /
`register_coap_service()`
([../src/main_ot_utils.c:447-519](../src/main_ot_utils.c#L447-L519)) so every
caller passes its TXT keys.

Proposed keys per service type:

```
_loki._udp:    txtvers=1
               name=<long name>
               dcc=<n or 0>
               nfc=<tag1>,<tag2>,…       # capability advert, not addressing
               eui64=<hex>
               rt=speed,acceleration,direction,stop,name
_dcc._udp:     short=<short name>
               proto=loconet
_nfc._udp:     short=<short name>
```

Constraints:

- TXT records on every service must fit the SRP client's TXT buffer
  (configurable; check `OPENTHREAD_CONFIG_SRP_CLIENT_BUFFERS_TXT_BUFFER_SIZE` or
  equivalent for your OT version).
- The `rt=` value should be **generated from `interface/coap.yaml`** rather than
  hand-maintained — see [04 §work items](04-machine-readable-descriptors.md#work-items).
- `nfc=` on `_loki._udp` is informational (capability listing); the actual
  per-tag addressing happens via 3.3, not via parsing this TXT.

**Affected:** [../src/main_ot_utils.c](../src/main_ot_utils.c) (registration
helpers); call sites in [../src/main.c](../src/main.c).
**Effort:** M. **Risk:** low.

---

## 3.5 Cross-cutting: registration lifecycle hygiene

A few smaller items that show up regardless of which scheme you adopt — worth
fixing at the same time as 3.1–3.4 to avoid leaving stale entries on the SRP
server:

- `init_srp()` re-runs on every IPv6 address change / multicast change event
  ([../src/main_ot_utils.c:199-232](../src/main_ot_utils.c#L199-L232)). It
  stops the SRP client and re-registers from scratch. Verify this doesn't
  leave orphaned services with the SRP server (key signing reuse, lease
  timing).
- The `srp_client_mutex` is referenced but commented out
  ([../src/main_ot_utils.c:355-432](../src/main_ot_utils.c#L355-L432)); the
  `LOG_WRN("Cannot lock Init SRP client routines")` always fires regardless of
  outcome.
- ~~`register_service()` ignores its `port` argument and always uses
  `OT_DEFAULT_COAP_PORT`~~ — **applied.** Both `register_service` and
  `re_register_service` now run their `port` argument through a small
  validator `srp_valid_or_default_port()` in
  [../src/main_ot_utils.c](../src/main_ot_utils.c) that accepts `[1, 65535]`
  and falls back to `OT_DEFAULT_COAP_PORT` with a `LOG_WRN` for 0/negative/
  out-of-range. The pre-existing call site at
  [../src/main_loki.c](../src/main_loki.c) `register_dcc_service` already
  passed `SRP_LCN_PORT` (1234); it was previously silently downgraded to
  5683 in SRP, so the DCC/Loconet SRV record will now advertise port 1234
  to match the actual UDP listener. **Unblocks 3.2 and 3.3.**

**Effort:** S. **Risk:** low. **Status:** port bug applied; the other two
items above remain open.
