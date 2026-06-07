#pragma once

#include "ticketx/types.hpp"

namespace ticketx {

enum class TicketStatus {
  Owned,
  LockedForSell,
  Transferred,
  Used,
  Revoked,
};

struct Ticket {
  TicketId id;
  EventId event_id;
  UserId owner_user_id;
  TicketStatus status{TicketStatus::Owned};
  std::uint64_t credential_version{1};
};

class TicketLedger {};

} // namespace ticketx
