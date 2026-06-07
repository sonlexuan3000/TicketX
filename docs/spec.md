# TicketX MVP Spec

TicketX is a wallet-based ticket exchange for event tickets. The MVP uses
event-level ticket categories instead of assigned seats. A tradable ticket is
identified by `ticket_id`, belongs to one `event_id`, and has a category such as
`standard`, `vip`, or `balcony`.

## Ticket Model

An organizer creates an event and issues a fixed number of primary-sale tickets
per category. Each ticket has one active owner at a time. The system controls
ticket ownership, so users do not manually exchange QR codes outside the
platform.

For the MVP, each order trades exactly one ticket. Multi-quantity orders and
assigned seats are out of scope.

## Money Model

Money is stored as an integer amount in Vietnamese dong:

```cpp
using Money = std::int64_t; // amount_vnd
```

Floating-point values must not be used for balances, prices, fees, or settlement.

## Ownership Rule

A user may hold at most one active ticket for the same event. This applies across
primary purchases and secondary-market trades. If a user already owns an active
ticket for an event, any attempt to buy another active ticket for that event must
be rejected.

## Order Types

The MVP supports:

- Limit buy
- Limit sell
- Market buy
- Market sell
- Cancel order

Limit orders may rest in the order book. Market orders execute immediately
against the best available opposite-side order and do not rest.

## Buy-Side Locking

When a buyer places a limit buy order, the wallet ledger reserves the full limit
price before the order enters the book.

Example: if a user places a buy limit at `1,200,000 VND`, then:

```text
available_balance -= 1,200,000
locked_balance += 1,200,000
```

If the order later trades at a lower price, the unused locked amount is refunded
to the buyer. If the order is cancelled before execution, the locked amount is
fully unlocked.

Market buy orders must have enough available balance to pay the current best ask
before execution.

## Sell-Side Locking

When a seller places a limit sell order, the ticket ledger locks the ticket before
the order enters the book. A locked ticket cannot be used for another sell order,
transferred manually, or sold twice.

If the sell order is cancelled, the ticket is unlocked and remains owned by the
seller. If the sell order trades, ownership transfers to the buyer, the old
credential is revoked, and a new credential version is issued to the buyer.

Market sell orders require the seller to own an unlocked active ticket for the
event and category before execution.

## Core Invariants

- No user balance may become negative.
- A ticket cannot be sold twice.
- A ticket has exactly one active owner.
- A user has at most one active ticket per event.
- Cancelling an open buy order unlocks money.
- Cancelling an open sell order unlocks the ticket.
- A completed trade atomically settles wallet balances and ticket ownership.
