#pragma once

#include <cstdint>

namespace ticketx {

using Money = std::int64_t;

struct UserId {
  std::uint64_t value{};
};

struct EventId {
  std::uint64_t value{};
};

struct TicketId {
  std::uint64_t value{};
};

struct OrderId {
  std::uint64_t value{};
};

struct TradeId {
  std::uint64_t value{};
};

} // namespace ticketx
