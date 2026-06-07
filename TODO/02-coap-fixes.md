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
