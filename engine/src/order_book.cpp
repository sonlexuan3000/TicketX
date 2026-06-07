#include "ticketx/order_book.hpp"

#include <iterator>

namespace ticketx {

namespace {

bool IsValidLimitOrder(const Order& order) {
  return order.type == OrderType::Limit && order.limit_price.has_value() && *order.limit_price > 0;
}

} // namespace

void OrderBook::add_limit_order(Order order) {
  if (!IsValidLimitOrder(order)) {
    return;
  }

  if (orders_by_id_.contains(order.id.value)) {
    return;
  }

  order.status = OrderStatus::Open;
  const Money price = *order.limit_price;

  if (order.side == Side::Buy) {
    auto& orders = bids_[price];
    orders.push_back(order);
    auto it = std::prev(orders.end());

    orders_by_id_[order.id.value] = OrderLocator{
        .side = Side::Buy,
        .price = price,
        .order_it = it,
    };
    ++size_;
    return;
  }

  auto& orders = asks_[price];
  orders.push_back(order);
  auto it = std::prev(orders.end());

  orders_by_id_[order.id.value] = OrderLocator{
      .side = Side::Sell,
      .price = price,
      .order_it = it,
  };
  ++size_;
}

std::optional<Order> OrderBook::remove_order(OrderId order_id) {
  const auto locator_it = orders_by_id_.find(order_id.value);
  if (locator_it == orders_by_id_.end()) {
    return std::nullopt;
  }

  const OrderLocator locator = locator_it->second;
  Order removed_order = *locator.order_it;

  if (locator.side == Side::Buy) {
    const auto level_it = bids_.find(locator.price);
    if (level_it == bids_.end()) {
      orders_by_id_.erase(locator_it);
      return std::nullopt;
    }

    level_it->second.erase(locator.order_it);
    if (level_it->second.empty()) {
      bids_.erase(level_it);
    }
  } else {
    const auto level_it = asks_.find(locator.price);
    if (level_it == asks_.end()) {
      orders_by_id_.erase(locator_it);
      return std::nullopt;
    }

    level_it->second.erase(locator.order_it);
    if (level_it->second.empty()) {
      asks_.erase(level_it);
    }
  }

  orders_by_id_.erase(locator_it);
  --size_;
  return removed_order;
}

std::optional<Order> OrderBook::cancel_order(OrderId order_id) {
  auto order = remove_order(order_id);
  if (!order.has_value()) {
    return std::nullopt;
  }

  order->status = OrderStatus::Cancelled;
  return order;
}

ExecutionReport OrderBook::place_limit_order(Order order) {
  ExecutionReport report{
      .order_id = order.id,
      .status = OrderStatus::Rejected,
      .trade = std::nullopt,
  };

  if (!IsValidLimitOrder(order)) {
    return report;
  }

  if (orders_by_id_.contains(order.id.value)) {
    return report;
  }

  const Money price = *order.limit_price;

  if (order.side == Side::Buy) {
    const std::optional<Order> best_ask_order = best_ask();
    if (!best_ask_order.has_value() || !best_ask_order->limit_price.has_value() ||
        *best_ask_order->limit_price > price) {
      add_limit_order(order);
      report.status = OrderStatus::Open;
      return report;
    }

    std::optional<Order> maker = extract_best_ask();
    if (!maker.has_value() || !maker->limit_price.has_value()) {
      return report;
    }

    order.status = OrderStatus::Filled;
    maker->status = OrderStatus::Filled;
    report.status = OrderStatus::Filled;
    report.trade = Trade{
        .buy_order_id = order.id,
        .sell_order_id = maker->id,
        .buyer_user_id = order.user_id,
        .seller_user_id = maker->user_id,
        .event_id = order.event_id,
        .category = order.category,
        .price = *maker->limit_price,
    };
    return report;
  }

  const std::optional<Order> best_bid_order = best_bid();
  if (!best_bid_order.has_value() || !best_bid_order->limit_price.has_value() ||
      *best_bid_order->limit_price < price) {
    add_limit_order(order);
    report.status = OrderStatus::Open;
    return report;
  }

  std::optional<Order> maker = extract_best_bid();
  if (!maker.has_value() || !maker->limit_price.has_value()) {
    return report;
  }

  order.status = OrderStatus::Filled;
  maker->status = OrderStatus::Filled;
  report.status = OrderStatus::Filled;
  report.trade = Trade{
      .buy_order_id = maker->id,
      .sell_order_id = order.id,
      .buyer_user_id = maker->user_id,
      .seller_user_id = order.user_id,
      .event_id = order.event_id,
      .category = order.category,
      .price = *maker->limit_price,
  };
  return report;
}

ExecutionReport OrderBook::place_market_order(Order order) {
  ExecutionReport report{
      .order_id = order.id,
      .status = OrderStatus::Rejected,
      .trade = std::nullopt,
  };

  if (order.type != OrderType::Market) {
    return report;
  }

  if (orders_by_id_.contains(order.id.value)) {
    return report;
  }

  if (order.side == Side::Buy) {
    std::optional<Order> maker = extract_best_ask();
    if (!maker.has_value() || !maker->limit_price.has_value()) {
      return report;
    }

    order.status = OrderStatus::Filled;
    maker->status = OrderStatus::Filled;
    report.status = OrderStatus::Filled;
    report.trade = Trade{
        .buy_order_id = order.id,
        .sell_order_id = maker->id,
        .buyer_user_id = order.user_id,
        .seller_user_id = maker->user_id,
        .event_id = order.event_id,
        .category = order.category,
        .price = *maker->limit_price,
    };
    return report;
  }

  std::optional<Order> maker = extract_best_bid();
  if (!maker.has_value() || !maker->limit_price.has_value()) {
    return report;
  }

  order.status = OrderStatus::Filled;
  maker->status = OrderStatus::Filled;
  report.status = OrderStatus::Filled;
  report.trade = Trade{
      .buy_order_id = maker->id,
      .sell_order_id = order.id,
      .buyer_user_id = maker->user_id,
      .seller_user_id = order.user_id,
      .event_id = order.event_id,
      .category = order.category,
      .price = *maker->limit_price,
  };
  return report;
}

std::optional<Order> OrderBook::best_bid() const {
  if (bids_.empty()) {
    return std::nullopt;
  }

  const auto& orders = bids_.begin()->second;
  if (orders.empty()) {
    return std::nullopt;
  }

  return orders.front();
}

std::optional<Order> OrderBook::extract_best_bid() {
  if (bids_.empty()) {
    return std::nullopt;
  }

  auto level_it = bids_.begin();
  auto& orders = level_it->second;
  if (orders.empty()) {
    bids_.erase(level_it);
    return std::nullopt;
  }

  Order order = orders.front();
  orders.pop_front();

  orders_by_id_.erase(order.id.value);
  --size_;

  if (orders.empty()) {
    bids_.erase(level_it);
  }

  return order;
}

std::optional<Order> OrderBook::best_ask() const {
  if (asks_.empty()) {
    return std::nullopt;
  }

  const auto& orders = asks_.begin()->second;
  if (orders.empty()) {
    return std::nullopt;
  }

  return orders.front();
}

std::optional<Order> OrderBook::extract_best_ask() {
  if (asks_.empty()) {
    return std::nullopt;
  }

  auto level_it = asks_.begin();
  auto& orders = level_it->second;
  if (orders.empty()) {
    asks_.erase(level_it);
    return std::nullopt;
  }

  Order order = orders.front();
  orders.pop_front();

  orders_by_id_.erase(order.id.value);
  --size_;

  if (orders.empty()) {
    asks_.erase(level_it);
  }

  return order;
}



} // namespace ticketx
