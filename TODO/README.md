# TODO — interface mitigations & improvements

Action plans for the caveats documented in [../INTERFACES.md](../INTERFACES.md).
Each plan states the problem, its impact, a proposed fix (with code sketch where
useful), affected files, and a rough effort/risk estimate.

| # | Plan | Area | Severity |
|---|---|---|---|
| 01 | [BLE / GATT fixes](01-ble-gatt-fixes.md) | BLE | mixed |
| 02 | [CoAP fixes](02-coap-fixes.md) | CoAP | mixed |
| 03 | [DNS-SD / SRP discovery — four-goal redesign](03-dns-sd-discovery.md) | Discovery | medium (breaking) |
| 04 | [Machine-readable descriptors — placement plan](04-machine-readable-descriptors.md) | Tooling / docs | proposal |

## Suggested order

1. **Correctness bugs first** (no behaviour change expected, but they silently
   return/route the wrong thing): `/acceleration` GET, `bt_notify_speed` attr
   index, `/name` not registered — Plans 01 & 02.
2. **Robustness / leaks**: `write_name` overflow, `name_request_handler` leak,
   content-format iterator deref — Plans 01 & 02.
3. **Behavioural decisions** (need a product call): turn BLE off after
   provisioning; persist DCC over BLE — Plan 01.
4. **Discovery redesign**: four-goal DNS-SD model — one host AAAA + `_loki._udp` for
   enumeration + `_dcc._udp` for DCC# point lookup + `_nfc._udp` for trackside tag
   point lookup, plus TXT metadata. Plan 03.
5. **Self-describing contract**: descriptors + `/.well-known/core` — Plan 04.

> These are plans, not applied changes. Severity is a guess; confirm against your
> own priorities before scheduling.
