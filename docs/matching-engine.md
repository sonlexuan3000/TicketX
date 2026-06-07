# Matching Engine

The matching engine target is a C++20 price-time-priority order book.

## MVP Order Types

- Limit buy
- Limit sell
- Market buy
- Market sell
- Cancel order

## Core Rules

- Highest bid is the best bid.
- Lowest ask is the best ask.
- Same-price orders execute FIFO.
- Money is represented as integer minor units, never floating point.
- Matching should be deterministic so event replay can reconstruct state.
