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
