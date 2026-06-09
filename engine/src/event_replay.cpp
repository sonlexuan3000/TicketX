#include "ticketx/event_replay.hpp"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

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

void ApplyTicketIssue(ReplayState& state, const Ticket& ticket) {
  if (ticket.category.empty() || !IsActiveTicketStatus(ticket.status) ||
      ticket.credential_version == 0 || state.tickets.contains(ticket.id.value) ||
      OwnsActiveTicketForEvent(state, ticket.owner_user_id.value, ticket.event_id.value)) {
    return;
  }

  state.tickets.emplace(ticket.id.value, ticket);
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

void ApplyTicketTransfer(ReplayState& state, std::uint64_t seller_user_id,
                         std::uint64_t buyer_user_id, std::uint64_t event_id,
                         const std::string& category) {
  if (category.empty() || OwnsActiveTicketForEvent(state, buyer_user_id, event_id)) {
    return;
  }

  auto seller_ticket_it =
      FindActiveTicketForOwnerMarket(state, seller_user_id, event_id, category);
  if (seller_ticket_it == state.tickets.end() ||
      seller_ticket_it->second.credential_version ==
          std::numeric_limits<std::uint64_t>::max()) {
    return;
  }

  Ticket& ticket = seller_ticket_it->second;
  ticket.owner_user_id = UserId{buyer_user_id};
  ticket.status = TicketStatus::Owned;
  ++ticket.credential_version;
}

void LockReplayFunds(ReplayState& state, const Order& order) {
  if (!IsLockableBuyOrder(order)) {
    return;
  }

  const auto wallet_it = state.wallets.find(order.user_id.value);
  if (wallet_it == state.wallets.end()) {
    return;
  }

  ReplayWallet& wallet = wallet_it->second;
  const Money amount = *order.limit_price;
  if (!CanSubtractMoney(wallet.available, amount) || !CanAddMoney(wallet.locked, amount)) {
    return;
  }

  wallet.available -= amount;
  wallet.locked += amount;
}

void UnlockReplayFunds(ReplayState& state, const Order& order) {
  if (!IsLockableBuyOrder(order)) {
    return;
  }

  const auto wallet_it = state.wallets.find(order.user_id.value);
  if (wallet_it == state.wallets.end()) {
    return;
  }

  ReplayWallet& wallet = wallet_it->second;
  const Money amount = *order.limit_price;
  if (!CanSubtractMoney(wallet.locked, amount) || !CanAddMoney(wallet.available, amount)) {
    return;
  }

  wallet.locked -= amount;
  wallet.available += amount;
}

void ApplyWalletSettlement(ReplayState& state, std::uint64_t buyer_user_id,
                           std::uint64_t seller_user_id, Money price) {
  if (price <= 0) {
    return;
  }

  const auto buyer_it = state.wallets.find(buyer_user_id);
  if (buyer_it == state.wallets.end()) {
    return;
  }

  const auto seller_it = state.wallets.find(seller_user_id);
  const Money seller_available =
      seller_it == state.wallets.end() ? 0 : seller_it->second.available;
  if (!CanAddMoney(seller_available, price)) {
    return;
  }

  const bool debit_locked = CanSubtractMoney(buyer_it->second.locked, price);
  const bool debit_available =
      !debit_locked && CanSubtractMoney(buyer_it->second.available, price);
  if (!debit_locked && !debit_available) {
    return;
  }

  ReplayWallet& seller_wallet = state.wallets.try_emplace(seller_user_id).first->second;
  ReplayWallet& buyer_wallet = state.wallets.find(buyer_user_id)->second;
  if (debit_locked) {
    buyer_wallet.locked -= price;
  } else {
    buyer_wallet.available -= price;
  }
  seller_wallet.available += price;
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

  for (const EventRecord& event : event_log) {
    const std::optional<nlohmann::json> payload = ParsePayload(event.payload_json);
    if (!payload.has_value()) {
      continue;
    }

    if (event.type == event_type::WalletDeposited) {
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
        }
        state.open_orders[order->id.value] = *order;
        LockReplayFunds(state, *order);
      }
    } else if (event.type == event_type::OrderCancelled) {
      const std::optional<std::uint64_t> order_id = ExtractUintField(*payload, "order_id");
      if (order_id.has_value()) {
        const auto order_it = state.open_orders.find(*order_id);
        if (order_it != state.open_orders.end()) {
          UnlockReplayFunds(state, order_it->second);
          state.open_orders.erase(order_it);
        }
      }
    } else if (event.type == event_type::OrderMatched) {
      const std::optional<std::uint64_t> buy_order_id =
          ExtractUintField(*payload, "buy_order_id");
      const std::optional<std::uint64_t> sell_order_id =
          ExtractUintField(*payload, "sell_order_id");
      if (buy_order_id.has_value()) {
        state.open_orders.erase(*buy_order_id);
      }
      if (sell_order_id.has_value()) {
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
      if (ticket.has_value()) {
        ApplyTicketIssue(state, *ticket);
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
} // namespace ticketx
