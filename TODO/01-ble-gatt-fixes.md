# 01 — BLE / GATT fixes

Covers the BLE-side caveats from [../INTERFACES.md](../INTERFACES.md#gatt-caveats--known-issues).

---

## 1.1 Speed notifications target the wrong attribute

**Problem.** `bt_notify_speed()` notifies on `loki_service.attrs[1]`
([../src/main_ble_utils.c:503-508](../src/main_ble_utils.c#L503-L508)). With the
current declaration order, `attrs[1]` is the **Acceleration** characteristic
*declaration*, not the Speed value attribute.

**Why it matters.** Subscribed clients either get notifications on the wrong
handle or none at all; the feature looks wired up but is effectively broken.

**Fix.** Don't hand-index the attribute array. Reference the value attribute by
its UUID-bearing entry. Two robust options:

- Use the attribute *next to* the characteristic declaration via the generated
  symbol, or
- Store a pointer to the Speed value attribute at registration time, or simplest:
  use `bt_gatt_notify_uuid()` / compute the index from the macro layout.

Recommended minimal change — point at the Speed **value** attribute (the entry
immediately after the Speed characteristic declaration). Given the order
`[0]=service, [1]=accel decl, [2]=accel val, [3]=speed decl, [4]=speed val`,
that is `attrs[4]`:

```c
void bt_notify_speed(void)
{
    bt_gatt_notify(NULL, &loki_service.attrs[4], &speed_value,
                   sizeof(speed_value));
}
```

Better: add a comment pinning the index to the declaration order, or switch to
`bt_gatt_notify_uuid(NULL, LOKI_SPEED_UUID, loki_service.attrs, ...)` so a
re-ordering of characteristics can't silently break it again.

**Affected:** [../src/main_ble_utils.c](../src/main_ble_utils.c).
**Effort:** S. **Risk:** low (verify with nRF Connect subscribe + a speed change).

---

## 1.2 DCC address written over BLE is volatile — **applied**

**Was.** `write_dcc` just assigned `dcc_address = value;` — no NVM save, no SRP
re-registration, no UDP rebind. A DCC address set over BLE survived only until
the next reboot.

**Now.** Two related helpers declared in [../src/main_loki.h](../src/main_loki.h)
and implemented in [../src/main_loki.c](../src/main_loki.c) — co-located with
the other loco-state setters (`change_speed_directly`, `change_direction`, …):

- `apply_dcc_address(uint16_t)` — runtime mutator. Updates `dcc_address`,
  persists via `settings_save_subtree("loki/dcc")`, then calls
  `register_dcc_service()`.
- `register_dcc_service(void)` — SRP/UDP registration for the current
  `dcc_address`. Idempotent: frees any prior entry first; no-op when
  `dcc_address == 0`.

Call sites:

- `write_dcc` in [../src/main_ble_utils.c](../src/main_ble_utils.c) calls
  `apply_dcc_address(value)`.
- `main()` boot path in [../src/main.c](../src/main.c) replaces the inline DCC
  block with a single call to `register_dcc_service()`, using the value already
  loaded from NVM. This also drops the `malloc(15)` leak the old block had.

**Verification.**

- [ ] Set a DCC address over BLE, reboot, observe `apply_dcc_address` /
      `register_dcc_service` logs and that DNS-SD still resolves the new DCC
      instance.
- [ ] Change the DCC over BLE while attached; confirm the old SRP instance is
      freed and the new one shows up in the OTBR's mDNS proxy.
- [ ] Write `dcc=0` and confirm the SRP entry is freed cleanly.

**Known limitations.**

- If the loco is detached when the write happens, `register_dcc_service()` will
  still call the SRP client APIs (which may log errors) — the NVM save is what
  guarantees persistence, and the boot path re-registers on next attach.
- The underlying SRP service type (`SRP_LCN_SERVICE`) is still the
  3-label non-conformant string flagged in
  [03 §3.1](03-dns-sd-discovery.md#31-use-conformant-2-label-service-types); the
  helper is structured so the rename to `_dcc._udp` will be a one-line change.
- `register_service()` ignores its `port` argument
  ([03 §3.5](03-dns-sd-discovery.md#35-cross-cutting-registration-lifecycle-hygiene));
  the SRV record currently always advertises `OT_DEFAULT_COAP_PORT` (5683)
  instead of `SRP_LCN_PORT` (1234). The helper passes the intended port so the
  fix in 03 §3.5 will take effect with no further change here.

---

## 1.3 Long-name write over BLE does not re-register SRP

**Problem.** The BLE long-name characteristic write calls `updateBleLongName()`
(BLE name + scan response only). The SRP-aware `modify_full_name()`
([../src/main.c:324-344](../src/main.c#L324-L344)) — which re-registers the
`_name` SRP service — is only reachable through the (currently unregistered)
CoAP `/name` resource.

**Why it matters.** Renaming over BLE leaves the Thread/DNS-SD long-name service
advertising the stale name.

**Fix.** Have the BLE long-name write call `modify_full_name(buf, len)` (which
itself calls `updateBleLongName`) instead of `updateBleLongName` directly, but
**only when Thread is up** (guard on commissioned/SRP-enabled so the unprovisioned
path doesn't touch SRP). Also persist `loki/longname` like the short name does.

**Affected:** [../src/main_ble_utils.c:207-227](../src/main_ble_utils.c#L207-L227),
[../src/main.c](../src/main.c).
**Effort:** S–M. **Risk:** medium (ordering: name change vs SRP availability).

---

## 1.4 `write_name` one-byte overflow — **applied**

**Was.** `write_name` declared `char new_name[MAX_LEN_FULL_NAME]` (63) and checked
`len > sizeof(new_name)`. When `len == 63`, the bounds check passed and the
subsequent `new_name[len] = '\0'` wrote the terminator one past the end.

**Now.** Buffer enlarged to `MAX_LEN_FULL_NAME + 1` and the bounds check uses the
constant directly (`len > MAX_LEN_FULL_NAME`); see
[../src/main_ble_utils.c](../src/main_ble_utils.c) `write_name`.

**Verification.** A 63-byte write now lands the terminator at `new_name[63]`,
which is inside the 64-byte buffer; a 64-byte write is rejected with
`BT_ATT_ERR_INVALID_ATTRIBUTE_LEN`. Worth confirming in nRF Connect with a
63-char name once the firmware is reflashed.

---

## 1.5 BLE off after successful Thread attach — **applied**

**Decision.** Option B from the original sketch: keep the boot-time advertise so
recovery is always possible, but stop advertising once Thread has actually
attached for a configurable grace period. If attach never happens (no dataset,
or commissioned-but-network-down), BLE stays on indefinitely. A CoAP
`/ble-recovery` endpoint re-opens the window from a connected client.

**Mechanism (now in tree).**

- `CONFIG_LOKI_BLE_OFF_AFTER_ATTACH_MINUTES` ([../Kconfig](../Kconfig)),
  default **5**, range `0..1440`. Setting to `0` disables the feature.
- Atomic intent flag `ble_should_advertise` in
  [../src/main_ble_utils.c](../src/main_ble_utils.c), gating both the
  `start_advertising` work and the post-disconnect restart in `disconnected_cb`.
- A `K_WORK_DELAYABLE` (`ble_stop_work`) scheduled when Thread attaches; on fire
  it calls `bt_le_adv_stop()` (existing connections survive — only new advertising
  is halted).
- Three exported entry points in [../src/main_ble_utils.h](../src/main_ble_utils.h):
  - `ble_lifecycle_on_thread_attached()` — schedule the stop.
  - `ble_lifecycle_on_thread_detached()` — cancel pending stop, resume advertising.
  - `ble_lifecycle_force_recovery()` — re-open a fresh window (CoAP entry point).
- Hooks fire from `on_thread_state_changed` in
  [../src/main_ot_utils.c](../src/main_ot_utils.c) (CHILD/ROUTER/LEADER →
  attached; DETACHED/DISABLED → detached).
- The CoAP escape hatch lives at URI path `ble-recovery` (`BLE_RECOVERY_URI_PATH`
  in [../interface/loki_server_client_interface.h](../interface/loki_server_client_interface.h)),
  registered through a new `on_ble_recovery_request` callback added to
  `loki_coap_init()` in [../src/loki_coap_utils.c](../src/loki_coap_utils.c).
  `main()` wires it to `ble_lifecycle_force_recovery`.

**Behavior matrix.**

| Boot state | Thread role over time | BLE advertising |
|---|---|---|
| Not commissioned | stays DISABLED | on, indefinitely |
| Commissioned, BR down | stays DETACHED | on, indefinitely |
| Commissioned, attach OK | DETACHED → CHILD | on for N min after attach, then off |
| Attached, network drops | CHILD → DETACHED | resumes immediately |
| Attached, `/ble-recovery` | (any) | back on, fresh N-min window |

**Verification checklist.**

- [ ] Build with default Kconfig; observe `LOG_INF "Thread attached; BLE
      advertising will stop in 5 min"` after attach, then `"stopping BLE
      advertising"` after 5 min, plus advertising actually going dark in nRF
      Connect.
- [ ] Force a Thread detach (power-cycle BR) and confirm BLE comes back.
- [ ] From a Thread-connected client:
      `coap-client -N -m put coap://[<loco>]:5683/ble-recovery` →
      advertising resumes for another window.
- [ ] Build with `CONFIG_LOKI_BLE_OFF_AFTER_ATTACH_MINUTES=0`; BLE never stops.
- [ ] Existing BLE client connected when timer fires: session not kicked, but no
      new connections accepted until recovery.

**Known follow-ups (not blocking).**

- The re-provisioning passphrase write (credential char) still happens over BLE.
  If BLE is off and the user wants to re-provision onto a different network,
  they must either trigger `/ble-recovery` from the current network or take
  Thread down so the detach path resumes BLE.
- The boot-time always-on window means a freshly rebooted commissioned loco is
  briefly BLE-visible. Acceptable; matches the "always reachable to grab" intent.

---

## 1.6 SRP buffer entries are per-TU duplicates — **applied**

**Was.** [../src/main_ot_utils.h:18-27](../src/main_ot_utils.h#L18-L27)
declared the SRP buffer storage with `static` linkage **in the header**:

```c
static otSrpClientBuffersServiceEntry short_name_coap_service;
static otSrpClientBuffersServiceEntry long_name_coap_service;
static otSrpClientBuffersServiceEntry dcc_name_coap_service;
static otSrpClientBuffersServiceEntry loconet_udp_service;
```

`static` at file scope gives internal linkage, so **every translation unit that
includes the header gets its own private copy** of each variable — they are not
the same memory.

**Why it matters.** Each `.c` file that touches these symbols is operating on
separate storage:

- `init_srp()` in [../src/main_ot_utils.c](../src/main_ot_utils.c) updates *its
  own* `short_name_coap_service`.
- `main()` in [../src/main.c](../src/main.c) — and now `register_dcc_service()`
  in [../src/main_loki.c](../src/main_loki.c) after [1.2](#12-dcc-address-written-over-ble-is-volatile--applied) —
  operate on *their own* copies.

The code works today only because each TU happens to keep its own lifecycle
internally consistent (`init_srp` manages the short-name entry; `main_loki.c`
manages the DCC entry; the long-name path in `main.c` re-allocates rather than
sharing). The moment cross-TU bookkeeping becomes necessary — e.g. a SRP
rename triggered in one file expected to be visible in another — the bug bites
silently. It also multiplies the RAM footprint of these buffers by the number
of including TUs (currently 3).

**Now.** The header has `extern` declarations
([../src/main_ot_utils.h:21-29](../src/main_ot_utils.h#L21-L29)) and the single
definitions live in `main_ot_utils.c` alongside the existing
`otUdpSocket loconet_udp_socket`
([../src/main_ot_utils.c:54-57](../src/main_ot_utils.c#L54-L57)). Zero-init in
BSS preserves the `mInstanceName != NULL` "is something registered?" idiom
already used at call sites.

**Audit findings.** Making the variables actually shared exposed two latent
issues that the per-TU duplicates had been masking — both pre-existing, neither
introduced by the §1.6 change:

1. **Short-name service was being registered twice at boot.** `init_srp()` in
   [../src/main_ot_utils.c](../src/main_ot_utils.c) registers the short-name
   service. The `main()` boot block in [../src/main.c:608-617](../src/main.c#L608-L617)
   then *also* calls `register_coap_service(..., ble_name, SRP_SHORTNAME_SERVICE)`.
   Before §1.6 the redundancy was invisible (each TU thought its own copy was
   empty). After §1.6 the redundancy is now visible via the
   `"freeing first"` log line firing on every boot — and the underlying
   double-registration with the SRP server is now obvious.
2. **`otSrpClientBuffersFreeService()` is being called with the wrong pointer.**
   `register_service()` copies the entry contents into the static via
   `short_name_coap_service = *entry;` — so `&short_name_coap_service` is the
   address of a *snapshot*, not a pointer into the OT buffer pool returned by
   `otSrpClientBuffersAllocateService`. The OT API contract requires the
   original pool pointer. See [1.7](#17-srp-entries-are-stored-as-snapshot-copies-not-pool-pointers)
   for the fix.

**Verification.**

- [ ] Build + boot a commissioned loco; expect to see the "freeing first" log
      now fire for the short-name service (was previously silent).
- [ ] BLE-driven DCC change still persists across reboot and DNS-SD discovery
      tracks the change (1.2 smoke test, but now against shared storage).
- [ ] BLE-driven short-name rename: `modify_short_name` →
      `re_register_coap_service` and `init_srp()`'s view of
      `short_name_coap_service` now agree. Confirm the rename surfaces in
      DNS-SD on the next attach (this is the case that silently diverged
      before).
- [ ] Memory: each of the four entries now has one storage location instead of
      three — a small but measurable RAM win.

---

## 1.7 Unregistering SRP services silently fails (slot leak) — **applied**

**The picture.** OpenThread's SRP client has a small, fixed-size cabinet of
slots for service registrations. When the firmware "registers a service" it
reserves a slot and hands back a numbered ticket — that ticket is how you
later say "take it off slot 3, please". Lose the ticket, and OpenThread has
no way to know which slot you meant.

**Was.** After registering, the firmware copied the *contents* of OpenThread's
slot into a global of its own (`short_name_coap_service = *entry;`) and then
threw the ticket away. At teardown it passed the address of that copy to
`otSrpClientBuffersFreeService` — but the API expects the original ticket,
not a transcription of what was on it. OpenThread didn't recognise the
address, quietly did nothing, and the slot stayed occupied. Every rename
silently leaked one more slot of the fixed pool; the
`if (… != NULL) { free; }` idiom that read like real teardown wasn't.

This came out of the audit for
[1.6](#16-srp-buffer-entries-are-per-tu-duplicates--applied) — the per-TU
duplicate variables had been masking it.

**Now.** The four globals hold the *ticket* itself: a pointer into the SRP
buffer pool, not a copy of what it points at. NULL means "no current
registration":

- [main_ot_utils.h:23-31](../src/main_ot_utils.h#L23-L31) — the four entries
  are now pointer-typed (`extern otSrpClientBuffersServiceEntry *short_name_coap_service;`
  etc.), with matching single definitions in
  [main_ot_utils.c:54-57](../src/main_ot_utils.c#L54-L57).
- [`re_register_service` / `re_register_coap_service`](../src/main_ot_utils.c#L469-L478)
  now take `**entry`. They free the old pool entry, allocate a new one, and
  **rebind the caller's variable**. Pre-1.7 the function only updated a local
  copy — every callsite happily believed the entry had been refreshed when it
  hadn't.
- [`init_srp` short-name block](../src/main_ot_utils.c#L421-L438) — the
  tautological `&foo != NULL` check is gone; the snapshot copy
  (`= *entry`) is now a pointer save (`= entry`); and there's a real
  free-before-re-register so a renamed loco doesn't leak a slot when
  `init_srp` re-runs on IP/multicast change events.
- [`register_dcc_service`](../src/main_loki.c#L142-L162) — pointer null-check,
  pool pointer passed to `Free` (no longer the address of a stack copy),
  null on teardown, pointer save on register.
- [`main()` boot](../src/main.c#L608-L621) — **deleted** the redundant short-
  name re-registration. `init_srp()` already registers it; the duplicate was
  visible only because [1.6](#16-srp-buffer-entries-are-per-tu-duplicates--applied)
  unified the storage. The long-name path now captures the returned pointer
  into `long_name_coap_service` instead of dropping it on the floor.
- Callsites that already passed `&long_name_coap_service` to `re_register_*`
  ([main.c:498](../src/main.c#L498), [main.c:520](../src/main.c#L520)) needed
  no change — with the type flip, `&foo` is now `T**` which matches the new
  signature.

**Verification.**

- [ ] Repeated short-name renames via BLE no longer exhaust the SRP pool. Pre-
      fix: roughly `CONFIG_OPENTHREAD_SRP_CLIENT_BUFFERS_SERVICES` renames
      before new registrations start failing. Post-fix: indefinite.
- [ ] At boot, the `"freeing first"` log line for the **short-name** service no
      longer appears (the redundant boot-block path is gone). The long-name
      line still appears only on a real re-register.
- [ ] DCC change via BLE: `dns-sd -B _loconet._loki_loconet._udp` on the OTBR
      shows the old DCC instance disappearing before the new one appears,
      instead of accumulating duplicates.
- [ ] Long-name change once the CoAP `/name` endpoint is wired up
      ([02 §2.1](02-coap-fixes.md#21-name-resource-is-never-registered))
      surfaces in DNS-SD on the next attach instead of silently leaking a
      slot.

**Depends on:** [1.6](#16-srp-buffer-entries-are-per-tu-duplicates--applied)
(done).
