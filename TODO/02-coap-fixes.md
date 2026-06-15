# 02 — CoAP fixes

Covers the CoAP-side caveats from [../INTERFACES.md](../INTERFACES.md#coap-caveats--known-issues).

---

## 2.1 `/name` resource is never registered — **applied**

**Was.** `name_resource` and `name_request_handler` were defined, but
`loki_coap_init()` neither assigned `srv_context.on_name_request` nor called
`otCoapAddResource()` for it — only speed, acceleration, direction, and stop
were added, so renaming over CoAP silently no-op'd.

**Now.** `loki_coap_init()` in
[../src/loki_coap_utils.c](../src/loki_coap_utils.c) wires both halves:
`name_resource.mContext` / `name_resource.mHandler` are set, the resource is
registered with `otCoapAddResource`, and `srv_context.on_name_request` is
assigned from the init argument. `main()` passes `modify_full_name`, so a PUT
to `/name` now actually drives the rename + SRP re-registration path.

The handler itself was a minefield until [2.4](#24-name_request_handler-leaks-and-mishandles-the-payload--applied)
landed; the two fixes only make sense together.

---

## 2.2 `/acceleration` GET returns speed, not acceleration — **applied**

**Was.** The GET branch did `payload = speed_value;` — an `int` assigned to a
`const void *` — and appended `sizeof(speed_value)` bytes. The log line
literally said `"Sent direct speed response"` from inside the *acceleration*
handler. Whether the wire bytes were the actual speed depended on calling
convention and integer width; the only certainty was that `accel_order` was
never read.

**Now.** [loki_coap_utils.c acceleration_request_handler](../src/loki_coap_utils.c)
takes `&accel_order` with `sizeof(accel_order)` (1 byte, `int8_t`) and the log
line is `"Sent acceleration response: %d"` on `accel_order`. A short comment
in-line marks this as a stop-gap until [2.6](#26-unify-speed--acceleration-into-one-generic-numeric-handler)
collapses the speed and acceleration handlers into one — at which point this
whole branch and the duplicated GET-response boilerplate disappear.

**Verification.**

- [ ] `coap-client -N -m get coap://[<loco>]:5683/acceleration` returns the
      current `accel_order` byte (try `0`, `+5`, `-3`).
- [ ] Acceleration writes followed by GETs show the updated value, not
      whatever speed happens to be.

---

## 2.3 Content-format negotiation is stubbed; iterator is uninitialised — **applied**

**Was.** Two coupled defects:

- The Speed GET handler hard-coded the text/plain branch via
  `0 == OT_COAP_OPTION_CONTENT_FORMAT_TEXT_PLAIN` — a tautology that bypassed
  `getContentFormat()` entirely.
- `getContentFormat()` itself called `otCoapOptionIteratorInit(iterator, ...)`
  on an *uninitialised pointer* (`otCoapOptionIterator *iterator;`), which
  would have dereferenced garbage the first time anyone actually called it.
  It also logged `"Getting content format option" / "Iterator Init done" /
  "Option found"` on every invocation.

**Now.** [loki_coap_utils.c getContentFormat](../src/loki_coap_utils.c):

- The iterator lives on the stack (`otCoapOptionIterator iterator;`) and is
  initialised by passing its address — `otCoapOptionIteratorInit(&iterator,
  request_message)`. Init failure falls back to `text/plain` (the de-facto
  default for clients that don't set Content-Format).
- The actual option value is fetched with
  `otCoapOptionIteratorGetOptionUintValue(&iterator, &raw)` — the correct
  OT API for the uint-encoded Content-Format option. The raw `uint64_t` is
  cast to `otCoapOptionContentFormat` on the way out.
- The noisy `LOG_INF` chatter is gone; only a single `LOG_WRN` fires when
  reading the option value fails, which should be rare.

The Speed GET handler now actually calls it:

```c
if (getContentFormat(request_message) == OT_COAP_OPTION_CONTENT_FORMAT_TEXT_PLAIN) {
    /* serialise as ASCII decimal */
} else {
    /* serialise as octet-stream */
}
```

This unblocks [2.6](#26-unify-speed--acceleration-into-one-generic-numeric-handler),
which depends on a working Content-Format inspection to pick ASCII vs binary
on the PUT path.

**Verification.**

- [ ] `coap-client -N -m get coap://[<loco>]:5683/speed` without `-T text/plain`
      → returns ASCII decimal as before.
- [ ] `coap-client -N -m get -T application/octet-stream coap://[<loco>]:5683/speed`
      → returns the 1-byte octet-stream representation. Pre-fix this branch
      was unreachable.
- [ ] No more per-call `"Getting content format option"` log spam.

---

## 2.4 `name_request_handler` leaks and mishandles the payload — **applied**

**The picture.** When a CoAP `PUT /name` arrived, the handler reserved a buffer
for "however big the message says it is", wrote a NUL one byte past the end of
that buffer, asked OpenThread to dump the entire message — *including the CoAP
header* — into it, then handed all of that to the rename callback. Whether the
buffer itself leaked depended on whether the `malloc_free` typo (later
`k_malloc`/`k_free`) ever linked. The pointer the callback got was technically
valid; the *contents* and the *length* the callback was told to read were not.

**Was.** Five overlapping problems in the same ~15-line function:

1. `malloc_free` was an undefined symbol — `nm zephyr.elf` only knew `free`, and
   no NCS/Zephyr/newlib header defined the name. The `.obj` was older than the
   `.c`, so the link error was just queued for the next rebuild. (A subsequent
   user-side rename to `k_malloc`/`k_free` closed this hole.)
2. `char *buf = malloc(len); … buf[len] = '\0';` wrote one byte past the end of
   the allocation — off-by-one heap corruption.
3. `len = otMessageGetLength(message)` is the **total** message length including
   the CoAP header. The payload starts at `otMessageGetOffset()`. Both the
   allocation and the callback length argument were `total` instead of
   `total - offset`, so the callback was told the name was longer than it was
   and the header bytes were treated as name characters.
4. `otMessageRead`'s return value (the actual byte count copied) was discarded;
   short reads left the tail of the buffer uninitialised and forwarded that
   garbage as part of the name.
5. `malloc(len)` had no upper bound — a CoAP client could request an arbitrary
   allocation off the system heap with one PUT.

**Now.** The handler in [../src/loki_coap_utils.c](../src/loki_coap_utils.c)
`name_request_handler` is heap-free: it computes `avail = total - offset`,
clamps to `MAX_LEN_FULL_NAME` with a `LOG_WRN`, reads into a stack
`char buf[MAX_LEN_FULL_NAME + 1]`, honours the read return value, NUL-terminates
in-bounds, and short-circuits cleanly when the payload is empty or the callback
is `NULL`. The rename callback (`modify_full_name`) already copies into the
persistent `full_name` array before returning, so the stack lifetime is fine.

The handler now includes the generated `loki_gatt.h` directly to get
`MAX_LEN_FULL_NAME` rather than reaching for it through `main_ble_utils.h`,
which keeps the dependency precise.

**Verification.**

- [ ] `coap-client -N -m put -e "Keihan Otsu Line Type 700" coap://[<loco>]:5683/name`
      → DNS-SD long-name advertisement updates to that string on next attach.
- [ ] Empty PUT body → `LOG_WRN "Name request payload empty"`, no state change.
- [ ] Oversize PUT (e.g. 200-byte body) → `LOG_WRN "Name payload N clamped …"`,
      name set to first `MAX_LEN_FULL_NAME` bytes.
- [ ] System heap usage no longer grows per rename (was at risk of permanent
      leak whenever the linker resolved `malloc_free` to nothing at all).

**Depends on:** [2.1](#21-name-resource-is-never-registered--applied) — without
it this handler is never reached.

---

## 2.5 No CoAP resource discovery (`/.well-known/core`) — **applied**

**Was.** Clients had no way to enumerate the loco's CoAP resources from the
wire. The resource list was out-of-band only, in [INTERFACES.md](../INTERFACES.md)
and `interface/coap.yaml`. A new controller or a tool like `aiocoap-client`
had no in-band way to discover what speed/acceleration/etc. resources
existed.

**Now.** A `.well-known/core` resource is registered alongside the others in
[loki_coap_init](../src/loki_coap_utils.c). It serves the
[`LOKI_WELL_KNOWN_CORE`](../interface/generated/loki_coap.h) string with
content-format 40 (`application/link-format`) per RFC 6690.

The link-format string is **generated** by
[tools/gen_descriptors.py](../tools/gen_descriptors.py) from each resource's
entry in [interface/coap.yaml](../interface/coap.yaml). The `if` attribute
is inferred from the YAML `methods:` field — `core.p` for read+write,
`core.a` for write-only (actuators), `core.s` for read-only — and `ct=0` is
emitted for any resource with a declared `type:`. The `rt` value comes
verbatim from the YAML `rt:` field. Concretely:

```
GET coap://[<loco>]/.well-known/core
ct: 40 (application/link-format)

</speed>;rt="loki.speed";if="core.p";ct=0,
</acceleration>;rt="loki.accel";if="core.p";ct=0,
</direction>;rt="loki.direction";if="core.a";ct=0,
</stop>;rt="loki.stop";if="core.a",
</name>;rt="loki.name";if="core.a";ct=0,
</ble-recovery>;rt="loki.ble-recovery";if="core.a"
```

Because the same YAML drives both the link-format string and the
`SPEED_URI_PATH` etc. macros the actual handlers register under, the
advertised surface cannot drift from the registered surface — adding a
resource to YAML, regenerating, and registering its handler updates the
discovery output for free.

The handler is GET-only, rejects confirmable messages (consistent with the
rest of the CoAP surface here), and uses a `static const` payload so the
link-format lives in `.rodata` exactly once.

**Verification.**

- [ ] `coap-client -N -m get coap://[<loco>]:5683/.well-known/core` returns
      the link-format string above with `Content-Format: 40`.
- [ ] `aiocoap-client coap://[<loco>]:5683/.well-known/core` parses the
      response and lists the six resources.
- [ ] Adding a new resource stanza to `interface/coap.yaml`, regenerating,
      and adding its handler in `loki_coap_utils.c` updates the
      `.well-known/core` response without touching that handler.
- [ ] Log line `.well-known/core: sent N bytes of link-format` fires on each
      GET.

---

## 2.6 Unify speed & acceleration into one generic numeric handler — **Part B applied, Part A deferred**

> **Status (2026-06):** the newlib-free numeric path (Part B below) has landed,
> taking a slightly different shape from this plan — it uses `strtoul` /
> `snprintk` (provided by picolibc / minimal-libc, free of charge) rather than
> the hand-rolled `parse_decimal_signed` / `format_decimal_signed` helpers
> sketched here. The end result is the same: `CONFIG_NEWLIB_LIBC` is gone
> from [../loki_app.conf](../loki_app.conf) and the ~36 KB regression is
> reclaimed.
>
> **Part A (handler unification) is deferred — explicitly, not just unscheduled.**
> The unification's whole appeal is that `/speed` and `/acceleration` are 80%
> the same shape. The current product direction is that those two resources'
> *semantics* are likely to diverge — speed gains profile/ramp inputs,
> acceleration grows direction-aware behaviour, and so on. Unifying them now
> would either force the generic handler to sprout a switch on resource
> identity (defeating the point) or guarantee the unification is ripped out
> again when the divergence lands. Re-evaluate **only** when a third numeric
> resource arrives that genuinely shares the speed/acceleration shape (e.g.
> `/pwm`); at that point the comparison is "3 similar copies vs unify", which
> is a real argument again.
>
> The generator-side groundwork *was* landed during this evaluation and is kept
> in [../tools/gen_descriptors.py](../tools/gen_descriptors.py) /
> [../interface/generated/loki_coap.h](../interface/generated/loki_coap.h):
> a `loki_num_type_t` enum and per-resource `LOKI_RES_<X>_TYPE/_MIN/_MAX`
> macros. They cost ~15 lines in a generated header that nobody hand-edits,
> and the `_MIN`/`_MAX` halves are usable today for range checks in the
> existing per-resource handlers (e.g. the speed handler's `parsed > 255`
> could be `parsed > LOKI_RES_SPEED_MAX`). If a future maintainer wants to
> roll the codegen back, deleting the `NUMERIC_TYPES` table + the
> "Numeric-resource metadata" block in `render_coap` is sufficient.

**The picture.** Two CoAP resources — `/speed` (`uint8`) and `/acceleration`
(`int8`) — both need the same shape of behaviour: GET returns the current
value, PUT writes a new one and runs a side-effect callback. The two handlers
are 80% copy-paste and 20% subtly different, and the duplication is exactly
where the bugs in [2.2](#22-acceleration-get-returns-speed-not-acceleration)
and [2.3](#23-content-format-negotiation-is-stubbed-iterator-is-uninitialised)
took root: one path was edited correctly, the other was forgotten. Each new
numeric resource (`/pwm`, `/direction`) would add another copy.

A secondary cost (now resolved — see status box above): the speed handler used
`sscanf("%d", …)` to parse ASCII — the source comment noted that enabling
newlibc *just for sscanf* added **~36 KB of flash** (FLASH 66.0% vs 62.6%). The
unification was the natural moment to drop sscanf; in practice the swap landed
ahead of Part A as a standalone change.

This plan supersedes [2.2](#22-acceleration-get-returns-speed-not-acceleration)
(the GET-returns-speed bug evaporates when both resources share the same
response code path) and depends on
[2.3](#23-content-format-negotiation-is-stubbed-iterator-is-uninitialised) (the
generic handler relies on a working `getContentFormat`).

### Part A — Generic numeric resource model

One `static const` record per resource describes everything the handler needs:

```c
typedef enum {
    LOKI_NUM_U8,
    LOKI_NUM_I8,
    LOKI_NUM_U16_LE,
    /* extend as needed for /pwm, /direction, … */
} loki_num_type_t;

typedef struct {
    const char       *uri;          /* matches a *_URI_PATH define     */
    loki_num_type_t   type;
    void             *value_ptr;    /* &speed_value, &accel_order, …   */
    void            (*on_put)(int32_t v);  /* widened-type setter      */
    const char       *log_name;     /* for log lines                   */
    otCoapResource    coap_resource;
} loki_numeric_resource_t;

static void numeric_request_handler(void *ctx, otMessage *msg,
                                    const otMessageInfo *mi)
{
    loki_numeric_resource_t *r = ctx;

    if (otCoapMessageGetType(msg) != OT_COAP_TYPE_NON_CONFIRMABLE) {
        LOG_ERR("%s: non-NON message ignored", r->log_name);
        return;
    }
    switch (otCoapMessageGetCode(msg)) {
    case OT_COAP_CODE_GET: numeric_send_get_response(r, msg, mi); break;
    case OT_COAP_CODE_PUT: numeric_handle_put(r, msg);            break;
    default: LOG_ERR("%s: unexpected CoAP code", r->log_name);
    }
}
```

The GET path widens the resource's value to `int32_t` and renders it as text
(or binary, if the client asked for `application/octet-stream` via `Accept`).
The PUT path inspects `Content-Format`, decodes accordingly, range-checks
against the resource's type, then calls `on_put(value)`.

Registration becomes data-driven:

```c
/* Module-private — the only thing that changes when you add a resource. */
static loki_numeric_resource_t speed_resource = {
    .uri        = SPEED_URI_PATH,
    .type       = LOKI_NUM_U8,
    .value_ptr  = &speed_value,
    .log_name   = "speed",
};
static loki_numeric_resource_t accel_resource = {
    .uri        = ACC_URI_PATH,
    .type       = LOKI_NUM_I8,
    .value_ptr  = &accel_order,
    .log_name   = "accel",
};

/* In loki_coap_init(): */
register_numeric_resource(&speed_resource, on_speed_request_adapter);
register_numeric_resource(&accel_resource, on_accel_request_adapter);
```

`register_numeric_resource()` wires the `coap_resource` (`mUriPath`, `mHandler =
numeric_request_handler`, `mContext = resource`), assigns `on_put`, and calls
`otCoapAddResource`. Adding `/pwm` later is two lines of data plus one
registration call.

**Callback shape.** The existing setters (`change_speed_directly(uint8_t)`,
`speed_set_acceleration(int8_t)`) keep their narrow types — small adapters in
[../src/loki_coap_utils.c](../src/loki_coap_utils.c) bridge to the widened
`int32_t` callback. Alternatively, promote them to take `int32_t` directly and
delete the adapters; that's a cascading change worth considering once a third
resource (`/pwm`) joins.

### Part B — Decimal parse/format without newlib — **applied (different path)**

> **What actually shipped.** `sscanf("%hhu", …)` in
> [../src/loki_coap_utils.c](../src/loki_coap_utils.c) was replaced with a
> bounded `otMessageRead` + `strtoul` parse (NUL-termination + range check),
> and the matching `sprintf(payload, "%d", …)` became `snprintk(payload,
> sizeof(payload), "%u", …)`. `strtoul` and `snprintk` are both newlib-free
> (provided by minimal-libc / picolibc and Zephyr's `<zephyr/sys/printk.h>`
> respectively), so the hand-rolled helpers sketched below were not needed.
> Every other `sprintf` in [../src/main.c](../src/main.c) and
> [../src/main_ot_utils.c](../src/main_ot_utils.c) was migrated at the same
> time (to `snprintk` or `strcpy`), and `CONFIG_NEWLIB_LIBC` was commented out
> in [../loki_app.conf](../loki_app.conf) with the rationale recorded inline.
> The helpers below are kept for reference in case a future call site needs
> tighter control over the parse (e.g. embedded NUL handling, custom error
> codes) than `strtoul` provides.

Two ~25-line helpers replace `sscanf("%d")` and `sprintf("%d")`. They live
beside the handler in [../src/loki_coap_utils.c](../src/loki_coap_utils.c) as
`static` helpers; if a third consumer appears (BLE writes, shell, …), extract
to `src/util_decimal.{c,h}`.

```c
/* Parse a decimal integer from a NOT-necessarily-null-terminated buffer.
 * Accepts optional leading '+'/'-'. Tolerates trailing NUL/whitespace.
 * Returns 0 on success and writes *out; -1 on parse error, overflow, or
 * out-of-range for [min,max]. No newlib, no FP, no allocation. */
static int parse_decimal_signed(const char *buf, size_t len,
                                int32_t min, int32_t max, int32_t *out)
{
    if (len == 0) return -1;
    size_t i = 0;
    bool negative = false;
    if (buf[0] == '-') { negative = true; i = 1; }
    else if (buf[0] == '+') { i = 1; }
    if (i == len) return -1;                /* sign with no digits */

    uint32_t mag = 0;                       /* uint to handle INT32_MIN */
    for (; i < len; i++) {
        char c = buf[i];
        if (c == '\0' || c == ' ' || c == '\n' || c == '\r') break;
        if (c < '0' || c > '9') return -1;
        unsigned d = (unsigned)(c - '0');
        if (mag > (UINT32_MAX - d) / 10u) return -1;   /* overflow */
        mag = mag * 10u + d;
    }
    int32_t v;
    if (negative) {
        if (mag > 0x80000000u) return -1;
        v = (int32_t)(-(int64_t)mag);
    } else {
        if (mag > (uint32_t)INT32_MAX) return -1;
        v = (int32_t)mag;
    }
    if (v < min || v > max) return -1;
    *out = v;
    return 0;
}

/* Render a signed decimal into buf (NOT null-terminated). Returns the byte
 * count written, or 0 if buf is too small. Worst case "-2147483648" = 11. */
static size_t format_decimal_signed(int32_t value, char *buf, size_t bufsz)
{
    char tmp[12];
    bool negative = value < 0;
    uint32_t mag = negative ? (uint32_t)(-(int64_t)value) : (uint32_t)value;
    size_t n = 0;
    do { tmp[n++] = (char)('0' + (mag % 10u)); mag /= 10u; } while (mag);
    if (negative) tmp[n++] = '-';
    if (n > bufsz) return 0;
    for (size_t i = 0; i < n; i++) buf[i] = tmp[n - 1 - i];   /* reverse */
    return n;
}
```

Once `sscanf`/`sprintf` are gone from the CoAP layer, audit the rest of the
tree for other newlib pulls; if none remain, `CONFIG_NEWLIB_LIBC=n` reclaims
the ~36 KB the source comment flagged. (**Done** — see the status box above;
the audit found no other newlib callers and the symbol is commented out in
[../loki_app.conf](../loki_app.conf).)

### Side effects this fix gives us for free

- **[2.2](#22-acceleration-get-returns-speed-not-acceleration) disappears.**
  There is exactly one GET path, and it reads from `r->value_ptr` — the
  acceleration resource points it at `&accel_order`, period.
- **[2.3](#23-content-format-negotiation-is-stubbed-iterator-is-uninitialised)
  is consumed as a prerequisite.** The PUT path needs working
  `getContentFormat` to distinguish ASCII from binary; fixing the
  uninitialised iterator there is part of landing §2.6.
- **Lower-friction next resources.** `/pwm` (`uint16` LE) and a properly typed
  `/direction` (or any future numeric resource) become a struct literal plus
  one call.

### Open questions

- **Adapter vs widen.** Two-line adapters keep `change_speed_directly` /
  `speed_set_acceleration` narrow; widening their parameter to `int32_t`
  cleans up but ripples through BLE write handlers
  ([../src/main_ble_utils.c](../src/main_ble_utils.c)) and the BLE-side
  callers. Recommend adapters now, widen when `/pwm` joins.
- **Content-Format on GET.** Default to `text/plain`. Honour `Accept:
  application/octet-stream` if present? Useful for tiny clients but optional.
- **Where the parse/format helpers live.** Inside
  [../src/loki_coap_utils.c](../src/loki_coap_utils.c) for now (one consumer);
  extract to `util_decimal.{c,h}` once a second consumer appears.
- **Wire compatibility.** Both endpoints become strictly more permissive
  (accept both ASCII and binary). No existing client breaks.

**Affected.** [../src/loki_coap_utils.c](../src/loki_coap_utils.c) (most of
it), [../src/loki_coap_utils.h](../src/loki_coap_utils.h) (collapse
`speed_request_callback_t` and `accel_request_callback_t` into one widened
typedef, or keep both and adapt at registration).

**Effort:** M — about a day. The generic handler is straightforward, but the
decimal helpers benefit from a small test or hand-validated corner cases
(empty input, lone sign, `INT8_MIN`, `INT8_MAX + 1`, embedded NUL).

**Risk:** low–medium on the CoAP layer; low on the parser (pure function,
easy to unit-test off-target). Smoke-test by sending ASCII and binary PUTs to
both endpoints and reading back over GET.

**Depends on:** [2.3](#23-content-format-negotiation-is-stubbed-iterator-is-uninitialised).
**Supersedes:** [2.2](#22-acceleration-get-returns-speed-not-acceleration).
