# Codebase Bug Report & Implementation Plan

> Analysis performed on `master` branch (commit `859e539`) — NCS 3.2.4 / Zephyr target.

---

## Overview

33 issues identified across 6 categories. Each issue is ranked by **Importance** (how likely it is to cause a real failure) and **Complexity** (effort to fix).

| Severity | Count |
|----------|-------|
| 🔴 CRITICAL (crashes / data corruption) | 7 |
| 🟠 HIGH (wrong behavior / leaks) | 10 |
| 🟡 MEDIUM (API misuse / maintainability) | 10 |
| 🟢 LOW (style / minor) | 6 |

---

## 🔴 CRITICAL — Fix Immediately

### C1 · Bitwise vs. Logical AND — direction display is always wrong

| | |
|---|---|
| **Files** | `src/main.c` lines 142-149, 348-355 |
| **Importance** | 🔴 10/10 — direction indicator is always broken |
| **Complexity** | 🟢 Trivial (two-character fix per site) |

**Bug:** `direction_pattern && 1` uses **logical AND** (`&&`), which evaluates to `true` whenever `direction_pattern != 0`. The second branch `direction_pattern && 2` is therefore dead code whenever direction ≥ 1.

**Correct code:**
```c
if (direction_pattern & 1) {        // bitwise AND
    sprintf(buffer, "%s", " >");
} else if (direction_pattern & 2) { // bitwise AND
    sprintf(buffer, "%s", "< ");
}
```

**Fix plan:**
1. In `src/main.c`, replace `&&` with `&` at both sites (lines 142/144 and 348/350).

---

### C2 · Pointer-as-integer in CoAP acceleration GET handler

| | |
|---|---|
| **File** | `src/loki_coap_utils.c` line 379 |
| **Importance** | 🔴 10/10 — immediate crash / garbage data sent to peer |
| **Complexity** | 🟢 Trivial (one-character fix) |

**Bug:**
```c
payload = speed_value;          // speed_value is uint8_t; payload is const void*
payload_size = sizeof(speed_value);
error = otMessageAppend(response, payload, payload_size);
```
`payload` receives the *integer value* of `speed_value` (e.g. `42`), then is dereferenced as a pointer → crash or garbage.

**Fix:**
```c
payload = &speed_value;
```

---

### C3 · `&payload` double-pointer in CoAP speed GET handler

| | |
|---|---|
| **File** | `src/loki_coap_utils.c` line 277 |
| **Importance** | 🔴 9/10 — sends address of stack pointer instead of payload |
| **Complexity** | 🟢 Trivial |

**Bug:**
```c
error = otMessageAppend(response, &payload, payload_size);
```
`payload` is already `char payload[40]`; `&payload` yields `char (*)[40]` — the array address (same numeric value, but wrong size semantics). In the octet-stream branch `payload_size = sizeof(speed_value)` = 1 byte, but the `&` is still a conceptual error and wrong for the text branch where `payload_size = sprintf(...)`.

**Fix:**
```c
error = otMessageAppend(response, payload, payload_size);
```

---

### C4 · Uninitialized `otCoapOptionIterator` pointer

| | |
|---|---|
| **File** | `src/loki_coap_utils.c` lines 313-317 |
| **Importance** | 🔴 9/10 — NULL/garbage pointer → crash |
| **Complexity** | 🟢 Easy |

**Bug:**
```c
otCoapOptionIterator *iterator;            // uninitialized pointer
otCoapOptionIteratorInit(iterator, request_message);   // writes through wild pointer
```

**Fix:** Allocate on the stack:
```c
otCoapOptionIterator iterator;
otCoapOptionIteratorInit(&iterator, request_message);
// then: otCoapOptionIteratorGetFirstOptionMatching(&iterator, ...)
// and:  otCoapOptionIteratorGetOptionValue(&iterator, ...)
```

---

### C5 · Unconditional `memcpy` overwrites bounds-checked copy in `init_srp()`

| | |
|---|---|
| **File** | `src/main_ot_utils.c` line 389 |
| **Importance** | 🔴 8/10 — buffer overflow if `ble_name` exceeds SRP buffer size |
| **Complexity** | 🟢 Trivial (delete one line) |

**Bug:**
```c
if (strlen(ble_name) < size) {
    memcpy(host_name, ble_name, strlen(ble_name) + 1);  // safe path
} else {
    memcpy(host_name, ble_name, size - 1);               // truncated path
    host_name[size - 1] = '\0';
}
memcpy(host_name, ble_name, strlen(ble_name) + 1);       // ← ALWAYS runs, ignores check
```

**Fix:** Delete line 389 (`memcpy(host_name, ble_name, strlen(ble_name) + 1);`).

---

### C6 · Stray `#endif` breaks build when `CONFIG_NVS` and `CONFIG_ZMS` are both disabled

| | |
|---|---|
| **File** | `src/main.c` line 62 |
| **Importance** | 🔴 8/10 — compile error in some configurations |
| **Complexity** | 🟢 Trivial |

**Bug:**
```c
#ifdef CONFIG_NVS
#include <zephyr/fs/nvs.h>
#elif defined(CONFIG_ZMS)
#include <zephyr/fs/zms.h>
#else
#error "Either CONFIG_NVS or CONFIG_ZMS must be enabled"
#endif
#include <zephyr/storage/flash_map.h>
#endif          // ← stray #endif — no matching #if
```

**Fix:** Remove the stray `#endif` at line 62, or restructure the preprocessor block to include `flash_map.h` only when NVS/ZMS is enabled.

---

### C7 · `name_request_handler` never reads the message, then leaks `malloc`'d buffer

| | |
|---|---|
| **File** | `src/loki_coap_utils.c` lines 474-483 |
| **Importance** | 🔴 7/10 — handler is broken + memory leak |
| **Complexity** | 🟡 Moderate |

**Bug:**
```c
uint16_t len = otMessageGetLength(message);
uint16_t offset = otMessageGetOffset(message);
char *buf = malloc(len);                     // allocated
// otMessageRead() is NEVER called — buf is uninitialized garbage
srv_context.on_name_request(buf, len);       // callback gets garbage
// buf is NEVER freed
```

**Fix:**
```c
uint16_t len = otMessageGetLength(message) - otMessageGetOffset(message);
if (len == 0 || len > MAX_LEN_FULL_NAME) { return; }
char *buf = malloc(len + 1);
if (!buf) { return; }
otMessageRead(message, otMessageGetOffset(message), buf, len);
buf[len] = '\0';
srv_context.on_name_request(buf, len);
free(buf);
```

---

## 🟠 HIGH — Fix Soon

### H1 · Uninitialized return value in `updateBleShortName()`

| | |
|---|---|
| **File** | `src/main_ble_utils.c` lines 425-442 |
| **Importance** | 🟠 7/10 — caller `modify_short_name()` checks return → undefined behavior |
| **Complexity** | 🟢 Trivial |

**Bug:** `int err;` is declared but never assigned. Function returns uninitialized `err`.

**Fix:** Change to `return 0;` or assign `err` from a real operation.

---

### H2 · `sendOtUdpReply()` — wrong buffer type + uninitialized `port`

| | |
|---|---|
| **File** | `src/main_ot_utils.c` lines 556-573 |
| **Importance** | 🟠 7/10 — crashes or logs garbage |
| **Complexity** | 🟢 Easy |

**Bugs (two):**
```c
char *buf[OT_IP6_ADDRESS_STRING_SIZE];   // array of POINTERS, not chars
u_int16_t port;                          // uninitialized, printed on line 563
```

**Fix:**
```c
char buf[OT_IP6_ADDRESS_STRING_SIZE];    // char array
uint16_t port = origMsgInfo->mPeerPort;  // initialize from message info
```

---

### H3 · `speed_input_value` missing null terminator before `sscanf`

| | |
|---|---|
| **File** | `src/loki_coap_utils.c` lines 198, 225 |
| **Importance** | 🟠 7/10 — `sscanf` reads past buffer if payload is exactly 4 bytes |
| **Complexity** | 🟢 Easy |

**Bug:**
```c
static char speed_input_value[4];  // 4 bytes
otMessageRead(..., &speed_input_value, 4);
sscanf(speed_input_value, "%d", &value);   // no null terminator guarantee
```

**Fix:** Read at most 3 bytes and null-terminate:
```c
static char speed_input_value[4];
int read_len = otMessageRead(..., speed_input_value, sizeof(speed_input_value) - 1);
speed_input_value[read_len] = '\0';
```

---

### H4 · `sscanf` target type mismatch — `%d` into `uint8_t`

| | |
|---|---|
| **File** | `src/loki_coap_utils.c` line 225 |
| **Importance** | 🟠 6/10 — stack corruption on 32-bit platforms |
| **Complexity** | 🟢 Trivial |

**Bug:** `sscanf(speed_input_value, "%d", &value)` where `value` is `uint8_t`. `%d` writes an `int` (4 bytes) into a 1-byte variable → stack smash.

**Fix:** Use an intermediate `int`:
```c
int parsed;
sscanf(speed_input_value, "%d", &parsed);
value = (uint8_t)parsed;
```

---

### H5 · `malloc` never freed — DCC string in `main()`

| | |
|---|---|
| **File** | `src/main.c` line 625 |
| **Importance** | 🟠 5/10 — memory leak (small, once) |
| **Complexity** | 🟢 Trivial |

**Bug:**
```c
char *dcc_string = malloc(15);   // never freed
```

**Fix:** Use a stack buffer since `main()` runs once and the string is short:
```c
static char dcc_string[16];
snprintf(dcc_string, sizeof(dcc_string), "%d", dcc_address);
```

---

### H6 · `K_TIMER_DEFINE` + separate `struct k_timer` declaration

| | |
|---|---|
| **File** | `src/main_loki.c` lines 43-44 |
| **Importance** | 🟠 5/10 — linker may complain or shadow the timer |
| **Complexity** | 🟢 Trivial |

**Bug:**
```c
struct k_timer my_timer;                           // declares variable
K_TIMER_DEFINE(my_timer, re_apply_acceleration, NULL);  // also defines it
```
`K_TIMER_DEFINE` already creates and initializes `my_timer` at file scope. The extra `struct k_timer my_timer;` either shadows it or causes a redefinition error depending on compiler.

**Fix:** Remove `struct k_timer my_timer;` on line 43.

---

### H7 · `otLinkGetFactoryAssignedIeeeEui64` return value never checked

| | |
|---|---|
| **File** | `src/main_ot_utils.c` lines 326-332 |
| **Importance** | 🟠 5/10 — dead error-check code gives false confidence |
| **Complexity** | 🟢 Easy |

**Bug:**
```c
otError error = OT_ERROR_NONE;
otLinkGetFactoryAssignedIeeeEui64(p_instance, &eui64);  // void function
if (error != OT_ERROR_NONE) {   // error is always OT_ERROR_NONE
```

**Fix:** `otLinkGetFactoryAssignedIeeeEui64()` is `void` in OpenThread. Remove the dead error check:
```c
otLinkGetFactoryAssignedIeeeEui64(p_instance, &eui64);
// (no error return to check)
```

---

### H8 · CMakeLists glob missing wildcard for motors

| | |
|---|---|
| **File** | `CMakeLists.txt` line 13 |
| **Importance** | 🟠 5/10 — `src/motors/*.c` files not compiled |
| **Complexity** | 🟢 Trivial |

**Bug:**
```cmake
FILE(GLOB app_sources src/*.c src/displays/*.c src/motors/.c)
#                                                      ^ missing *
```

**Fix:**
```cmake
FILE(GLOB app_sources src/*.c src/displays/*.c src/motors/*.c)
```

> **Note:** Since the new driver-model motor files live under `drivers/motor/` (and are globbed separately on line 14), this may only affect the old `src/motors/` legacy files. But fixing the glob is correct regardless.

---

### H9 · `de_register_service` passes stack copy instead of pointer

| | |
|---|---|
| **File** | `src/main.c` lines 482-491 |
| **Importance** | 🟠 5/10 — service de-registration uses dangling copy |
| **Complexity** | 🟢 Easy |

**Bug:**
```c
void *de_register_service(otSrpClientService service) {  // passed BY VALUE (large struct)
    otSrpClientRemoveService(p_instance, &service);       // &service points to stack copy
```
After the function returns, the pointer OpenThread may have stored is invalid.

**Fix:** Pass by pointer:
```c
void *de_register_service(otSrpClientService *service) {
    otSrpClientRemoveService(p_instance, service);
```

---

### H10 · Race conditions on shared global variables

| | |
|---|---|
| **Files** | `src/main_loki.c`, `src/main_ble_utils.c`, `src/loki_coap_utils.c` |
| **Importance** | 🟠 6/10 — data races between BLE GATT callbacks, CoAP handlers, timer callback |
| **Complexity** | 🔴 High |

**Bug:** `speed_value`, `accel_order`, `direction_pattern` are read/written from:
- Timer expiry callback (`re_apply_acceleration`)
- BLE GATT write handlers (Bluetooth stack context)
- CoAP request handlers (OpenThread context)
- Main thread

No locks or atomics protect these variables.

**Fix plan:**
1. Wrap all reads/writes of motor-state globals in a `k_spinlock` or use `atomic_t`.
2. Alternatively, funnel all mutations through a single work-queue to serialize access.

---

## 🟡 MEDIUM — Should Fix

### M1 · `read_direction` returns `sizeof(speed_value)` instead of `sizeof(direction_pattern)`

| | |
|---|---|
| **File** | `src/main_ble_utils.c` line 148 |
| **Complexity** | 🟢 Trivial |

**Bug:** Copy/paste error — should be `sizeof(direction_pattern)`.

---

### M2 · VLA from untrusted BLE input in `write_credential`

| | |
|---|---|
| **File** | `src/main_ble_utils.c` line 282 |
| **Complexity** | 🟡 Easy |

**Bug:** `char cred[len+1];` with `len` from BLE write — could overflow stack.
**Fix:** Add max-length check or use a fixed buffer.

---

### M3 · `write_name` buffer overflow — `new_name[MAX_LEN_FULL_NAME]` but `new_name[len] = '\0'` with `len == MAX_LEN_FULL_NAME`

| | |
|---|---|
| **File** | `src/main_ble_utils.c` line 208 |
| **Complexity** | 🟢 Trivial |

**Bug:** Array is `char new_name[MAX_LEN_FULL_NAME]` (63 bytes). Check is `if (len > sizeof(new_name))` which allows `len == 63`, then `new_name[63] = '\0'` writes one past end.
**Fix:** Change to `char new_name[MAX_LEN_FULL_NAME + 1]` or check `len >= sizeof(new_name)`.

---

### M4 · NULL-pointer checks missing before function-pointer calls

| | |
|---|---|
| **File** | `src/loki_coap_utils.c` lines 228, 416, 462, 482 |
| **Complexity** | 🟢 Easy |

**Fix:** Add `if (srv_context.on_speed_request)` etc. before each callback invocation.

---

### M5 · `name_resource` never added to CoAP server

| | |
|---|---|
| **File** | `src/loki_coap_utils.c` — `loki_coap_init()` |
| **Complexity** | 🟢 Trivial |

**Bug:** `name_resource` handler is set up (line 545-546) but `otCoapAddResource(srv_context.ot, &name_resource)` is never called.
**Fix:** Add the resource after the stop resource.

---

### M6 · `on_name_request` callback never stored

| | |
|---|---|
| **File** | `src/loki_coap_utils.c` line 558 |
| **Complexity** | 🟢 Trivial |

**Bug:** `srv_context.on_name_request = on_name_request;` is missing — the parameter is accepted but never stored.
**Fix:** Add assignment alongside the other callback assignments (around line 554-558).

---

### M7 · Mutex unlock called without matching lock in `init_srp()`

| | |
|---|---|
| **File** | `src/main_ot_utils.c` lines 394, 402 |
| **Complexity** | 🟡 Moderate |

**Bug:** The `k_mutex_lock` at line 356 is commented out, but `k_mutex_unlock` calls at lines 394 and 402 are still active. Unlocking a mutex you don't hold is undefined behavior in Zephyr.

Also, `srp_client_mutex` is never initialized with `k_mutex_init()`.

**Fix:** Either re-enable the lock consistently, or remove all lock/unlock calls.

---

### M8 · `settings_save_subtree("loki/init")` vs. handler naming mismatch

| | |
|---|---|
| **File** | `src/main.c` lines 468-475 |
| **Complexity** | 🟡 Moderate |

**Bug:** The settings handler is registered with `.name = DEFAULT_NAME_PREFIX` (= `"TREN"`), but code saves `"loki/init"` and loads `"loki/init"`. The Zephyr settings subsystem uses the handler name as key prefix. If handler name is `"TREN"`, then keys are `"TREN/shortname"`, `"TREN/longname"`, etc. — not `"loki/shortname"`.

**Fix:** Either change `.name = "loki"` or change all key references to use `DEFAULT_NAME_PREFIX`.

---

### M9 · `_ssize_t` non-standard type

| | |
|---|---|
| **File** | `src/main_ble_utils.c` lines 189, 196, 218, 226 |
| **Complexity** | 🟢 Trivial |

**Fix:** Replace `_ssize_t` with `ssize_t`.

---

### M10 · `register_service()` ignores `port` parameter

| | |
|---|---|
| **File** | `src/main_ot_utils.c` line 510 |
| **Complexity** | 🟢 Trivial |

**Bug:**
```c
otSrpClientBuffersServiceEntry *register_service(..., int port) {
    // ...
    entry->mService.mPort = OT_DEFAULT_COAP_PORT;   // ignores port argument!
```

**Fix:** `entry->mService.mPort = port;`

---

## 🟢 LOW — Nice to Fix

### L1 · `u_int8_t` / `u_int16_t` / `u_int32_t` non-portable types
- **Files:** `src/main_loki.c` line 132, `src/main_ot_utils.c` line 559
- **Fix:** Use `uint8_t` / `uint16_t` / `uint32_t`.

### L2 · Duplicate `#include` directives
- **File:** `src/main.c` — `<zephyr/kernel.h>`, `<zephyr/settings/settings.h>`, `<zephyr/device.h>`, `<stdio.h>` included multiple times.

### L3 · `LOG_INF` format specifier `%c` for numeric values
- **Files:** `src/loki_coap_utils.c` lines 414, 445 — use `%d` for `int8_t`/`uint8_t`.

### L4 · Unused variable `messageSettings` in `sendOtUdpReply`
- **File:** `src/main_ot_utils.c` line 565

### L5 · `static int ret;` in `on_thread_state_changed` — static without reason
- **File:** `src/main_ot_utils.c` line 237

### L6 · `ble_name[strlen(ble_name)] = '\0';` is a no-op
- **File:** `src/main.c` line 338

---

## Implementation Order (Recommended)

Group fixes into PRs by risk and logical grouping:

### PR 1 — Critical one-liner fixes (C1, C2, C3, C5, C6, H1, H6, H8)
**Estimated impact:** Fixes crashes, broken display logic, compile errors.
**Complexity:** All trivial — ~30 minutes total.

1. Fix `&&` → `&` in `main.c` (C1)
2. Fix `payload = speed_value` → `&speed_value` in `loki_coap_utils.c` (C2)
3. Fix `&payload` → `payload` in `loki_coap_utils.c` (C3)
4. Delete unconditional `memcpy` in `main_ot_utils.c` (C5)
5. Remove stray `#endif` in `main.c` (C6)
6. Fix uninitialized `err` return in `main_ble_utils.c` (H1)
7. Remove duplicate `struct k_timer` declaration (H6)
8. Fix CMake glob (H8)

### PR 2 — CoAP handler fixes (C4, C7, H3, H4, M4, M5, M6)
**Estimated impact:** Fixes all CoAP request handling bugs.
**Complexity:** Moderate — ~1-2 hours.

1. Fix `otCoapOptionIterator` stack allocation (C4)
2. Fix `name_request_handler` — add `otMessageRead` + free (C7)
3. Fix `speed_input_value` null termination (H3)
4. Fix `sscanf` type mismatch (H4)
5. Add NULL checks on callback pointers (M4)
6. Add `name_resource` to CoAP server (M5)
7. Store `on_name_request` callback (M6)

### PR 3 — OpenThread / SRP fixes (H2, H5, H7, H9, M7, M8, M10)
**Estimated impact:** Fixes SRP registration, UDP reply, settings.
**Complexity:** Moderate — ~1-2 hours.

1. Fix `sendOtUdpReply` buffer type + uninitialized port (H2)
2. Replace `malloc` with stack buffer for DCC string (H5)
3. Remove dead EUI64 error check (H7)
4. Fix `de_register_service` pass-by-value (H9)
5. Fix/remove orphaned mutex calls in `init_srp` (M7)
6. Fix settings handler name mismatch (M8)
7. Fix `register_service` port parameter (M10)

### PR 4 — BLE safety & type fixes (M1, M2, M3, M9, L1-L6)
**Estimated impact:** Hardens BLE handlers, cleans types.
**Complexity:** Easy — ~1 hour.

1. Fix `read_direction` sizeof (M1)
2. Add VLA bounds check in `write_credential` (M2)
3. Fix `write_name` off-by-one (M3)
4. Replace `_ssize_t` with `ssize_t` (M9)
5. Fix non-portable types (L1)
6. Clean up duplicate includes, format specifiers, unused vars (L2-L6)

### PR 5 — Concurrency hardening (H10)
**Estimated impact:** Eliminates data races.
**Complexity:** High — ~2-4 hours. Requires careful design choice (spinlock vs. work-queue serialization).

1. Identify all access points for shared motor-state globals
2. Choose synchronization strategy
3. Implement and test

---

## Questions for Maintainer

1. **Settings handler name:** Is the settings key prefix intended to be `"TREN"` (from `DEFAULT_NAME_PREFIX`) or `"loki"`? The code uses both inconsistently (M8).

2. **`src/motors/` legacy drivers:** The old motor files (`motorTB67H453.c`, `motorTB67driver.c`, `motor_dummy.c`) under `src/motors/` appear to be superseded by the new Zephyr driver-model implementations in `drivers/motor/`. Should the legacy files be removed, or are both paths still needed?

3. **Concurrency strategy (H10):** Would you prefer a spinlock-based approach (protect individual variable accesses) or funneling all motor state changes through a single work-queue thread?

4. **`getContentFormat()` function:** This function in `loki_coap_utils.c` is defined but intentionally disabled (`if (/* FIXME */ 0 == ...)`). Should it be fixed and enabled, or should the CoAP server always respond with octet-stream?
