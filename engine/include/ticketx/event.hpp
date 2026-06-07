#pragma once

#include "ticketx/ticket_ledger.hpp"
#include "ticketx/types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ticketx {

struct PrimarySaleCategory {
  std::string name;
  Money price{0};
  std::uint64_t remaining{0};
};

struct Event {
  EventId id;
  std::string name;
  std::vector<PrimarySaleCategory> categories;
};

enum class PrimaryBuyStatus {
  Accepted,
  EventNotFound,
  CategoryNotFound,
  SoldOut,
  BuyerAlreadyOwnsTicket,
  InsufficientFunds,
  TicketIssueFailed,
};

struct PrimaryBuyResult {
  PrimaryBuyStatus status{PrimaryBuyStatus::EventNotFound};
  std::optional<Ticket> ticket;

  [[nodiscard]] constexpr bool accepted() const noexcept {
    return status == PrimaryBuyStatus::Accepted;
  }
};

} // namespace ticketx
