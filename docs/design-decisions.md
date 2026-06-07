# Design Decisions

## Engine First

The engine is the critical path. Backend and UI should remain thin until order
matching, wallet locking, ticket locking, and settlement tests are trustworthy.

## Single Writer Before Concurrency

The first engine version should process commands sequentially. Backend requests
can be concurrent, but engine state transitions should be serialized for
correctness and replayability.

## Protocol Codegen Is Stretch

The TXP binary protocol and schema-driven code generator should stay out of the
MVP critical path until the exchange and benchmark story are solid.
