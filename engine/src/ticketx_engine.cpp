#include "ticketx/ticketx_engine.hpp"
#include "ticketx/version.hpp"

#include <limits>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

namespace ticketx {

namespace {

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

bool IsValidMarketOrder(const Order& order) {
  return order.type == OrderType::Market && !order.category.empty();
}

std::string EventPayload(EventId event_id) {
  return "{\"event_id\":" + std::to_string(event_id.value) + "}";
}

std::string DepositPayload(UserId user_id, Money amount) {
  return "{\"user_id\":" + std::to_string(user_id.value) + ",\"amount\":" +
         std::to_string(amount) + "}";
}

std::string PrimaryBuyPayload(const Ticket& ticket, Money price) {
  return "{\"ticket_id\":" + std::to_string(ticket.id.value) + ",\"buyer_user_id\":" +
         std::to_string(ticket.owner_user_id.value) + ",\"event_id\":" +
         std::to_string(ticket.event_id.value) + ",\"price\":" + std::to_string(price) + "}";
}

std::string OrderPayload(const Order& order) {
  return "{\"order_id\":" + std::to_string(order.id.value) + ",\"user_id\":" +
         std::to_string(order.user_id.value) + ",\"event_id\":" +
         std::to_string(order.event_id.value) + "}";
}

std::string MatchPayload(const Trade& trade) {
  return "{\"buy_order_id\":" + std::to_string(trade.buy_order_id.value) +
         ",\"sell_order_id\":" + std::to_string(trade.sell_order_id.value) + ",\"price\":" +
         std::to_string(trade.price) + "}";
}

std::string WalletSettlementPayload(const Trade& trade) {
  return "{\"buyer_user_id\":" + std::to_string(trade.buyer_user_id.value) +
         ",\"seller_user_id\":" + std::to_string(trade.seller_user_id.value) +
         ",\"price\":" + std::to_string(trade.price) + "}";
}

std::string TicketTransferPayload(const Trade& trade) {
  return "{\"buyer_user_id\":" + std::to_string(trade.buyer_user_id.value) +
         ",\"seller_user_id\":" + std::to_string(trade.seller_user_id.value) +
         ",\"event_id\":" + std::to_string(trade.event_id.value) + "}";
}

} // namespace

static_assert(sizeof(Money) == sizeof(std::int64_t));

bool TicketXEngine::create_event(Event event) {
  if (event.name.empty() || event.categories.empty() || events_.contains(event.id.value)) {
    return false;
  }

  std::unordered_set<std::string> category_names;
  for (const PrimarySaleCategory& category : event.categories) {
    if (category.name.empty() || category.price <= 0) {
      return false;
    }
    if (!category_names.insert(category.name).second) {
      return false;
    }
  }

  const EventId event_id = event.id;
  events_.emplace(event.id.value, std::move(event));
  append_event(event_type::EventCreated, EventPayload(event_id));
  return true;
}

std::optional<Event> TicketXEngine::event(EventId event_id) const {
  const auto it = events_.find(event_id.value);
  if (it == events_.end()) {
    return std::nullopt;
  }
  return it->second;
}

PrimarySaleCategory* TicketXEngine::find_category(Event& event, const std::string& category) {
  for (PrimarySaleCategory& cat : event.categories) {
    if (cat.name == category) {
      return &cat;
    }
  }
  return nullptr;
}

const PrimarySaleCategory* TicketXEngine::find_category(const Event& event,
                                                        const std::string& category) const {
  for (const PrimarySaleCategory& cat : event.categories) {
    if (cat.name == category) {
      return &cat;
    }
  }
  return nullptr;
}

PrimaryBuyResult TicketXEngine::primary_buy(UserId user_id, EventId event_id,
                                            const std::string& category) {
  const auto event_it = events_.find(event_id.value);
  if (event_it == events_.end()) {
    return PrimaryBuyResult{
        .status = PrimaryBuyStatus::EventNotFound,
    };
  }

  PrimarySaleCategory* primary_sale = find_category(event_it->second, category);
  if (primary_sale == nullptr) {
    return PrimaryBuyResult{
        .status = PrimaryBuyStatus::CategoryNotFound,
    };
  }

  if (primary_sale->remaining == 0) {
    return PrimaryBuyResult{
        .status = PrimaryBuyStatus::SoldOut,
    };
  }

  if (tickets_.owns_active_ticket(user_id, event_id) ||
      has_open_buy_for_event(user_id, event_id)) {
    return PrimaryBuyResult{
        .status = PrimaryBuyStatus::BuyerAlreadyOwnsTicket,
    };
  }

  if (!wallets_.withdraw(user_id, primary_sale->price)) {
    return PrimaryBuyResult{
        .status = PrimaryBuyStatus::InsufficientFunds,
    };
  }

  const Ticket ticket{
      .id = TicketId{next_ticket_id_++},
      .event_id = event_id,
      .category = category,
      .owner_user_id = user_id,
      .status = TicketStatus::Owned,
  };
  if (!tickets_.issue_ticket(ticket)) {
    wallets_.deposit(user_id, primary_sale->price);
    return PrimaryBuyResult{
        .status = PrimaryBuyStatus::TicketIssueFailed,
    };
  }

  --primary_sale->remaining;
  append_event(event_type::PrimaryTicketBought, PrimaryBuyPayload(ticket, primary_sale->price));
  return PrimaryBuyResult{
      .status = PrimaryBuyStatus::Accepted,
      .ticket = ticket,
  };
}

bool TicketXEngine::deposit(UserId user_id, Money amount) {
  if (!wallets_.deposit(user_id, amount)) {
    return false;
  }
  append_event(event_type::WalletDeposited, DepositPayload(user_id, amount));
  return true;
}

bool TicketXEngine::issue_ticket(Ticket ticket) {
  if (has_open_buy_for_event(ticket.owner_user_id, ticket.event_id)) {
    return false;
  }
  return tickets_.issue_ticket(std::move(ticket));
}

WalletBalance TicketXEngine::wallet_balance(UserId user_id) const {
  return wallets_.balance(user_id);
}

std::optional<Ticket> TicketXEngine::active_ticket(UserId user_id, EventId event_id) const {
  return tickets_.active_ticket(user_id, event_id);
}

std::optional<Ticket> TicketXEngine::unlocked_ticket(UserId user_id, EventId event_id,
                                                     const std::string& category) const {
  return tickets_.unlocked_ticket(user_id, event_id, category);
}

std::optional<Ticket> TicketXEngine::locked_ticket(UserId user_id, EventId event_id,
                                                   const std::string& category) const {
  return tickets_.locked_ticket(user_id, event_id, category);
}

ExecutionReport TicketXEngine::place_limit_order(Order order) {
  if (!IsValidLimitOrder(order) || order.category.empty() || open_orders_.contains(order.id.value)) {
    return RejectedReportFor(order);
  }

  const Money limit_price = *order.limit_price;
  if (order.side == Side::Buy) {
    return place_buy_limit(order, limit_price);
  }
  return place_sell_limit(order);
}

ExecutionReport TicketXEngine::place_buy_limit(Order order, Money limit_price) {
  if (tickets_.owns_active_ticket(order.user_id, order.event_id)) {
    return RejectedReportFor(order);
  }
  if (!wallets_.lock_funds(order.user_id, limit_price)) {
    return RejectedReportFor(order);
  }

  locked_buy_amounts_.emplace(order.id.value, limit_price);
  ExecutionReport report = matching_engine_.place_limit_order(order);
  if (report.status == OrderStatus::Rejected) {
    wallets_.unlock_funds(order.user_id, limit_price);
    locked_buy_amounts_.erase(order.id.value);
    return report;
  }

  if (report.status == OrderStatus::Open) {
    order.status = OrderStatus::Open;
    open_orders_.emplace(order.id.value, order);
    append_event(event_type::OrderPlaced, OrderPayload(order));
    return report;
  }

  if (!report.trade.has_value() || !settle_locked_buyer_trade(*report.trade)) {
    return RejectedReportFor(order);
  }
  return report;
}

ExecutionReport TicketXEngine::place_sell_limit(Order order) {
  const std::optional<Ticket> locked_ticket =
      tickets_.lock_ticket(order.user_id, order.event_id, order.category);
  if (!locked_ticket.has_value()) {
    return RejectedReportFor(order);
  }

  ExecutionReport report = matching_engine_.place_limit_order(order);
  if (report.status == OrderStatus::Rejected) {
    tickets_.unlock_ticket(order.user_id, order.event_id, order.category);
    return report;
  }

  if (report.status == OrderStatus::Open) {
    order.status = OrderStatus::Open;
    open_orders_.emplace(order.id.value, order);
    append_event(event_type::OrderPlaced, OrderPayload(order));
    return report;
  }

  if (!report.trade.has_value() || !settle_locked_buyer_trade(*report.trade)) {
    return RejectedReportFor(order);
  }
  return report;
}

ExecutionReport TicketXEngine::place_market_order(Order order) {
  if (!IsValidMarketOrder(order) || open_orders_.contains(order.id.value)) {
    return RejectedReportFor(order);
  }

  if (order.side == Side::Buy) {
    return place_market_buy(order);
  }
  return place_market_sell(order);
}

ExecutionReport TicketXEngine::place_market_buy(Order order) {
  if (tickets_.owns_active_ticket(order.user_id, order.event_id)) {
    return RejectedReportFor(order);
  }

  const std::optional<Order> best_ask = matching_engine_.best_ask(order.event_id, order.category);
  if (!best_ask.has_value() || !best_ask->limit_price.has_value()) {
    return RejectedReportFor(order);
  }
  if (wallets_.balance(order.user_id).available < *best_ask->limit_price) {
    return RejectedReportFor(order);
  }

  ExecutionReport report = matching_engine_.place_market_order(order);
  if (!report.trade.has_value()) {
    return report;
  }
  if (!settle_market_buy_trade(*report.trade)) {
    return RejectedReportFor(order);
  }
  return report;
}

ExecutionReport TicketXEngine::place_market_sell(Order order) {
  const std::optional<Ticket> locked_ticket =
      tickets_.lock_ticket(order.user_id, order.event_id, order.category);
  if (!locked_ticket.has_value()) {
    return RejectedReportFor(order);
  }

  ExecutionReport report = matching_engine_.place_market_order(order);
  if (!report.trade.has_value()) {
    tickets_.unlock_ticket(order.user_id, order.event_id, order.category);
    return report;
  }
  if (!settle_locked_buyer_trade(*report.trade)) {
    return RejectedReportFor(order);
  }
  return report;
}

std::optional<Order> TicketXEngine::cancel_order(OrderId order_id) {
  const auto open_order_it = open_orders_.find(order_id.value);
  if (open_order_it == open_orders_.end()) {
    return std::nullopt;
  }

  std::optional<Order> cancelled_order = matching_engine_.cancel_order(order_id);
  if (!cancelled_order.has_value()) {
    open_orders_.erase(open_order_it);
    locked_buy_amounts_.erase(order_id.value);
    return std::nullopt;
  }

  const Order order = *cancelled_order;
  if (order.side == Side::Buy) {
    const auto locked_it = locked_buy_amounts_.find(order.id.value);
    if (locked_it != locked_buy_amounts_.end()) {
      wallets_.unlock_funds(order.user_id, locked_it->second);
      locked_buy_amounts_.erase(locked_it);
    }
  } else {
    tickets_.unlock_ticket(order.user_id, order.event_id, order.category);
  }

  open_orders_.erase(open_order_it);
  append_event(event_type::OrderCancelled, OrderPayload(order));
  return cancelled_order;
}

std::optional<Order> TicketXEngine::best_bid(EventId event_id,
                                             const std::string& category) const {
  return matching_engine_.best_bid(event_id, category);
}

std::optional<Order> TicketXEngine::best_ask(EventId event_id,
                                             const std::string& category) const {
  return matching_engine_.best_ask(event_id, category);
}

bool TicketXEngine::settle_locked_buyer_trade(const Trade& trade) {
  const auto locked_it = locked_buy_amounts_.find(trade.buy_order_id.value);
  if (locked_it == locked_buy_amounts_.end() || locked_it->second < trade.price ||
      !can_credit(trade.seller_user_id, trade.price) || !can_transfer_ticket(trade)) {
    return false;
  }

  const Money locked_amount = locked_it->second;
  if (!wallets_.debit_locked(trade.buyer_user_id, trade.price)) {
    return false;
  }

  const Money refund = locked_amount - trade.price;
  if (refund > 0 && !wallets_.unlock_funds(trade.buyer_user_id, refund)) {
    return false;
  }
  if (!wallets_.credit(trade.seller_user_id, trade.price)) {
    return false;
  }
  if (!tickets_.transfer_ticket(trade.seller_user_id, trade.buyer_user_id, trade.event_id,
                                trade.category)
           .has_value()) {
    return false;
  }

  locked_buy_amounts_.erase(locked_it);
  clear_filled_orders(trade);
  append_trade_events(trade);
  return true;
}

bool TicketXEngine::settle_market_buy_trade(const Trade& trade) {
  if (!can_credit(trade.seller_user_id, trade.price) || !can_transfer_ticket(trade)) {
    return false;
  }
  if (!wallets_.withdraw(trade.buyer_user_id, trade.price)) {
    return false;
  }
  if (!wallets_.credit(trade.seller_user_id, trade.price)) {
    return false;
  }
  if (!tickets_.transfer_ticket(trade.seller_user_id, trade.buyer_user_id, trade.event_id,
                                trade.category)
           .has_value()) {
    return false;
  }

  clear_filled_orders(trade);
  append_trade_events(trade);
  return true;
}

bool TicketXEngine::can_credit(UserId user_id, Money amount) const {
  if (amount <= 0) {
    return false;
  }
  const WalletBalance balance = wallets_.balance(user_id);
  return balance.available <= std::numeric_limits<Money>::max() - amount;
}

bool TicketXEngine::can_transfer_ticket(const Trade& trade) const {
  if (tickets_.owns_active_ticket(trade.buyer_user_id, trade.event_id)) {
    return false;
  }
  return tickets_.locked_ticket(trade.seller_user_id, trade.event_id, trade.category).has_value() ||
         tickets_.unlocked_ticket(trade.seller_user_id, trade.event_id, trade.category).has_value();
}

bool TicketXEngine::has_open_buy_for_event(UserId user_id, EventId event_id) const {
  for (const auto& [unused_order_id, order] : open_orders_) {
    (void)unused_order_id;
    if (order.side == Side::Buy && order.user_id.value == user_id.value &&
        order.event_id.value == event_id.value) {
      return true;
    }
  }
  return false;
}

void TicketXEngine::clear_filled_orders(const Trade& trade) {
  open_orders_.erase(trade.buy_order_id.value);
  open_orders_.erase(trade.sell_order_id.value);
}

void TicketXEngine::append_event(std::string_view type, std::string payload_json) {
  event_log_.push_back(EventRecord{
      .type = std::string{type},
      .payload_json = std::move(payload_json),
  });
}

void TicketXEngine::append_trade_events(const Trade& trade) {
  append_event(event_type::OrderMatched, MatchPayload(trade));
  append_event(event_type::WalletSettled, WalletSettlementPayload(trade));
  append_event(event_type::TicketTransferred, TicketTransferPayload(trade));
}

std::string_view version() noexcept { return "0.1.0"; }

} // namespace ticketx
