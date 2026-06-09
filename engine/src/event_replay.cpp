#include "ticketx/event_replay.hpp"
#include "ticketx/event_store.hpp"
#include "ticketx/trade.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

namespace ticketx {

namespace {

std::optional<nlohmann::json> ParsePayload(std::string_view payload_json) {
  nlohmann::json payload =
      nlohmann::json::parse(payload_json.begin(), payload_json.end(), nullptr, false);
  if (payload.is_discarded() || !payload.is_object()) {
    return std::nullopt;
  }
  return payload;
}

std::optional<std::uint64_t> ExtractUintField(const nlohmann::json& payload,
                                              std::string_view field_name) {
  const auto it = payload.find(std::string{field_name});
  if (it == payload.end()) {
    return std::nullopt;
  }
  if (it->is_number_unsigned()) {
    return it->get<std::uint64_t>();
  }
  if (it->is_number_integer()) {
    const std::int64_t value = it->get<std::int64_t>();
    if (value >= 0) {
      return static_cast<std::uint64_t>(value);
    }
  }
  return std::nullopt;
}

std::optional<Money> ExtractMoneyField(const nlohmann::json& payload,
                                       std::string_view field_name) {
  const auto it = payload.find(std::string{field_name});
  if (it == payload.end()) {
    return std::nullopt;
  }
  if (it->is_number_integer()) {
    return it->get<Money>();
  }
  if (it->is_number_unsigned()) {
    const std::uint64_t value = it->get<std::uint64_t>();
    if (value <= static_cast<std::uint64_t>(std::numeric_limits<Money>::max())) {
      return static_cast<Money>(value);
    }
  }
  return std::nullopt;
}

std::optional<std::string> ExtractStringField(const nlohmann::json& payload,
                                             std::string_view field_name) {
  const auto it = payload.find(std::string{field_name});
  if (it == payload.end() || !it->is_string()) {
    return std::nullopt;
  }
  return it->get<std::string>();
}

std::optional<Event> ParseEventPayload(const nlohmann::json& payload) {
  const std::optional<std::uint64_t> event_id = ExtractUintField(payload, "event_id");
  const std::optional<std::string> name = ExtractStringField(payload, "name");
  const auto categories_it = payload.find("categories");
  if (!event_id.has_value() || !name.has_value() || name->empty() ||
      categories_it == payload.end() || !categories_it->is_array() ||
      categories_it->empty()) {
    return std::nullopt;
  }

  std::unordered_set<std::string> category_names;
  std::vector<PrimarySaleCategory> categories;
  categories.reserve(categories_it->size());
  for (const nlohmann::json& category_payload : *categories_it) {
    if (!category_payload.is_object()) {
      return std::nullopt;
    }

    const std::optional<std::string> category_name =
        ExtractStringField(category_payload, "name");
    const std::optional<Money> price = ExtractMoneyField(category_payload, "price");
    const std::optional<std::uint64_t> remaining =
        ExtractUintField(category_payload, "remaining");
    if (!category_name.has_value() || category_name->empty() || !price.has_value() ||
        *price <= 0 || !remaining.has_value() ||
        !category_names.insert(*category_name).second) {
      return std::nullopt;
    }

    categories.push_back(PrimarySaleCategory{
        .name = *category_name,
        .price = *price,
        .remaining = *remaining,
    });
  }

  return Event{
      .id = EventId{*event_id},
      .name = *name,
      .categories = std::move(categories),
  };
}

std::optional<Side> ParseSide(std::string_view side) {
  if (side == "Buy") {
    return Side::Buy;
  }
  if (side == "Sell") {
    return Side::Sell;
  }
  return std::nullopt;
}

std::optional<OrderType> ParseOrderType(std::string_view type) {
  if (type == "Limit") {
    return OrderType::Limit;
  }
  if (type == "Market") {
    return OrderType::Market;
  }
  return std::nullopt;
}

std::optional<OrderStatus> ParseOrderStatus(std::string_view status) {
  if (status == "Accepted") {
    return OrderStatus::Accepted;
  }
  if (status == "Rejected") {
    return OrderStatus::Rejected;
  }
  if (status == "Open") {
    return OrderStatus::Open;
  }
  if (status == "Filled") {
    return OrderStatus::Filled;
  }
  if (status == "Cancelled") {
    return OrderStatus::Cancelled;
  }
  return std::nullopt;
}

std::optional<TicketStatus> ParseTicketStatus(std::string_view status) {
  if (status == "Owned") {
    return TicketStatus::Owned;
  }
  if (status == "LockedForSell") {
    return TicketStatus::LockedForSell;
  }
  if (status == "Transferred") {
    return TicketStatus::Transferred;
  }
  if (status == "Used") {
    return TicketStatus::Used;
  }
  if (status == "Revoked") {
    return TicketStatus::Revoked;
  }
  return std::nullopt;
}

std::optional<Order> ParseOrderPayload(const nlohmann::json& payload) {
  const std::optional<std::uint64_t> order_id = ExtractUintField(payload, "order_id");
  const std::optional<std::uint64_t> user_id = ExtractUintField(payload, "user_id");
  const std::optional<std::uint64_t> event_id = ExtractUintField(payload, "event_id");
  const std::optional<std::string> category = ExtractStringField(payload, "category");
  const std::optional<std::string> side_name = ExtractStringField(payload, "side");
  const std::optional<std::string> type_name = ExtractStringField(payload, "type");
  if (!order_id.has_value() || !user_id.has_value() || !event_id.has_value() ||
      !category.has_value() || !side_name.has_value() || !type_name.has_value()) {
    return std::nullopt;
  }

  const std::optional<Side> side = ParseSide(*side_name);
  const std::optional<OrderType> type = ParseOrderType(*type_name);
  if (!side.has_value() || !type.has_value()) {
    return std::nullopt;
  }

  std::optional<Money> limit_price;
  if (const auto it = payload.find("limit_price"); it != payload.end() && !it->is_null()) {
    limit_price = ExtractMoneyField(payload, "limit_price");
    if (!limit_price.has_value()) {
      return std::nullopt;
    }
  }

  OrderStatus status = OrderStatus::Open;
  if (const std::optional<std::string> status_name = ExtractStringField(payload, "status");
      status_name.has_value()) {
    const std::optional<OrderStatus> parsed_status = ParseOrderStatus(*status_name);
    if (!parsed_status.has_value()) {
      return std::nullopt;
    }
    status = *parsed_status;
  }

  return Order{
      .id = OrderId{*order_id},
      .user_id = UserId{*user_id},
      .event_id = EventId{*event_id},
      .category = *category,
      .side = *side,
      .type = *type,
      .limit_price = limit_price,
      .status = status,
  };
}

std::optional<Ticket> ParseIssuedTicketPayload(const nlohmann::json& payload) {
  const std::optional<std::uint64_t> ticket_id = ExtractUintField(payload, "ticket_id");
  const std::optional<std::uint64_t> owner_user_id =
      ExtractUintField(payload, "owner_user_id");
  const std::optional<std::uint64_t> event_id = ExtractUintField(payload, "event_id");
  const std::optional<std::string> category = ExtractStringField(payload, "category");
  if (!ticket_id.has_value() || !owner_user_id.has_value() || !event_id.has_value() ||
      !category.has_value()) {
    return std::nullopt;
  }

  TicketStatus status = TicketStatus::Owned;
  if (const std::optional<std::string> status_name = ExtractStringField(payload, "status");
      status_name.has_value()) {
    const std::optional<TicketStatus> parsed_status = ParseTicketStatus(*status_name);
    if (!parsed_status.has_value()) {
      return std::nullopt;
    }
    status = *parsed_status;
  }

  std::uint64_t credential_version = 1;
  if (const std::optional<std::uint64_t> parsed_credential_version =
          ExtractUintField(payload, "credential_version");
      parsed_credential_version.has_value()) {
    credential_version = *parsed_credential_version;
  }

  return Ticket{
      .id = TicketId{*ticket_id},
      .event_id = EventId{*event_id},
      .category = *category,
      .owner_user_id = UserId{*owner_user_id},
      .status = status,
      .credential_version = credential_version,
  };
}

std::optional<Ticket> ParsePrimaryTicketPayload(const nlohmann::json& payload) {
  const std::optional<std::uint64_t> ticket_id = ExtractUintField(payload, "ticket_id");
  const std::optional<std::uint64_t> buyer_user_id =
      ExtractUintField(payload, "buyer_user_id");
  const std::optional<std::uint64_t> event_id = ExtractUintField(payload, "event_id");
  const std::optional<std::string> category = ExtractStringField(payload, "category");
  if (!ticket_id.has_value() || !buyer_user_id.has_value() || !event_id.has_value() ||
      !category.has_value()) {
    return std::nullopt;
  }

  std::uint64_t credential_version = 1;
  if (const std::optional<std::uint64_t> parsed_credential_version =
          ExtractUintField(payload, "credential_version");
      parsed_credential_version.has_value()) {
    credential_version = *parsed_credential_version;
  }

  return Ticket{
      .id = TicketId{*ticket_id},
      .event_id = EventId{*event_id},
      .category = *category,
      .owner_user_id = UserId{*buyer_user_id},
      .status = TicketStatus::Owned,
      .credential_version = credential_version,
  };
}

std::optional<Trade> ParseTradePayload(const nlohmann::json& payload) {
  const std::optional<std::uint64_t> buy_order_id =
      ExtractUintField(payload, "buy_order_id");
  const std::optional<std::uint64_t> sell_order_id =
      ExtractUintField(payload, "sell_order_id");
  const std::optional<std::uint64_t> buyer_user_id =
      ExtractUintField(payload, "buyer_user_id");
  const std::optional<std::uint64_t> seller_user_id =
      ExtractUintField(payload, "seller_user_id");
  const std::optional<std::uint64_t> event_id = ExtractUintField(payload, "event_id");
  const std::optional<std::string> category = ExtractStringField(payload, "category");
  const std::optional<Money> price = ExtractMoneyField(payload, "price");
  if (!buy_order_id.has_value() || !sell_order_id.has_value() ||
      !buyer_user_id.has_value() || !seller_user_id.has_value() ||
      !event_id.has_value() || !category.has_value() || !price.has_value()) {
    return std::nullopt;
  }

  return Trade{
      .buy_order_id = OrderId{*buy_order_id},
      .sell_order_id = OrderId{*sell_order_id},
      .buyer_user_id = UserId{*buyer_user_id},
      .seller_user_id = UserId{*seller_user_id},
      .event_id = EventId{*event_id},
      .category = *category,
      .price = *price,
  };
}

bool CanAddMoney(Money current, Money amount) {
  return amount >= 0 && current <= std::numeric_limits<Money>::max() - amount;
}

bool CanSubtractMoney(Money current, Money amount) {
  return amount >= 0 && current >= amount;
}

bool IsLockableBuyOrder(const Order& order) {
  return order.status == OrderStatus::Open && order.side == Side::Buy &&
         order.type == OrderType::Limit && order.limit_price.has_value() &&
         *order.limit_price > 0;
}

bool IsActiveTicketStatus(TicketStatus status) {
  return status == TicketStatus::Owned || status == TicketStatus::LockedForSell;
}

bool OwnsActiveTicketForEvent(const ReplayState& state, std::uint64_t user_id,
                              std::uint64_t event_id) {
  for (const auto& [unused_ticket_id, ticket] : state.tickets) {
    (void)unused_ticket_id;
    if (ticket.owner_user_id.value == user_id && ticket.event_id.value == event_id &&
        IsActiveTicketStatus(ticket.status)) {
      return true;
    }
  }
  return false;
}

bool ApplyTicketIssue(ReplayState& state, const Ticket& ticket) {
  if (ticket.category.empty() || !IsActiveTicketStatus(ticket.status) ||
      ticket.credential_version == 0 || state.tickets.contains(ticket.id.value) ||
      OwnsActiveTicketForEvent(state, ticket.owner_user_id.value, ticket.event_id.value)) {
    return false;
  }

  state.tickets.emplace(ticket.id.value, ticket);
  if (ticket.id.value > state.max_ticket_id) {
    state.max_ticket_id = ticket.id.value;
  }
  return true;
}

auto FindActiveTicketForOwnerMarket(ReplayState& state, std::uint64_t owner_user_id,
                                    std::uint64_t event_id, const std::string& category) {
  for (auto ticket_it = state.tickets.begin(); ticket_it != state.tickets.end();
       ++ticket_it) {
    Ticket& ticket = ticket_it->second;
    if (ticket.owner_user_id.value == owner_user_id && ticket.event_id.value == event_id &&
        ticket.category == category && IsActiveTicketStatus(ticket.status)) {
      return ticket_it;
    }
  }
  return state.tickets.end();
}

PrimarySaleCategory* FindReplayCategory(Event& event, const std::string& category) {
  for (PrimarySaleCategory& candidate : event.categories) {
    if (candidate.name == category) {
      return &candidate;
    }
  }
  return nullptr;
}

bool ApplyEventCreated(ReplayState& state, Event event) {
  if (event.name.empty() || event.categories.empty() ||
      state.events.contains(event.id.value)) {
    return false;
  }
  state.events.emplace(event.id.value, std::move(event));
  return true;
}

bool ApplyPrimaryTicketBuy(ReplayState& state, const Ticket& ticket, Money price) {
  if (price <= 0) {
    return false;
  }

  if (auto wallet_it = state.wallets.find(ticket.owner_user_id.value);
      wallet_it != state.wallets.end() && CanSubtractMoney(wallet_it->second.available, price)) {
    wallet_it->second.available -= price;
  }

  if (auto event_it = state.events.find(ticket.event_id.value); event_it != state.events.end()) {
    PrimarySaleCategory* category = FindReplayCategory(event_it->second, ticket.category);
    if (category != nullptr && category->remaining > 0) {
      --category->remaining;
    }
  }

  return ApplyTicketIssue(state, ticket);
}

bool LockReplayTicketForSell(ReplayState& state, const Order& order) {
  if (order.status != OrderStatus::Open || order.side != Side::Sell || order.category.empty()) {
    return false;
  }

  auto seller_ticket_it =
      FindActiveTicketForOwnerMarket(state, order.user_id.value, order.event_id.value,
                                     order.category);
  if (seller_ticket_it == state.tickets.end() ||
      seller_ticket_it->second.status != TicketStatus::Owned) {
    return false;
  }

  seller_ticket_it->second.status = TicketStatus::LockedForSell;
  return true;
}

bool UnlockReplayTicketForSell(ReplayState& state, const Order& order) {
  if (order.side != Side::Sell || order.category.empty()) {
    return false;
  }

  auto seller_ticket_it =
      FindActiveTicketForOwnerMarket(state, order.user_id.value, order.event_id.value,
                                     order.category);
  if (seller_ticket_it == state.tickets.end() ||
      seller_ticket_it->second.status != TicketStatus::LockedForSell) {
    return false;
  }

  seller_ticket_it->second.status = TicketStatus::Owned;
  return true;
}

bool ApplyTicketTransfer(ReplayState& state, std::uint64_t seller_user_id,
                         std::uint64_t buyer_user_id, std::uint64_t event_id,
                         const std::string& category) {
  if (category.empty() || OwnsActiveTicketForEvent(state, buyer_user_id, event_id)) {
    return false;
  }

  auto seller_ticket_it =
      FindActiveTicketForOwnerMarket(state, seller_user_id, event_id, category);
  if (seller_ticket_it == state.tickets.end() ||
      seller_ticket_it->second.credential_version ==
          std::numeric_limits<std::uint64_t>::max()) {
    return false;
  }

  Ticket& ticket = seller_ticket_it->second;
  ticket.owner_user_id = UserId{buyer_user_id};
  ticket.status = TicketStatus::Owned;
  ++ticket.credential_version;
  return true;
}

bool LockReplayFunds(ReplayState& state, const Order& order) {
  if (!IsLockableBuyOrder(order)) {
    return false;
  }

  const auto wallet_it = state.wallets.find(order.user_id.value);
  if (wallet_it == state.wallets.end()) {
    return false;
  }

  ReplayWallet& wallet = wallet_it->second;
  const Money amount = *order.limit_price;
  if (!CanSubtractMoney(wallet.available, amount) || !CanAddMoney(wallet.locked, amount)) {
    return false;
  }

  wallet.available -= amount;
  wallet.locked += amount;
  return true;
}

bool UnlockReplayFunds(ReplayState& state, const Order& order) {
  if (!IsLockableBuyOrder(order)) {
    return false;
  }

  const auto wallet_it = state.wallets.find(order.user_id.value);
  if (wallet_it == state.wallets.end()) {
    return false;
  }

  ReplayWallet& wallet = wallet_it->second;
  const Money amount = *order.limit_price;
  if (!CanSubtractMoney(wallet.locked, amount) || !CanAddMoney(wallet.available, amount)) {
    return false;
  }

  wallet.locked -= amount;
  wallet.available += amount;
  return true;
}

void RemoveOpenOrderFromSequence(ReplayState& state, std::uint64_t order_id) {
  state.open_order_sequence.erase(
      std::remove(state.open_order_sequence.begin(), state.open_order_sequence.end(), order_id),
      state.open_order_sequence.end());
}

bool ApplyWalletSettlement(ReplayState& state, std::uint64_t buyer_user_id,
                           std::uint64_t seller_user_id, Money price) {
  if (price <= 0) {
    return false;
  }

  const auto buyer_it = state.wallets.find(buyer_user_id);
  if (buyer_it == state.wallets.end()) {
    return false;
  }

  const auto seller_it = state.wallets.find(seller_user_id);
  const Money seller_available =
      seller_it == state.wallets.end() ? 0 : seller_it->second.available;
  if (!CanAddMoney(seller_available, price)) {
    return false;
  }

  const bool debit_locked = CanSubtractMoney(buyer_it->second.locked, price);
  const bool debit_available =
      !debit_locked && CanSubtractMoney(buyer_it->second.available, price);
  if (!debit_locked && !debit_available) {
    return false;
  }

  ReplayWallet& seller_wallet = state.wallets.try_emplace(seller_user_id).first->second;
  ReplayWallet& buyer_wallet = state.wallets.find(buyer_user_id)->second;
  if (debit_locked) {
    buyer_wallet.locked -= price;
  } else {
    buyer_wallet.available -= price;
  }
  seller_wallet.available += price;
  return true;
}

bool ApplyWalletSettlementForTrade(ReplayState& state, const Trade& trade,
                                   const std::optional<Order>& open_buy_order) {
  if (trade.price <= 0) {
    return false;
  }

  const auto buyer_it = state.wallets.find(trade.buyer_user_id.value);
  if (buyer_it == state.wallets.end()) {
    return false;
  }

  const auto seller_it = state.wallets.find(trade.seller_user_id.value);
  const Money seller_available =
      seller_it == state.wallets.end() ? 0 : seller_it->second.available;
  if (!CanAddMoney(seller_available, trade.price)) {
    return false;
  }

  ReplayWallet& buyer_wallet = buyer_it->second;
  ReplayWallet& seller_wallet =
      state.wallets.try_emplace(trade.seller_user_id.value).first->second;

  if (open_buy_order.has_value()) {
    if (!open_buy_order->limit_price.has_value() ||
        *open_buy_order->limit_price < trade.price ||
        !CanSubtractMoney(buyer_wallet.locked, *open_buy_order->limit_price)) {
      return false;
    }

    const Money refund = *open_buy_order->limit_price - trade.price;
    if (refund > 0 && !CanAddMoney(buyer_wallet.available, refund)) {
      return false;
    }

    buyer_wallet.locked -= *open_buy_order->limit_price;
    buyer_wallet.available += refund;
  } else {
    if (!CanSubtractMoney(buyer_wallet.available, trade.price)) {
      return false;
    }
    buyer_wallet.available -= trade.price;
  }

  seller_wallet.available += trade.price;
  return true;
}

bool ApplyTradeGroup(ReplayState& state, const Trade& trade) {
  const auto open_buy_it = state.open_orders.find(trade.buy_order_id.value);
  const auto open_sell_it = state.open_orders.find(trade.sell_order_id.value);

  std::optional<Order> open_buy_order;
  if (open_buy_it != state.open_orders.end()) {
    open_buy_order = open_buy_it->second;
    RemoveOpenOrderFromSequence(state, trade.buy_order_id.value);
    state.open_orders.erase(open_buy_it);
  }

  if (open_sell_it != state.open_orders.end()) {
    RemoveOpenOrderFromSequence(state, trade.sell_order_id.value);
    state.open_orders.erase(open_sell_it);
  }

  if (!ApplyWalletSettlementForTrade(state, trade, open_buy_order)) {
    return false;
  }

  return ApplyTicketTransfer(state, trade.seller_user_id.value, trade.buyer_user_id.value,
                             trade.event_id.value, trade.category);
}

bool IsTradeEffectEvent(const EventRecord& event) {
  return event.type == event_type::OrderMatched || event.type == event_type::WalletSettled ||
         event.type == event_type::TicketTransferred;
}

bool IsCompleteTradeGroupAt(const EventLog& event_log, std::size_t index) {
  return index + 2 < event_log.size() &&
         event_log[index].type == event_type::OrderMatched &&
         event_log[index + 1].type == event_type::WalletSettled &&
         event_log[index + 2].type == event_type::TicketTransferred;
}

bool IsKnownEventType(std::string_view type) {
  return type == event_type::EventCreated || type == event_type::WalletDeposited ||
         type == event_type::TicketIssued || type == event_type::PrimaryTicketBought ||
         type == event_type::OrderPlaced || type == event_type::OrderCancelled ||
         type == event_type::OrderMatched || type == event_type::WalletSettled ||
         type == event_type::TicketTransferred;
}

std::string EventContext(const EventRecord& event) {
  return "sequence_id=" + std::to_string(event.sequence_id) + " type=" + event.type;
}

bool HasPositiveMoneyField(const nlohmann::json& payload, std::string_view field_name) {
  const std::optional<Money> amount = ExtractMoneyField(payload, field_name);
  return amount.has_value() && *amount > 0;
}

bool HasNonEmptyStringField(const nlohmann::json& payload, std::string_view field_name) {
  const std::optional<std::string> value = ExtractStringField(payload, field_name);
  return value.has_value() && !value->empty();
}

bool IsValidEventPayload(const EventRecord& event, const nlohmann::json& payload) {
  if (event.type == event_type::EventCreated) {
    return ParseEventPayload(payload).has_value();
  }
  if (event.type == event_type::WalletDeposited) {
    return ExtractUintField(payload, "user_id").has_value() &&
           HasPositiveMoneyField(payload, "amount");
  }
  if (event.type == event_type::TicketIssued) {
    return ParseIssuedTicketPayload(payload).has_value();
  }
  if (event.type == event_type::PrimaryTicketBought) {
    return ParsePrimaryTicketPayload(payload).has_value() && HasPositiveMoneyField(payload, "price");
  }
  if (event.type == event_type::OrderPlaced) {
    const std::optional<Order> order = ParseOrderPayload(payload);
    return order.has_value() && order->status == OrderStatus::Open &&
           order->type == OrderType::Limit && order->limit_price.has_value() &&
           *order->limit_price > 0 && !order->category.empty();
  }
  if (event.type == event_type::OrderCancelled) {
    return ExtractUintField(payload, "order_id").has_value();
  }
  if (event.type == event_type::OrderMatched) {
    return ExtractUintField(payload, "buy_order_id").has_value() &&
           ExtractUintField(payload, "sell_order_id").has_value() &&
           ExtractUintField(payload, "buyer_user_id").has_value() &&
           ExtractUintField(payload, "seller_user_id").has_value() &&
           ExtractUintField(payload, "event_id").has_value() &&
           HasNonEmptyStringField(payload, "category") &&
           HasPositiveMoneyField(payload, "price");
  }
  if (event.type == event_type::WalletSettled) {
    return ExtractUintField(payload, "buyer_user_id").has_value() &&
           ExtractUintField(payload, "seller_user_id").has_value() &&
           ExtractUintField(payload, "event_id").has_value() &&
           HasNonEmptyStringField(payload, "category") &&
           HasPositiveMoneyField(payload, "price");
  }
  if (event.type == event_type::TicketTransferred) {
    return ExtractUintField(payload, "buyer_user_id").has_value() &&
           ExtractUintField(payload, "seller_user_id").has_value() &&
           ExtractUintField(payload, "event_id").has_value() &&
           HasNonEmptyStringField(payload, "category");
  }
  return false;
}

bool TradeGroupPayloadsMatch(const nlohmann::json& match_payload,
                             const nlohmann::json& wallet_payload,
                             const nlohmann::json& ticket_payload) {
  const std::optional<std::uint64_t> buyer_user_id =
      ExtractUintField(match_payload, "buyer_user_id");
  const std::optional<std::uint64_t> seller_user_id =
      ExtractUintField(match_payload, "seller_user_id");
  const std::optional<std::uint64_t> event_id = ExtractUintField(match_payload, "event_id");
  const std::optional<std::string> category = ExtractStringField(match_payload, "category");
  const std::optional<Money> price = ExtractMoneyField(match_payload, "price");
  if (!buyer_user_id.has_value() || !seller_user_id.has_value() || !event_id.has_value() ||
      !category.has_value() || !price.has_value()) {
    return false;
  }

  return ExtractUintField(wallet_payload, "buyer_user_id") == buyer_user_id &&
         ExtractUintField(wallet_payload, "seller_user_id") == seller_user_id &&
         ExtractUintField(wallet_payload, "event_id") == event_id &&
         ExtractStringField(wallet_payload, "category") == category &&
         ExtractMoneyField(wallet_payload, "price") == price &&
         ExtractUintField(ticket_payload, "buyer_user_id") == buyer_user_id &&
         ExtractUintField(ticket_payload, "seller_user_id") == seller_user_id &&
         ExtractUintField(ticket_payload, "event_id") == event_id &&
         ExtractStringField(ticket_payload, "category") == category;
}

void AddTransitionError(RecoveryReport& report, const EventRecord& event,
                        std::string detail) {
  report.errors.push_back("invalid state transition at " + EventContext(event) +
                          ": " + std::move(detail));
}

bool HasOpenBuyForEvent(const ReplayState& state, std::uint64_t user_id,
                        std::uint64_t event_id) {
  for (const auto& [unused_order_id, order] : state.open_orders) {
    (void)unused_order_id;
    if (order.side == Side::Buy && order.user_id.value == user_id &&
        order.event_id.value == event_id) {
      return true;
    }
  }
  return false;
}

bool WouldCrossOpenOrder(const ReplayState& state, const Order& order) {
  if (!order.limit_price.has_value()) {
    return false;
  }

  for (const auto& [unused_order_id, resting_order] : state.open_orders) {
    (void)unused_order_id;
    if (resting_order.event_id.value != order.event_id.value ||
        resting_order.category != order.category ||
        resting_order.side == order.side || !resting_order.limit_price.has_value()) {
      continue;
    }

    if (order.side == Side::Buy && *resting_order.limit_price <= *order.limit_price) {
      return true;
    }
    if (order.side == Side::Sell && *resting_order.limit_price >= *order.limit_price) {
      return true;
    }
  }
  return false;
}

bool OpenBuyOrderMatchesTrade(const Order& order, const Trade& trade) {
  return order.side == Side::Buy && order.user_id.value == trade.buyer_user_id.value &&
         order.event_id.value == trade.event_id.value && order.category == trade.category &&
         order.limit_price.has_value() && *order.limit_price == trade.price;
}

bool OpenSellOrderMatchesTrade(const Order& order, const Trade& trade) {
  return order.side == Side::Sell && order.user_id.value == trade.seller_user_id.value &&
         order.event_id.value == trade.event_id.value && order.category == trade.category &&
         order.limit_price.has_value() && *order.limit_price == trade.price;
}

bool ApplyPrimaryTicketBuyStrict(ReplayState& state, const Ticket& ticket, Money price) {
  if (price <= 0) {
    return false;
  }

  const auto event_it = state.events.find(ticket.event_id.value);
  if (event_it == state.events.end()) {
    return false;
  }

  PrimarySaleCategory* category = FindReplayCategory(event_it->second, ticket.category);
  if (category == nullptr || category->price != price || category->remaining == 0) {
    return false;
  }

  const auto wallet_it = state.wallets.find(ticket.owner_user_id.value);
  if (wallet_it == state.wallets.end() ||
      !CanSubtractMoney(wallet_it->second.available, price)) {
    return false;
  }

  wallet_it->second.available -= price;
  --category->remaining;
  return ApplyTicketIssue(state, ticket);
}

bool ApplyOrderPlacedStrict(ReplayState& state, const Order& order) {
  if (state.open_orders.contains(order.id.value) || WouldCrossOpenOrder(state, order)) {
    return false;
  }

  if (order.side == Side::Buy) {
    if (OwnsActiveTicketForEvent(state, order.user_id.value, order.event_id.value) ||
        HasOpenBuyForEvent(state, order.user_id.value, order.event_id.value) ||
        !LockReplayFunds(state, order)) {
      return false;
    }
  } else if (!LockReplayTicketForSell(state, order)) {
    return false;
  }

  state.open_orders.emplace(order.id.value, order);
  state.open_order_sequence.push_back(order.id.value);
  return true;
}

bool ApplyOrderCancelledStrict(ReplayState& state, std::uint64_t order_id) {
  const auto order_it = state.open_orders.find(order_id);
  if (order_it == state.open_orders.end()) {
    return false;
  }

  const Order order = order_it->second;
  if (order.side == Side::Buy) {
    if (!UnlockReplayFunds(state, order)) {
      return false;
    }
  } else if (!UnlockReplayTicketForSell(state, order)) {
    return false;
  }

  RemoveOpenOrderFromSequence(state, order_id);
  state.open_orders.erase(order_it);
  return true;
}

bool ValidateAndApplyTradeGroup(ReplayState& state, const Trade& trade) {
  const auto open_buy_it = state.open_orders.find(trade.buy_order_id.value);
  const auto open_sell_it = state.open_orders.find(trade.sell_order_id.value);
  const bool buy_order_open = open_buy_it != state.open_orders.end();
  const bool sell_order_open = open_sell_it != state.open_orders.end();
  if (buy_order_open == sell_order_open) {
    return false;
  }

  if (buy_order_open && !OpenBuyOrderMatchesTrade(open_buy_it->second, trade)) {
    return false;
  }
  if (sell_order_open && !OpenSellOrderMatchesTrade(open_sell_it->second, trade)) {
    return false;
  }

  ReplayState next_state = state;
  if (!ApplyTradeGroup(next_state, trade)) {
    return false;
  }

  state = std::move(next_state);
  return true;
}

void ValidateStateTransitions(const EventLog& event_log, RecoveryReport& report) {
  ReplayState state{.summary = replay_summary(event_log)};

  for (std::size_t index = 0; index < event_log.size(); ++index) {
    const EventRecord& event = event_log[index];
    if (!IsKnownEventType(event.type)) {
      continue;
    }

    const std::optional<nlohmann::json> payload = ParsePayload(event.payload_json);
    if (!payload.has_value() || !IsValidEventPayload(event, *payload)) {
      continue;
    }

    if (IsCompleteTradeGroupAt(event_log, index)) {
      const std::optional<Trade> trade = ParseTradePayload(*payload);
      if (trade.has_value() && !ValidateAndApplyTradeGroup(state, *trade)) {
        AddTransitionError(report, event, "trade group cannot be applied");
      }
      index += 2;
      continue;
    }

    if (event.type == event_type::EventCreated) {
      const std::optional<Event> created_event = ParseEventPayload(*payload);
      if (created_event.has_value() && !ApplyEventCreated(state, *created_event)) {
        AddTransitionError(report, event, "event already exists");
      }
    } else if (event.type == event_type::WalletDeposited) {
      const std::optional<std::uint64_t> user_id = ExtractUintField(*payload, "user_id");
      const std::optional<Money> amount = ExtractMoneyField(*payload, "amount");
      const Money current_available =
          user_id.has_value() && state.wallets.contains(*user_id)
              ? state.wallets.at(*user_id).available
              : 0;
      if (!user_id.has_value() || !amount.has_value() ||
          !CanAddMoney(current_available, *amount)) {
        AddTransitionError(report, event, "wallet deposit would overflow");
      } else {
        state.wallets[*user_id].available = current_available + *amount;
      }
    } else if (event.type == event_type::TicketIssued) {
      const std::optional<Ticket> ticket = ParseIssuedTicketPayload(*payload);
      if (ticket.has_value()) {
        Ticket normalized_ticket = *ticket;
        if (normalized_ticket.status != TicketStatus::Owned ||
            !ApplyTicketIssue(state, normalized_ticket)) {
          AddTransitionError(report, event, "ticket cannot be issued");
        }
      }
    } else if (event.type == event_type::PrimaryTicketBought) {
      const std::optional<Ticket> ticket = ParsePrimaryTicketPayload(*payload);
      const std::optional<Money> price = ExtractMoneyField(*payload, "price");
      ReplayState next_state = state;
      if (!ticket.has_value() || !price.has_value() ||
          !ApplyPrimaryTicketBuyStrict(next_state, *ticket, *price)) {
        AddTransitionError(report, event, "primary buy cannot be applied");
      } else {
        state = std::move(next_state);
      }
    } else if (event.type == event_type::OrderPlaced) {
      const std::optional<Order> order = ParseOrderPayload(*payload);
      ReplayState next_state = state;
      if (!order.has_value() || !ApplyOrderPlacedStrict(next_state, *order)) {
        AddTransitionError(report, event, "order cannot be opened");
      } else {
        state = std::move(next_state);
      }
    } else if (event.type == event_type::OrderCancelled) {
      const std::optional<std::uint64_t> order_id = ExtractUintField(*payload, "order_id");
      ReplayState next_state = state;
      if (!order_id.has_value() || !ApplyOrderCancelledStrict(next_state, *order_id)) {
        AddTransitionError(report, event, "order cannot be cancelled");
      } else {
        state = std::move(next_state);
      }
    }
  }
}

void ValidateTradeGroupConsistency(const EventLog& event_log, RecoveryReport& report) {
  for (std::size_t i = 0; i < event_log.size(); ++i) {
    if (!IsCompleteTradeGroupAt(event_log, i)) {
      continue;
    }

    const std::optional<nlohmann::json> match_payload =
        ParsePayload(event_log[i].payload_json);
    const std::optional<nlohmann::json> wallet_payload =
        ParsePayload(event_log[i + 1].payload_json);
    const std::optional<nlohmann::json> ticket_payload =
        ParsePayload(event_log[i + 2].payload_json);
    if (!match_payload.has_value() || !wallet_payload.has_value() ||
        !ticket_payload.has_value()) {
      continue;
    }

    if (!TradeGroupPayloadsMatch(*match_payload, *wallet_payload, *ticket_payload)) {
      report.errors.push_back("inconsistent trade group at " +
                              EventContext(event_log[i]));
    }
    i += 2;
  }
}

void UpdateTradeGroupIntegrity(const EventLog& event_log, ReplaySummary& summary) {
  for (std::size_t i = 0; i < event_log.size();) {
    const EventRecord& event = event_log[i];
    if (event.type == event_type::OrderMatched) {
      if (IsCompleteTradeGroupAt(event_log, i)) {
        i += 3;
        continue;
      }
      ++summary.incomplete_trade_group_count;
      summary.trade_groups_complete = false;
      ++i;
      while (i < event_log.size() && event_log[i].type != event_type::OrderMatched &&
             IsTradeEffectEvent(event_log[i])) {
        ++i;
      }
      continue;
    }

    if (event.type == event_type::WalletSettled ||
        event.type == event_type::TicketTransferred) {
      ++summary.incomplete_trade_group_count;
      summary.trade_groups_complete = false;
    }
    ++i;
  }
}

} // namespace

ReplaySummary replay_summary(const EventLog& event_log) {
  ReplaySummary summary{
      .event_count = event_log.size(),
  };

  bool has_previous_sequence = false;
  std::uint64_t previous_sequence_id = 0;
  for (const EventRecord& event : event_log) {
    if (has_previous_sequence && event.sequence_id != previous_sequence_id + 1) {
      summary.sequence_contiguous = false;
    }
    has_previous_sequence = true;
    previous_sequence_id = event.sequence_id;
    summary.last_sequence_id = event.sequence_id;

    if (event.type == event_type::OrderPlaced) {
      ++summary.order_placed_count;
    } else if (event.type == event_type::OrderCancelled) {
      ++summary.order_cancelled_count;
    } else if (event.type == event_type::OrderMatched) {
      ++summary.trade_count;
    } else if (event.type == event_type::WalletSettled) {
      const std::optional<nlohmann::json> payload = ParsePayload(event.payload_json);
      const std::optional<Money> price =
          payload.has_value() ? ExtractMoneyField(*payload, "price") : std::nullopt;
      if (price.has_value() && CanAddMoney(summary.wallet_settled_amount, *price)) {
        summary.wallet_settled_amount += *price;
      }
    } else if (event.type == event_type::TicketTransferred) {
      ++summary.ticket_transfer_count;
    }
  }

  UpdateTradeGroupIntegrity(event_log, summary);
  return summary;
}

ReplayState replay_state(const EventLog& event_log) {
  ReplayState state{.summary = replay_summary(event_log)};

  for (std::size_t index = 0; index < event_log.size(); ++index) {
    const EventRecord& event = event_log[index];
    if (IsCompleteTradeGroupAt(event_log, index)) {
      const std::optional<nlohmann::json> match_payload =
          ParsePayload(event.payload_json);
      const std::optional<Trade> trade =
          match_payload.has_value() ? ParseTradePayload(*match_payload) : std::nullopt;
      if (trade.has_value()) {
        (void)ApplyTradeGroup(state, *trade);
      }
      index += 2;
      continue;
    }

    const std::optional<nlohmann::json> payload = ParsePayload(event.payload_json);
    if (!payload.has_value()) {
      continue;
    }

    if (event.type == event_type::EventCreated) {
      const std::optional<Event> created_event = ParseEventPayload(*payload);
      if (created_event.has_value()) {
        ApplyEventCreated(state, *created_event);
      }
    } else if (event.type == event_type::WalletDeposited) {
      const std::optional<std::uint64_t> user_id = ExtractUintField(*payload, "user_id");
      const std::optional<Money> amount = ExtractMoneyField(*payload, "amount");
      if (user_id.has_value() && amount.has_value()) {
        const auto wallet_it = state.wallets.find(*user_id);
        const Money current_available =
            wallet_it == state.wallets.end() ? 0 : wallet_it->second.available;
        if (CanAddMoney(current_available, *amount)) {
          state.wallets[*user_id].available = current_available + *amount;
        }
      }
    } else if (event.type == event_type::OrderPlaced) {
      const std::optional<Order> order = ParseOrderPayload(*payload);
      if (order.has_value()) {
        if (const auto previous_order_it = state.open_orders.find(order->id.value);
            previous_order_it != state.open_orders.end()) {
          UnlockReplayFunds(state, previous_order_it->second);
          UnlockReplayTicketForSell(state, previous_order_it->second);
          RemoveOpenOrderFromSequence(state, order->id.value);
        }
        state.open_orders[order->id.value] = *order;
        state.open_order_sequence.push_back(order->id.value);
        LockReplayFunds(state, *order);
        LockReplayTicketForSell(state, *order);
      }
    } else if (event.type == event_type::OrderCancelled) {
      const std::optional<std::uint64_t> order_id = ExtractUintField(*payload, "order_id");
      if (order_id.has_value()) {
        const auto order_it = state.open_orders.find(*order_id);
        if (order_it != state.open_orders.end()) {
          UnlockReplayFunds(state, order_it->second);
          UnlockReplayTicketForSell(state, order_it->second);
          RemoveOpenOrderFromSequence(state, *order_id);
          state.open_orders.erase(order_it);
        }
      }
    } else if (event.type == event_type::OrderMatched) {
      const std::optional<std::uint64_t> buy_order_id =
          ExtractUintField(*payload, "buy_order_id");
      const std::optional<std::uint64_t> sell_order_id =
          ExtractUintField(*payload, "sell_order_id");
      if (buy_order_id.has_value()) {
        RemoveOpenOrderFromSequence(state, *buy_order_id);
        state.open_orders.erase(*buy_order_id);
      }
      if (sell_order_id.has_value()) {
        RemoveOpenOrderFromSequence(state, *sell_order_id);
        state.open_orders.erase(*sell_order_id);
      }
    } else if (event.type == event_type::WalletSettled) {
      const std::optional<std::uint64_t> buyer_user_id =
          ExtractUintField(*payload, "buyer_user_id");
      const std::optional<std::uint64_t> seller_user_id =
          ExtractUintField(*payload, "seller_user_id");
      const std::optional<Money> price = ExtractMoneyField(*payload, "price");
      if (buyer_user_id.has_value() && seller_user_id.has_value() && price.has_value()) {
        ApplyWalletSettlement(state, *buyer_user_id, *seller_user_id, *price);
      }
    } else if (event.type == event_type::TicketIssued) {
      const std::optional<Ticket> ticket = ParseIssuedTicketPayload(*payload);
      if (ticket.has_value()) {
        ApplyTicketIssue(state, *ticket);
      }
    } else if (event.type == event_type::PrimaryTicketBought) {
      const std::optional<Ticket> ticket = ParsePrimaryTicketPayload(*payload);
      const std::optional<Money> price = ExtractMoneyField(*payload, "price");
      if (ticket.has_value() && price.has_value()) {
        ApplyPrimaryTicketBuy(state, *ticket, *price);
      }
    } else if (event.type == event_type::TicketTransferred) {
      const std::optional<std::uint64_t> buyer_user_id =
          ExtractUintField(*payload, "buyer_user_id");
      const std::optional<std::uint64_t> seller_user_id =
          ExtractUintField(*payload, "seller_user_id");
      const std::optional<std::uint64_t> event_id = ExtractUintField(*payload, "event_id");
      const std::optional<std::string> category = ExtractStringField(*payload, "category");
      if (buyer_user_id.has_value() && seller_user_id.has_value() && event_id.has_value() &&
          category.has_value()) {
        ApplyTicketTransfer(state, *seller_user_id, *buyer_user_id, *event_id, *category);
      }
    }
  }

  return state;
}

RecoveryReport validate_recovery_log(const EventLog& event_log) {
  RecoveryReport report{
      .ok = true,
      .event_count = event_log.size(),
  };

  std::uint64_t expected_sequence_id = 1;
  for (const EventRecord& event : event_log) {
    if (event.sequence_id != expected_sequence_id) {
      report.errors.push_back("sequence gap or out-of-order event at " + EventContext(event) +
                              "; expected sequence_id=" +
                              std::to_string(expected_sequence_id));
    }
    if (expected_sequence_id < std::numeric_limits<std::uint64_t>::max()) {
      ++expected_sequence_id;
    }

    if (!IsKnownEventType(event.type)) {
      report.errors.push_back("unknown event type at " + EventContext(event));
      continue;
    }

    const std::optional<nlohmann::json> payload = ParsePayload(event.payload_json);
    if (!payload.has_value()) {
      report.errors.push_back("malformed payload at " + EventContext(event));
      continue;
    }
    if (!IsValidEventPayload(event, *payload)) {
      report.errors.push_back("invalid payload shape at " + EventContext(event));
    }
  }

  const ReplaySummary summary = replay_summary(event_log);
  if (!summary.trade_groups_complete) {
    report.errors.push_back("incomplete trade group count=" +
                            std::to_string(summary.incomplete_trade_group_count));
  }
  ValidateTradeGroupConsistency(event_log, report);
  ValidateStateTransitions(event_log, report);

  report.ok = report.errors.empty();
  return report;
}

ReplayLoadResult load_replay_state_from_event_log_file(const std::filesystem::path& path) {
  const std::optional<EventLog> event_log = load_event_log(path);
  if (!event_log.has_value()) {
    return ReplayLoadResult{
        .report =
            RecoveryReport{
                .ok = false,
                .event_count = 0,
                .errors = {"failed to load event log file"},
            },
        .state = std::nullopt,
    };
  }

  RecoveryReport report = validate_recovery_log(*event_log);
  if (!report.ok) {
    return ReplayLoadResult{
        .report = std::move(report),
        .state = std::nullopt,
    };
  }

  return ReplayLoadResult{
      .report = std::move(report),
      .state = replay_state(*event_log),
  };
}
} // namespace ticketx
