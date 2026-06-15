# 01 — BLE / GATT fixes

Covers the BLE-side caveats from [../INTERFACES.md](../INTERFACES.md#gatt-caveats--known-issues).

---

## 1.1 Speed notifications target the wrong attribute — **applied**

**The picture.** Zephyr's GATT API has two ways to fire a notification: hand it
an `attr*` index into the service's attribute array, or hand it a UUID and let
the stack find the value attribute by walking the service. The original code
used the first, with a hard-coded `&attrs[1]`. That was correct at one point in
the service's history, but as soon as more characteristics were added in front
of Speed, that index started pointing at someone else's declaration — silently.

**Was.** `bt_notify_speed()` notified on `&loki_service.attrs[1]`, which under
the current `BT_GATT_SERVICE_DEFINE` order is the Acceleration characteristic
*declaration*, not the Speed value (`attrs[4]`). Subscribed clients either saw
no notifications or got them on the wrong handle.

**Now.** [main_ble_utils.c `bt_notify_speed`](../src/main_ble_utils.c) uses
`bt_gatt_notify_uuid(NULL, LOKI_SPEED_UUID, loki_service.attrs, &speed_value,
sizeof(speed_value))`. The stack resolves the Speed value attribute by walking
from the service handle and matching the UUID, so any future reordering of the
characteristic list can't silently break this again. `LOKI_SPEED_UUID` comes
from the generated `loki_gatt.h` (Plan 04), so there's no extra hand-maintained
constant to drift.

**Verification.**

- [ ] Subscribe to the Speed characteristic in nRF Connect; change speed via a
      direct write or `/speed` PUT; observe notifications arrive on the
      *Speed* characteristic, not the Acceleration declaration.
- [ ] Confirm the existing `speed_ccc_cfg_changed` enable/disable logic still
      gates whether notifications fire (no regression there).

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
- **Automatic recovery on SRP failure.** Five SRP-registration failure sites
  (host-name set, auto-host-address mode, and the short/long/DCC service
  allocations) call `ble_lifecycle_recover_on_srp_failure()`, a `static inline`
  helper in [../src/main_ble_utils.h](../src/main_ble_utils.h) that re-opens
  the advertising window when `CONFIG_LOKI_BLE_RECOVERY_ON_SRP_FAIL=y`
  (default). The CoAP `/ble-recovery` path bypasses the gate — a human asking
  for recovery always gets it. Turn the Kconfig off to treat SRP outages
  silently and require the manual or detach path.

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

---

## 1.8 Guardrail: catch `static` shared-state primitives in headers

**The picture.** A `static` keyword on a variable defined inside a header
file does *not* share that variable across the firmware — it gives every
`.c` that includes the header its own private copy. Twice in the same
session this pattern bit us: the SRP buffer entries
([1.6](#16-srp-buffer-entries-are-per-tu-duplicates--applied)) and the BLE
advertise-intent atomic flag (followup to [1.7](#17-unregistering-srp-services-silently-fails-slot-leak--applied)
when the controller's atomic was moved into the header by accident). In
both cases the code *appeared* to work because all the live readers and
writers happened to live in the same TU — until they didn't, and a
"shared" flag silently stopped being shared.

Once is an oversight; twice in a week is a pattern worth blocking
mechanically.

**Why a check is worth the cost.** The footgun is easy to introduce
(`static` looks defensive; moving a definition between `.c` and `.h` reads
as a refactor) and hard to spot in review unless you're already looking
for it. The footprint matches a small set of recognisable Zephyr / OT
primitives, so a one-line `git grep` regex is enough to catch every
instance we care about — false-positive rate of essentially zero.

**Proposed check.** A small script at `tools/check_no_static_in_header.sh`
that fails (exit 1) if any matching line is found:

```sh
#!/usr/bin/env bash
# Reject `static <state-primitive> …` definitions in headers. These would
# silently give every including TU its own private copy of what should be
# shared state. Use `extern <type> foo;` in the header + one non-static
# definition in a .c file instead — see TODO/01 §1.6 and §1.7.
set -euo pipefail

PATTERN='^\s*static\s+(atomic_t|atomic_var_t|otSrpClientBuffersServiceEntry|otCoapResource|struct\s+(k_work|k_work_delayable|k_timer|k_sem|k_mutex|k_mbox|k_fifo|k_lifo|k_stack|k_pipe))\b'

hits=$(git grep -nE "$PATTERN" -- 'src/**/*.h' 'interface/**/*.h' || true)

if [ -n "$hits" ]; then
    echo "error: 'static' shared-state primitive(s) found in headers:" >&2
    echo "$hits" >&2
    echo >&2
    echo "These produce one independent copy per translation unit." >&2
    echo "Move the definition into a .c file and use 'extern …;' in the header." >&2
    exit 1
fi
```

The blocklist is intentionally **specific** (named Zephyr / OT types we
know we don't want duplicated) rather than a broad `static\s+\w+` regex —
that way legitimate uses survive without an allowlist: `static inline`
functions, `static const` lookup tables, `static_assert(…)`, and anything
PM/DT-generated all match neither alternation and pass through silently.

**Where to wire it.** Three options, increasing in ceremony:

1. **Documented manual script.** Add to `tools/`, mention in
   `interface/README.md` or a new `tools/README.md`. Devs run it locally
   when in doubt. Zero infrastructure cost, weakest signal.
2. **CMake configure-time check.** Add a small block to
   [../CMakeLists.txt](../CMakeLists.txt) that runs the script during the
   CMake configure step. Same shape as the existing
   `find_package(Python3 …)` + warn-or-skip pattern that we used for
   `tools/gen_descriptors.py` — except this one *fails* the configure
   when a hit is found, since the issue is correctness, not codegen
   convenience.
3. **Pre-commit / GitHub Action.** Native CI gating. Cleanest, but adds a
   tooling layer the repo doesn't have yet.

Option 2 is the smallest delta that actually gates `west build`. The
configure step happens at every `west build -p auto` and on first
`-p never` build after a CMakeLists change; the rest of the time the
check is silent.

**Verification.** Run the script on the current tree before wiring it —
expectation: zero hits, because [1.6](#16-srp-buffer-entries-are-per-tu-duplicates--applied),
[1.7](#17-unregistering-srp-services-silently-fails-slot-leak--applied),
and the BLE-atomic followup all moved their respective `static`-in-header
declarations to `extern` + single `.c` definition. If the script flags
something we didn't already know about, that's a third instance and the
check has paid for itself.

**Affected.** New file `tools/check_no_static_in_header.sh`; small block
added to [../CMakeLists.txt](../CMakeLists.txt) if option 2 is taken.
No source files touched.

**Effort:** S (the script is ~20 lines; CMake integration is ~5 more).
**Risk:** very low — additive, with the failure path being a clearly
explained build error rather than a runtime surprise.

**Depends on:** nothing. Worth doing independently of the rest of Plan 01.
