#pragma once

#include "ticketx/event_log.hpp"
#include "ticketx/order_book.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace ticketx {

struct MarketKey {
  EventId event_id;
  std::string category;
};

bool operator==(const MarketKey& lhs, const MarketKey& rhs);

struct MarketKeyHash {
  std::size_t operator()(const MarketKey& key) const;
};

class MatchingEngine {
public:
  [[nodiscard]] constexpr std::string_view name() const noexcept { return "TicketX MatchingEngine"; }
  [[nodiscard]] const EventLog& event_log() const noexcept { return event_log_; }

  ExecutionReport place_limit_order(Order order);
  ExecutionReport place_market_order(Order order);

  std::optional<Order> best_bid(EventId event_id, const std::string& category) const;
  std::optional<Order> best_ask(EventId event_id, const std::string& category) const;
  std::optional<Order> cancel_order(OrderId order_id);

private:
  std::unordered_map<MarketKey, OrderBook, MarketKeyHash> books_;
  std::unordered_map<std::uint64_t, MarketKey> order_market_;
  EventLog event_log_;
};

} // namespace ticketx
