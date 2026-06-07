#include "ticketx/matching_engine.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>

namespace ticketx {

namespace {

MarketKey MarketKeyForOrder(const Order& order) {
  return MarketKey{
      .event_id = order.event_id,
      .category = order.category,
  };
}

ExecutionReport RejectedReportFor(const Order& order) {
  return ExecutionReport{
      .order_id = order.id,
      .status = OrderStatus::Rejected,
      .trade = std::nullopt,
  };
}

bool IsValidLimitOrder(const Order& order) {
  return order.type == OrderType::Limit && order.limit_price.has_value() && *order.limit_price > 0;
}

std::size_t HashCombine(std::size_t seed, std::size_t value) {
  return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U));
}

} // namespace

bool operator==(const MarketKey& lhs, const MarketKey& rhs) {
  return lhs.event_id.value == rhs.event_id.value && lhs.category == rhs.category;
}

std::size_t MarketKeyHash::operator()(const MarketKey& key) const {
  std::size_t seed = std::hash<std::uint64_t>{}(key.event_id.value);
  return HashCombine(seed, std::hash<std::string>{}(key.category));
}

ExecutionReport MatchingEngine::place_limit_order(Order order) {
  if (!IsValidLimitOrder(order)) {
    return RejectedReportFor(order);
  }

  if (order_market_.contains(order.id.value)) {
    return RejectedReportFor(order);
  }

  const MarketKey key = MarketKeyForOrder(order);
  ExecutionReport report = books_[key].place_limit_order(order);
  if (report.status == OrderStatus::Open) {
    order_market_[order.id.value] = key;
  }

  if (report.trade.has_value()) {
    order_market_.erase(report.trade->buy_order_id.value);
    order_market_.erase(report.trade->sell_order_id.value);
  }

  return report;
}

ExecutionReport MatchingEngine::place_market_order(Order order) {
  if (order.type != OrderType::Market) {
    return RejectedReportFor(order);
  }

  if (order_market_.contains(order.id.value)) {
    return RejectedReportFor(order);
  }

  const MarketKey key = MarketKeyForOrder(order);
  auto book_it = books_.find(key);
  if (book_it == books_.end()) {
    return RejectedReportFor(order);
  }

  ExecutionReport report = book_it->second.place_market_order(order);
  if (report.trade.has_value()) {
    order_market_.erase(report.trade->buy_order_id.value);
    order_market_.erase(report.trade->sell_order_id.value);
  }

  return report;
}

std::optional<Order> MatchingEngine::best_bid(EventId event_id, const std::string& category) const {
  const auto it = books_.find(MarketKey{
      .event_id = event_id,
      .category = category,
  });
  if (it == books_.end()) {
    return std::nullopt;
  }

  return it->second.best_bid();
}

std::optional<Order> MatchingEngine::best_ask(EventId event_id, const std::string& category) const {
  const auto it = books_.find(MarketKey{
      .event_id = event_id,
      .category = category,
  });
  if (it == books_.end()) {
    return std::nullopt;
  }

  return it->second.best_ask();
}

std::optional<Order> MatchingEngine::cancel_order(OrderId order_id) {
  const auto market_it = order_market_.find(order_id.value);
  if (market_it == order_market_.end()) {
    return std::nullopt;
  }

  const MarketKey market_key = market_it->second;
  auto book_it = books_.find(market_key);
  if (book_it == books_.end()) {
    order_market_.erase(market_it);
    return std::nullopt;
  }

  std::optional<Order> cancelled_order = book_it->second.cancel_order(order_id);
  if (!cancelled_order.has_value()) {
    order_market_.erase(market_it);
    return std::nullopt;
  }

  order_market_.erase(market_it);
  return cancelled_order;
}

} // namespace ticketx
