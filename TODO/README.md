# TODO — interface mitigations & improvements

Action plans for the caveats documented in [../INTERFACES.md](../INTERFACES.md).
Each plan states the problem, its impact, a proposed fix (with code sketch where
useful), affected files, and a rough effort/risk estimate.

| # | Plan | Area | Severity |
|---|---|---|---|
| 01 | [BLE / GATT fixes](01-ble-gatt-fixes.md) | BLE | mixed |
| 02 | [CoAP fixes](02-coap-fixes.md) | CoAP | mixed |
| 03 | [DNS-SD / SRP discovery](03-dns-sd-discovery.md) | Discovery | medium |
| 04 | [Machine-readable descriptors — placement plan](04-machine-readable-descriptors.md) | Tooling / docs | proposal |

## Suggested order

1. **Correctness bugs first** (no behaviour change expected, but they silently
   return/route the wrong thing): `/acceleration` GET, `bt_notify_speed` attr
   index, `/name` not registered — Plans 01 & 02.
2. **Robustness / leaks**: `write_name` overflow, `name_request_handler` leak,
   content-format iterator deref — Plans 01 & 02.
3. **Behavioural decisions** (need a product call): turn BLE off after
   provisioning; persist DCC over BLE — Plan 01.
4. **Discovery cleanup**: conformant DNS-SD types + TXT records — Plan 03.
5. **Self-describing contract**: descriptors + `/.well-known/core` — Plan 04.

> These are plans, not applied changes. Severity is a guess; confirm against your
> own priorities before scheduling.
