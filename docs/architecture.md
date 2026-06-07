# Architecture

TicketX keeps the exchange core separate from the web surfaces.

```text
frontend -> backend -> engine
                     -> wallet ledger
                     -> ticket ledger
                     -> event log
```

## Boundaries

- The engine owns order matching and deterministic state transitions.
- The backend translates HTTP/JSON requests into engine commands.
- The frontend is a demo/control surface for wallet, ticket, and exchange flows.
- Persistence starts simple and can move from in-memory snapshots to SQLite or
  PostgreSQL after the core invariants are stable.

## MVP Bias

The first working version should prioritize correctness over distribution:
single process, single writer, fake wallet, category-based tickets, quantity one.
