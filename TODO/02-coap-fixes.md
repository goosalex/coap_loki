# 02 — CoAP fixes

Covers the CoAP-side caveats from [../INTERFACES.md](../INTERFACES.md#coap-caveats--known-issues).

---

## 2.1 `/name` resource is never registered

**Problem.** `name_resource` and `name_request_handler` exist, but
`loki_coap_init()` neither assigns `srv_context.on_name_request` nor calls
`otCoapAddResource()` for it — only speed, acceleration, direction and stop are
added ([../src/loki_coap_utils.c:549-557](../src/loki_coap_utils.c#L549-L557)).
So renaming over CoAP is a dead path.

**Fix.** In `loki_coap_init()`:

```c
name_resource.mContext = srv_context.ot;
name_resource.mHandler = name_request_handler;
...
otCoapAddResource(srv_context.ot, &name_resource);
...
srv_context.on_name_request = on_name_request;   /* currently missing */
```

Then fix the handler (see 2.4) so it actually reads the payload and calls the
callback (`modify_full_name`).

**Affected:** [../src/loki_coap_utils.c](../src/loki_coap_utils.c).
**Effort:** S. **Risk:** low.

---

## 2.2 `/acceleration` GET returns speed, not acceleration

**Problem.** The GET branch sets `payload = speed_value;` (an `int` assigned to a
`const void *`) and appends it
([../src/loki_coap_utils.c:379-393](../src/loki_coap_utils.c#L379-L393)). The log
line even says "Sent direct speed response". So GET `/acceleration` returns the
speed value via a bogus pointer.

**Fix.** Return the acceleration order by address, with its real size:

```c
const void *payload = &accel_order;          /* int8_t */
uint16_t payload_size = sizeof(accel_order);
...
LOG_INF("Sent acceleration response: %d", accel_order);
```

Consider a shared helper for the GET response boilerplate (token, payload marker,
append, send) — it is duplicated across the speed and acceleration handlers.

**Affected:** [../src/loki_coap_utils.c](../src/loki_coap_utils.c).
**Effort:** S. **Risk:** low.

---

## 2.3 Content-format negotiation is stubbed; iterator is uninitialised

**Problem.** The Speed GET path hard-codes the text/plain branch via
`0 == OT_COAP_OPTION_CONTENT_FORMAT_TEXT_PLAIN`
([../src/loki_coap_utils.c:255](../src/loki_coap_utils.c#L255)), bypassing
`getContentFormat()`. `getContentFormat()` itself calls
`otCoapOptionIteratorInit(iterator, ...)` on an **uninitialised pointer**
([../src/loki_coap_utils.c:310-333](../src/loki_coap_utils.c#L310-L333)) — it
would dereference garbage if ever called.

**Fix.**

1. Give the iterator storage and pass its address:
   ```c
   otCoapOptionIterator iterator;
   otCoapOptionIteratorInit(&iterator, request_message);
   ```
2. Re-enable real negotiation in the speed handler:
   ```c
   if (getContentFormat(request_message) == OT_COAP_OPTION_CONTENT_FORMAT_TEXT_PLAIN) { ... }
   ```
3. Drop the noisy per-call `LOG_INF` lines inside `getContentFormat`.

**Affected:** [../src/loki_coap_utils.c](../src/loki_coap_utils.c).
**Effort:** S–M. **Risk:** low (currently dead code; safe to fix or delete).

---

## 2.4 `name_request_handler` leaks and ignores the payload

**Problem.** `name_request_handler`
([../src/loki_coap_utils.c:465-483](../src/loki_coap_utils.c#L465-L483))
`malloc`s a buffer, never reads the message into it, never frees it, and passes
the uninitialised buffer to the callback.

**Fix.** Read the payload into the buffer, call the callback, free it (or use a
small stack buffer bounded by `MAX_LEN_FULL_NAME`):

```c
uint16_t off = otMessageGetOffset(message);
uint16_t len = otMessageGetLength(message) - off;
if (len > MAX_LEN_FULL_NAME) len = MAX_LEN_FULL_NAME;

char buf[MAX_LEN_FULL_NAME + 1];
otMessageRead(message, off, buf, len);
buf[len] = '\0';

if (srv_context.on_name_request) {
    srv_context.on_name_request(buf, len);
}
```

**Affected:** [../src/loki_coap_utils.c](../src/loki_coap_utils.c).
**Effort:** S. **Risk:** low. **Depends on:** 2.1.

---

## 2.5 No CoAP resource discovery (`/.well-known/core`)

**Problem.** Clients can't enumerate resources over CoAP; the resource list is
out-of-band only.

**Fix.** Register a `.well-known/core` resource returning
`application/link-format` (content-format 40). See
[04 — machine-readable descriptors](04-machine-readable-descriptors.md) for the
recommended approach (generate the link-format string from `interface/coap.yaml`
so it can't drift from the actual handlers).

**Affected:** [../src/loki_coap_utils.c](../src/loki_coap_utils.c).
**Effort:** M. **Risk:** low.

---

## 2.6 Unify speed & acceleration into one generic numeric handler

**The picture.** Two CoAP resources — `/speed` (`uint8`) and `/acceleration`
(`int8`) — both need the same shape of behaviour: GET returns the current
value, PUT writes a new one and runs a side-effect callback. The two handlers
are 80% copy-paste and 20% subtly different, and the duplication is exactly
where the bugs in [2.2](#22-acceleration-get-returns-speed-not-acceleration)
and [2.3](#23-content-format-negotiation-is-stubbed-iterator-is-uninitialised)
took root: one path was edited correctly, the other was forgotten. Each new
numeric resource (`/pwm`, `/direction`) would add another copy.

A secondary cost: the speed handler uses `sscanf("%d", …)` to parse ASCII —
the source comment notes that enabling newlibc *just for sscanf* adds **~36 KB
of flash** (FLASH 66.0% vs 62.6%). The unification is also the natural moment
to drop sscanf for a tiny hand-rolled decimal parser.

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

### Part B — Decimal parse/format without newlib

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
the ~36 KB the source comment flagged.

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
