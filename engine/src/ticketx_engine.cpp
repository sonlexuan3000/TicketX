#include "ticketx/ticketx_engine.hpp"
#include "ticketx/version.hpp"

#include <cstddef>
#include <limits>
#include <memory>
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

std::string JsonString(const std::string& value) {
  std::string escaped{"\""};
  constexpr char kHex[] = "0123456789abcdef";
  for (const unsigned char ch : value) {
    switch (ch) {
    case '"':
      escaped += "\\\"";
      break;
    case '\\':
      escaped += "\\\\";
      break;
    case '\b':
      escaped += "\\b";
      break;
    case '\f':
      escaped += "\\f";
      break;
    case '\n':
      escaped += "\\n";
      break;
    case '\r':
      escaped += "\\r";
      break;
    case '\t':
      escaped += "\\t";
      break;
    default:
      if (ch < 0x20) {
        escaped += "\\u00";
        escaped.push_back(kHex[ch >> 4]);
        escaped.push_back(kHex[ch & 0x0F]);
      } else {
        escaped.push_back(static_cast<char>(ch));
      }
    }
  }
  escaped.push_back('"');
  return escaped;
}

std::string_view SideName(Side side) {
  switch (side) {
  case Side::Buy:
    return "Buy";
  case Side::Sell:
    return "Sell";
  }
  return "Unknown";
}

std::string_view OrderTypeName(OrderType type) {
  switch (type) {
  case OrderType::Limit:
    return "Limit";
  case OrderType::Market:
    return "Market";
  }
  return "Unknown";
}

std::string_view OrderStatusName(OrderStatus status) {
  switch (status) {
  case OrderStatus::Accepted:
    return "Accepted";
  case OrderStatus::Rejected:
    return "Rejected";
  case OrderStatus::Open:
    return "Open";
  case OrderStatus::Filled:
    return "Filled";
  case OrderStatus::Cancelled:
    return "Cancelled";
  }
  return "Unknown";
}

std::string_view TicketStatusName(TicketStatus status) {
  switch (status) {
  case TicketStatus::Owned:
    return "Owned";
  case TicketStatus::LockedForSell:
    return "LockedForSell";
  case TicketStatus::Transferred:
    return "Transferred";
  case TicketStatus::Used:
    return "Used";
  case TicketStatus::Revoked:
    return "Revoked";
  }
  return "Unknown";
}

std::string EventPayload(const Event& event) {
  std::string payload = "{\"event_id\":" + std::to_string(event.id.value) + ",\"name\":" +
                        JsonString(event.name) + ",\"categories\":[";
  for (std::size_t i = 0; i < event.categories.size(); ++i) {
    const PrimarySaleCategory& category = event.categories[i];
    if (i > 0) {
      payload += ",";
    }
    payload += "{\"name\":" + JsonString(category.name) + ",\"price\":" +
               std::to_string(category.price) + ",\"remaining\":" +
               std::to_string(category.remaining) + "}";
  }
  payload += "]}";
  return payload;
}

std::string DepositPayload(UserId user_id, Money amount) {
  return "{\"user_id\":" + std::to_string(user_id.value) + ",\"amount\":" +
         std::to_string(amount) + "}";
}

std::string TicketPayload(const Ticket& ticket) {
  return "{\"ticket_id\":" + std::to_string(ticket.id.value) + ",\"owner_user_id\":" +
         std::to_string(ticket.owner_user_id.value) + ",\"event_id\":" +
         std::to_string(ticket.event_id.value) + ",\"category\":" +
         JsonString(ticket.category) + ",\"status\":" +
         JsonString(std::string{TicketStatusName(ticket.status)}) + ",\"credential_version\":" +
         std::to_string(ticket.credential_version) + "}";
}

std::string PrimaryBuyPayload(const Ticket& ticket, Money price) {
  return "{\"ticket_id\":" + std::to_string(ticket.id.value) + ",\"buyer_user_id\":" +
         std::to_string(ticket.owner_user_id.value) + ",\"event_id\":" +
         std::to_string(ticket.event_id.value) + ",\"category\":" +
         JsonString(ticket.category) + ",\"price\":" + std::to_string(price) +
         ",\"credential_version\":" + std::to_string(ticket.credential_version) + "}";
}

std::string OrderPayload(const Order& order) {
  return "{\"order_id\":" + std::to_string(order.id.value) + ",\"user_id\":" +
         std::to_string(order.user_id.value) + ",\"event_id\":" +
         std::to_string(order.event_id.value) + ",\"category\":" +
         JsonString(order.category) + ",\"side\":" +
         JsonString(std::string{SideName(order.side)}) + ",\"type\":" +
         JsonString(std::string{OrderTypeName(order.type)}) + ",\"limit_price\":" +
         (order.limit_price.has_value() ? std::to_string(*order.limit_price) : "null") +
         ",\"status\":" + JsonString(std::string{OrderStatusName(order.status)}) + "}";
}

std::string MatchPayload(const Trade& trade) {
  return "{\"buy_order_id\":" + std::to_string(trade.buy_order_id.value) +
         ",\"sell_order_id\":" + std::to_string(trade.sell_order_id.value) +
         ",\"buyer_user_id\":" + std::to_string(trade.buyer_user_id.value) +
         ",\"seller_user_id\":" + std::to_string(trade.seller_user_id.value) +
         ",\"event_id\":" + std::to_string(trade.event_id.value) + ",\"category\":" +
         JsonString(trade.category) + ",\"price\":" + std::to_string(trade.price) + "}";
}

std::string WalletSettlementPayload(const Trade& trade) {
  return "{\"buyer_user_id\":" + std::to_string(trade.buyer_user_id.value) +
         ",\"seller_user_id\":" + std::to_string(trade.seller_user_id.value) +
         ",\"event_id\":" + std::to_string(trade.event_id.value) + ",\"category\":" +
         JsonString(trade.category) + ",\"price\":" + std::to_string(trade.price) + "}";
}

std::string TicketTransferPayload(const Trade& trade) {
  return "{\"buyer_user_id\":" + std::to_string(trade.buyer_user_id.value) +
         ",\"seller_user_id\":" + std::to_string(trade.seller_user_id.value) +
         ",\"event_id\":" + std::to_string(trade.event_id.value) + ",\"category\":" +
         JsonString(trade.category) + "}";
}

} // namespace

static_assert(sizeof(Money) == sizeof(std::int64_t));

TicketXEngine::TicketXEngine(std::filesystem::path event_log_path)
    : event_writer_(std::make_unique<AsyncEventWriter>(std::move(event_log_path))) {}

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

  std::string payload = EventPayload(event);
  events_.emplace(event.id.value, std::move(event));
  append_event(event_type::EventCreated, std::move(payload));
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
  if (!tickets_.issue_ticket(ticket)) {
    return false;
  }
  append_event(event_type::TicketIssued, TicketPayload(ticket));
  return true;
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
  if (tickets_.owns_active_ticket(order.user_id, order.event_id) ||
      has_open_buy_for_event(order.user_id, order.event_id)) {
    return RejectedReportFor(order);
  }
  if (!wallets_.lock_funds(order.user_id, limit_price)) {
    return RejectedReportFor(order);
  }

  locked_buy_amounts_.emplace(order.id.value, limit_price);
  const std::optional<Trade> preview_trade = preview_limit_trade(order);
  if (preview_trade.has_value() && !can_settle_locked_buyer_trade(*preview_trade)) {
    wallets_.unlock_funds(order.user_id, limit_price);
    locked_buy_amounts_.erase(order.id.value);
    return RejectedReportFor(order);
  }

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

  const std::optional<Trade> preview_trade = preview_limit_trade(order);
  if (preview_trade.has_value() && !can_settle_locked_buyer_trade(*preview_trade)) {
    tickets_.unlock_ticket(order.user_id, order.event_id, order.category);
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

  const std::optional<Trade> preview_trade = preview_market_trade(order);
  if (!preview_trade.has_value() || !can_settle_market_buy_trade(*preview_trade)) {
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

  const std::optional<Trade> preview_trade = preview_market_trade(order);
  if (!preview_trade.has_value() || !can_settle_locked_buyer_trade(*preview_trade)) {
    tickets_.unlock_ticket(order.user_id, order.event_id, order.category);
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
  if (!can_settle_locked_buyer_trade(trade)) {
    return false;
  }

  const auto locked_it = locked_buy_amounts_.find(trade.buy_order_id.value);
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
  if (!can_settle_market_buy_trade(trade)) {
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

  const std::optional<Ticket> locked_ticket =
      tickets_.locked_ticket(trade.seller_user_id, trade.event_id, trade.category);
  if (locked_ticket.has_value()) {
    return locked_ticket->credential_version < std::numeric_limits<std::uint64_t>::max();
  }

  const std::optional<Ticket> unlocked_ticket =
      tickets_.unlocked_ticket(trade.seller_user_id, trade.event_id, trade.category);
  return unlocked_ticket.has_value() &&
         unlocked_ticket->credential_version < std::numeric_limits<std::uint64_t>::max();
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
  EventRecord record{
      .sequence_id = next_event_sequence_id_++,
      .type = std::string{type},
      .payload_json = std::move(payload_json),
  };
  event_log_.push_back(record);
  if (event_writer_ != nullptr) {
    (void)event_writer_->enqueue(std::move(record));
  }
}

void TicketXEngine::append_trade_events(const Trade& trade) {
  append_event(event_type::OrderMatched, MatchPayload(trade));
  append_event(event_type::WalletSettled, WalletSettlementPayload(trade));
  append_event(event_type::TicketTransferred, TicketTransferPayload(trade));
}

bool TicketXEngine::can_settle_locked_buyer_trade(const Trade& trade) const {
  const auto locked_it = locked_buy_amounts_.find(trade.buy_order_id.value);
  if (locked_it == locked_buy_amounts_.end() || locked_it->second < trade.price) {
    return false;
  }

  const Money locked_amount = locked_it->second;
  const Money refund = locked_amount - trade.price;
  const WalletBalance buyer_balance = wallets_.balance(trade.buyer_user_id);
  if (buyer_balance.locked < locked_amount) {
    return false;
  }
  if (refund > 0 &&
      buyer_balance.available > std::numeric_limits<Money>::max() - refund) {
    return false;
  }

  return can_credit(trade.seller_user_id, trade.price) && can_transfer_ticket(trade);
}

bool TicketXEngine::can_settle_market_buy_trade(const Trade& trade) const {
  return can_credit(trade.seller_user_id, trade.price) && can_transfer_ticket(trade) &&
         wallets_.balance(trade.buyer_user_id).available >= trade.price;
}

std::optional<Trade> TicketXEngine::preview_limit_trade(const Order& order) const {
  if (!IsValidLimitOrder(order) || order.category.empty()) {
    return std::nullopt;
  }

  if (order.side == Side::Buy) {
    const std::optional<Order> best_ask =
        matching_engine_.best_ask(order.event_id, order.category);
    if (!best_ask.has_value() || !best_ask->limit_price.has_value() ||
        *best_ask->limit_price > *order.limit_price) {
      return std::nullopt;
    }
    return Trade{
        .buy_order_id = order.id,
        .sell_order_id = best_ask->id,
        .buyer_user_id = order.user_id,
        .seller_user_id = best_ask->user_id,
        .event_id = order.event_id,
        .category = order.category,
        .price = *best_ask->limit_price,
    };
  }

  const std::optional<Order> best_bid =
      matching_engine_.best_bid(order.event_id, order.category);
  if (!best_bid.has_value() || !best_bid->limit_price.has_value() ||
      *best_bid->limit_price < *order.limit_price) {
    return std::nullopt;
  }
  return Trade{
      .buy_order_id = best_bid->id,
      .sell_order_id = order.id,
      .buyer_user_id = best_bid->user_id,
      .seller_user_id = order.user_id,
      .event_id = order.event_id,
      .category = order.category,
      .price = *best_bid->limit_price,
  };
}

std::optional<Trade> TicketXEngine::preview_market_trade(const Order& order) const {
  if (!IsValidMarketOrder(order) || order.category.empty()) {
    return std::nullopt;
  }

  if (order.side == Side::Buy) {
    const std::optional<Order> best_ask =
        matching_engine_.best_ask(order.event_id, order.category);
    if (!best_ask.has_value() || !best_ask->limit_price.has_value()) {
      return std::nullopt;
    }
    return Trade{
        .buy_order_id = order.id,
        .sell_order_id = best_ask->id,
        .buyer_user_id = order.user_id,
        .seller_user_id = best_ask->user_id,
        .event_id = order.event_id,
        .category = order.category,
        .price = *best_ask->limit_price,
    };
  }

  const std::optional<Order> best_bid =
      matching_engine_.best_bid(order.event_id, order.category);
  if (!best_bid.has_value() || !best_bid->limit_price.has_value()) {
    return std::nullopt;
  }
  return Trade{
      .buy_order_id = best_bid->id,
      .sell_order_id = order.id,
      .buyer_user_id = best_bid->user_id,
      .seller_user_id = order.user_id,
      .event_id = order.event_id,
      .category = order.category,
      .price = *best_bid->limit_price,
  };
}

std::string_view version() noexcept { return "0.1.0"; }

} // namespace ticketx
