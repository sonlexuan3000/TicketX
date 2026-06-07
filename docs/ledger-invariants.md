# Ledger Invariants

These invariants are the main reason TicketX is interesting as a systems project.

## Wallet

- No user balance may become negative.
- Funds locked for open buy orders are unavailable for withdrawal or new bids.
- Cancelling an open buy order unlocks its reserved funds.

## Ticket

- A user may hold at most one active ticket per event.
- A ticket has exactly one active owner.
- A ticket locked for sale cannot be sold twice.
- Cancelling an open sell order unlocks the ticket.
- A successful trade revokes the old credential and issues a new credential.
