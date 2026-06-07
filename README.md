# TicketX

TicketX is a wallet-based ticket exchange project centered on a C++20 matching
engine. The MVP target is a primary ticket sale, a secondary order book, wallet
locking, ticket ownership transfer, invariant tests, and latency benchmarks.

## Current Shape

- `engine/`: C++ matching engine, wallet ledger, ticket ledger, tests, benchmarks.
- `backend/`: C++ API layer placeholder that will call the engine.
- `frontend/`: React + Vite + TypeScript demo app.
- `docs/`: architecture notes, invariants, matching model, benchmark notes.

## First Commands

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
cd frontend && npm run dev
```

The current code is intentionally skeletal: it sets up the project boundaries and
dependency workflow before implementing the exchange logic.
