#pragma once

#include "ticketx/order.hpp"
#include "ticketx/trade.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>

namespace ticketx {

struct ExecutionReport {
  OrderId order_id;
  OrderStatus status{OrderStatus::Rejected};
  std::optional<Trade> trade;
};

class OrderBook {
public:
  [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }
  [[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }

  void add_limit_order(Order order);
  ExecutionReport place_limit_order(Order order);
  ExecutionReport place_market_order(Order order);
  std::optional<Order> cancel_order(OrderId order_id);
  std::optional<Order> best_bid() const;
  std::optional<Order> best_ask() const;

private:
  std::size_t size_{0};
  using OrderList = std::list<Order>;
  using BidsLevel = std::map<Money, OrderList, std::greater<Money>>;
  BidsLevel bids_;
  using AsksLevel = std::map<Money, OrderList>;
  AsksLevel asks_;

  struct OrderLocator {
    Side side;
    Money price;
    OrderList::iterator order_it;
  };
  std::unordered_map<std::uint64_t, OrderLocator> orders_by_id_;

  std::optional<Order> remove_order(OrderId order_id);
  std::optional<Order> extract_best_bid();
  std::optional<Order> extract_best_ask();
};

} // namespace ticketx
