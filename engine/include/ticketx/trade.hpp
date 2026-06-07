#pragma once

#include "ticketx/types.hpp"

#include <string>

namespace ticketx {

struct Trade {
  OrderId buy_order_id;
  OrderId sell_order_id;
  UserId buyer_user_id;
  UserId seller_user_id;
  EventId event_id;
  std::string category;
  Money price;
};
} // namespace ticketx
