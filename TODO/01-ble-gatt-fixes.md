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

## 1.2 DCC address written over BLE is volatile

**Problem.** `write_dcc` ([../src/main_ble_utils.c:261-274](../src/main_ble_utils.c#L261-L274))
updates `dcc_address` in RAM only — no NVM save, no SRP/Loconet re-registration.
The short-name path (`modify_short_name`) does all three.

**Why it matters.** A DCC address set over BLE is lost on reboot, and the loco
won't be discoverable/addressable by that DCC number until the next cold start
that happens to re-register.

**Fix.** Mirror the short-name path:

1. Persist: `settings_save_subtree("loki/dcc")` (the export handler already
   serialises `loki/dcc`, [../src/main.c:257](../src/main.c#L257)).
2. Re-register the Loconet/DCC SRP service with the new address and rebind the
   UDP listener — factor the block in [../src/main.c:444-458](../src/main.c#L444-L458)
   into a reusable `apply_dcc_address(uint16_t)` and call it from both `main()`
   and `write_dcc`.

**Affected:** [../src/main_ble_utils.c](../src/main_ble_utils.c),
[../src/main.c](../src/main.c).
**Effort:** M. **Risk:** medium (touches SRP registration lifecycle).

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

## 1.4 `write_name` one-byte overflow

**Problem.** `write_name` ([../src/main_ble_utils.c:207-227](../src/main_ble_utils.c#L207-L227))
declares `char new_name[MAX_LEN_FULL_NAME]` (63), checks `len > sizeof(new_name)`,
then writes `new_name[len] = '\0'`. When `len == 63`, the terminator lands at
`new_name[63]` — one past the end.

**Fix.** Reserve room for the terminator:

```c
char new_name[MAX_LEN_FULL_NAME + 1];
if (len > MAX_LEN_FULL_NAME) {
    return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
}
memcpy(new_name, buf, len);
new_name[len] = '\0';
```

**Affected:** [../src/main_ble_utils.c](../src/main_ble_utils.c).
**Effort:** S. **Risk:** low.

---

## 1.5 BLE never turns off after provisioning (product decision)

**Problem.** `main()` enables BLE and starts advertising unconditionally at the
end of startup ([../src/main.c:463-485](../src/main.c#L463-L485)), even when the
loco is already commissioned. The stated design is "BLE while unprovisioned,
CoAP once joined".

**Why it matters.** Power draw, attack surface, and a second always-open control
channel after the loco is on the mesh.

**Options.**

- **A — Off when commissioned (matches stated intent).** Wrap the BLE start in
  `if (!otDatasetIsCommissioned(...))`. Provide a way back (e.g. a button or a
  CoAP/Loconet "open BLE for N minutes" command) for re-provisioning.
- **B — Keep BLE, but stop advertising after the first Thread attach** and on a
  timeout, so it's reachable for recovery but not broadcasting.
- **C — Leave as-is** and update [../INTERFACES.md](../INTERFACES.md) to drop the
  "turns off BLE" claim.

**Recommendation.** Decide A vs B as a product call. A is simplest and matches the
documented model; pair it with a deliberate re-provisioning trigger so a
commissioned loco isn't bricked from BLE access if the Thread network is lost.

**Affected:** [../src/main.c](../src/main.c).
**Effort:** M. **Risk:** medium (don't strand a loco with no control channel).
