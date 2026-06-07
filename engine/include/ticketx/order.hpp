#pragma once

#include "ticketx/types.hpp"

#include <chrono>
#include <optional>
#include <string>

namespace ticketx {

enum class Side {
  Buy,
  Sell,
};

enum class OrderType {
  Limit,
  Market,
};

enum class OrderStatus {
  Accepted,
  Rejected,
  Open,
  Filled,
  Cancelled,
};

struct Order {
  OrderId id;
  UserId user_id;
  EventId event_id;
  std::string category;
  Side side{Side::Buy};
  OrderType type{OrderType::Limit};
  std::optional<Money> limit_price;
  OrderStatus status{OrderStatus::Accepted};
  std::chrono::steady_clock::time_point created_at{};
};

} // namespace ticketx
