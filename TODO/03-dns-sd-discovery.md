# 03 — DNS-SD / SRP discovery cleanup

Covers the discovery caveats from [../INTERFACES.md](../INTERFACES.md#discovery-caveats--known-issues).

---

## 3.1 Non-conformant service types

**Problem.** A DNS-SD service type must be exactly two labels —
`_<application>._<transport>` (RFC 6763 §4.1.2). The current types have three:

| Macro | Current value | Labels |
|---|---|---|
| `SRP_SHORTNAME_SERVICE` | `_ble._loki_coap._udp` | 3 ❌ |
| `SRP_LONGNAME_SERVICE` | `_name._loki_coap._udp` | 3 ❌ |
| `SRP_LCN_SERVICE` | `_loconet._loki_loconet._udp` | 3 ❌ |

Defined in [../src/main_ot_utils.h:18-28](../src/main_ot_utils.h#L18-L28).

**Why it matters.** Standard browsers (`dns-sd -B`, Avahi, the OTBR's mDNS proxy)
won't treat these as a single well-formed service type, so discovery is
unreliable across tools.

**Fix.** Collapse to one conformant CoAP service type and carry the role/details
in TXT records (see 3.3). Suggested:

```c
#define SRP_LOKI_SERVICE   "_loki._udp"        /* CoAP control, port 5683 */
#define SRP_LOCONET_SERVICE "_loconet._udp"    /* Loconet UDP, port 1234  */
```

If you genuinely want to advertise sub-roles as separate browsable entries, use
DNS-SD **subtypes** (the `_sub` keyword), e.g. `_ble._sub._loki._udp` — not extra
labels baked into the base type.

**Affected:** [../src/main_ot_utils.h](../src/main_ot_utils.h),
[../src/main.c](../src/main.c), [../src/main_ot_utils.c](../src/main_ot_utils.c).
**Effort:** S (string change) + retest discovery. **Risk:** medium — this is a
**breaking change** for any existing client that browses the old types.

---

## 3.2 DCC entry is registered under the Loconet service

**Problem.** `SRP_DCC_SERVICE` (`_dcc._loki_dcc._udp`) is defined but only used in
log strings. The DCC instance is actually registered under `SRP_LCN_SERVICE`
(Loconet) on port 1234 ([../src/main.c:452](../src/main.c#L452)).

**Why it matters.** Discovery can't distinguish "this loco answers to DCC N" from
"this loco speaks Loconet", and the unused macro invites confusion.

**Fix.** Decide the intended model and make it explicit:

- If the DCC number is just an *attribute* of the loco → drop `SRP_DCC_SERVICE`
  and put `dcc=<n>` in the TXT record of `_loki._udp` (see 3.3).
- If DCC/Loconet is a distinct *service* → register it under `_loconet._udp` on
  1234 with the DCC number in its TXT record, and remove the dead
  `SRP_DCC_SERVICE` macro.

Recommended: DCC as a TXT attribute of the main `_loki._udp` service, plus the
separate `_loconet._udp` service for the actual UDP listener.

**Affected:** [../src/main_ot_utils.h](../src/main_ot_utils.h),
[../src/main.c](../src/main.c).
**Effort:** S. **Risk:** low–medium.

---

## 3.3 No TXT records

**Problem.** Registrations carry no TXT records, so discovery yields name +
address + port but nothing machine-readable about capabilities.

**Why it matters.** A controller has to hard-code what a Loki exposes instead of
reading it from discovery.

**Fix.** Populate TXT entries on the SRP service. With the OpenThread SRP client,
set `entry->mService.mTxtEntries` / `mNumTxtEntries` (or the raw TXT buffer via
`otSrpClientBuffersGetServiceEntryTxtBuffer`) in `register_service()`
([../src/main_ot_utils.c:492-519](../src/main_ot_utils.c#L492-L519)). Proposed
keys for `_loki._udp`:

```
txtvers=1
name=<full/long name>
dcc=<dcc address or 0>
rt=speed,acceleration,direction,stop,name
eui64=<eui64 hex>
```

Keep the total TXT size within the SRP buffer limits configured for the client.

**Affected:** [../src/main_ot_utils.c](../src/main_ot_utils.c).
**Effort:** M. **Risk:** low.
**Synergy:** the `rt=` list should be generated from the same source as
`/.well-known/core` — see [04](04-machine-readable-descriptors.md).
