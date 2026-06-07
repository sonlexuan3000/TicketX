#pragma once

#include "ticketx/event_log.hpp"
#include "ticketx/order_book.hpp"

#include <string_view>

namespace ticketx {

class MatchingEngine {
public:
  [[nodiscard]] constexpr std::string_view name() const noexcept { return "TicketX MatchingEngine"; }
  [[nodiscard]] const EventLog& event_log() const noexcept { return event_log_; }

private:
  OrderBook order_book_;
  EventLog event_log_;
};

} // namespace ticketx
