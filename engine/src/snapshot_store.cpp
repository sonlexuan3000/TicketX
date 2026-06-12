#include "ticketx/snapshot_store.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace ticketx {

namespace {

std::string_view SideName(Side side) {
  switch (side) {
  case Side::Buy:
    return "Buy";
  case Side::Sell:
    return "Sell";
  }
  return "Unknown";
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

std::string_view OrderTypeName(OrderType type) {
  switch (type) {
  case OrderType::Limit:
    return "Limit";
  case OrderType::Market:
    return "Market";
  }
  return "Unknown";
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

std::optional<std::uint64_t> ExtractUintField(const nlohmann::json& object,
                                              std::string_view field_name) {
  const auto it = object.find(std::string{field_name});
  if (it == object.end()) {
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

std::optional<std::size_t> ExtractSizeField(const nlohmann::json& object,
                                            std::string_view field_name) {
  const std::optional<std::uint64_t> value = ExtractUintField(object, field_name);
  if (!value.has_value() ||
      *value > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(*value);
}

std::optional<Money> ExtractMoneyField(const nlohmann::json& object,
                                       std::string_view field_name) {
  const auto it = object.find(std::string{field_name});
  if (it == object.end()) {
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

std::optional<std::string> ExtractStringField(const nlohmann::json& object,
                                              std::string_view field_name) {
  const auto it = object.find(std::string{field_name});
  if (it == object.end() || !it->is_string()) {
    return std::nullopt;
  }
  return it->get<std::string>();
}

std::optional<bool> ExtractBoolField(const nlohmann::json& object,
                                     std::string_view field_name) {
  const auto it = object.find(std::string{field_name});
  if (it == object.end() || !it->is_boolean()) {
    return std::nullopt;
  }
  return it->get<bool>();
}

bool CanAddMoney(Money current, Money amount) {
  return amount >= 0 && current <= std::numeric_limits<Money>::max() - amount;
}

std::size_t HashCombine(std::size_t seed, std::size_t value) {
  return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U));
}

struct OwnerEventKey {
  std::uint64_t user_id{};
  std::uint64_t event_id{};

  bool operator==(const OwnerEventKey& other) const {
    return user_id == other.user_id && event_id == other.event_id;
  }
};

struct OwnerEventKeyHash {
  std::size_t operator()(const OwnerEventKey& key) const {
    std::size_t seed = std::hash<std::uint64_t>{}(key.user_id);
    return HashCombine(seed, std::hash<std::uint64_t>{}(key.event_id));
  }
};

struct OwnerMarketKey {
  std::uint64_t user_id{};
  std::uint64_t event_id{};
  std::string category;

  bool operator==(const OwnerMarketKey& other) const {
    return user_id == other.user_id && event_id == other.event_id &&
           category == other.category;
  }
};

struct OwnerMarketKeyHash {
  std::size_t operator()(const OwnerMarketKey& key) const {
    std::size_t seed = std::hash<std::uint64_t>{}(key.user_id);
    seed = HashCombine(seed, std::hash<std::uint64_t>{}(key.event_id));
    return HashCombine(seed, std::hash<std::string>{}(key.category));
  }
};

nlohmann::json ReplaySummaryToJson(const ReplaySummary& summary) {
  return nlohmann::json{
      {"event_count", summary.event_count},
      {"order_placed_count", summary.order_placed_count},
      {"order_cancelled_count", summary.order_cancelled_count},
      {"trade_count", summary.trade_count},
      {"wallet_settled_amount", summary.wallet_settled_amount},
      {"ticket_transfer_count", summary.ticket_transfer_count},
      {"last_sequence_id", summary.last_sequence_id},
      {"sequence_contiguous", summary.sequence_contiguous},
      {"incomplete_trade_group_count", summary.incomplete_trade_group_count},
      {"trade_groups_complete", summary.trade_groups_complete},
  };
}

std::optional<ReplaySummary> ParseReplaySummary(const nlohmann::json& object) {
  if (!object.is_object()) {
    return std::nullopt;
  }

  const std::optional<std::size_t> event_count = ExtractSizeField(object, "event_count");
  const std::optional<std::size_t> order_placed_count =
      ExtractSizeField(object, "order_placed_count");
  const std::optional<std::size_t> order_cancelled_count =
      ExtractSizeField(object, "order_cancelled_count");
  const std::optional<std::size_t> trade_count = ExtractSizeField(object, "trade_count");
  const std::optional<Money> wallet_settled_amount =
      ExtractMoneyField(object, "wallet_settled_amount");
  const std::optional<std::size_t> ticket_transfer_count =
      ExtractSizeField(object, "ticket_transfer_count");
  const std::optional<std::uint64_t> last_sequence_id =
      ExtractUintField(object, "last_sequence_id");
  const std::optional<bool> sequence_contiguous =
      ExtractBoolField(object, "sequence_contiguous");
  const std::optional<std::size_t> incomplete_trade_group_count =
      ExtractSizeField(object, "incomplete_trade_group_count");
  const std::optional<bool> trade_groups_complete =
      ExtractBoolField(object, "trade_groups_complete");
  if (!event_count.has_value() || !order_placed_count.has_value() ||
      !order_cancelled_count.has_value() || !trade_count.has_value() ||
      !wallet_settled_amount.has_value() || !ticket_transfer_count.has_value() ||
      !last_sequence_id.has_value() || !sequence_contiguous.has_value() ||
      !incomplete_trade_group_count.has_value() || !trade_groups_complete.has_value()) {
    return std::nullopt;
  }

  return ReplaySummary{
      .event_count = *event_count,
      .order_placed_count = *order_placed_count,
      .order_cancelled_count = *order_cancelled_count,
      .trade_count = *trade_count,
      .wallet_settled_amount = *wallet_settled_amount,
      .ticket_transfer_count = *ticket_transfer_count,
      .last_sequence_id = *last_sequence_id,
      .sequence_contiguous = *sequence_contiguous,
      .incomplete_trade_group_count = *incomplete_trade_group_count,
      .trade_groups_complete = *trade_groups_complete,
  };
}

nlohmann::json CategoryToJson(const PrimarySaleCategory& category) {
  return nlohmann::json{
      {"name", category.name},
      {"price", category.price},
      {"remaining", category.remaining},
  };
}

std::optional<PrimarySaleCategory> ParseCategory(const nlohmann::json& object) {
  if (!object.is_object()) {
    return std::nullopt;
  }

  const std::optional<std::string> name = ExtractStringField(object, "name");
  const std::optional<Money> price = ExtractMoneyField(object, "price");
  const std::optional<std::uint64_t> remaining = ExtractUintField(object, "remaining");
  if (!name.has_value() || name->empty() || !price.has_value() || *price <= 0 ||
      !remaining.has_value()) {
    return std::nullopt;
  }

  return PrimarySaleCategory{
      .name = *name,
      .price = *price,
      .remaining = *remaining,
  };
}

nlohmann::json EventToJson(const Event& event) {
  nlohmann::json categories = nlohmann::json::array();
  for (const PrimarySaleCategory& category : event.categories) {
    categories.push_back(CategoryToJson(category));
  }

  return nlohmann::json{
      {"event_id", event.id.value},
      {"name", event.name},
      {"categories", std::move(categories)},
  };
}

std::optional<Event> ParseEvent(const nlohmann::json& object) {
  if (!object.is_object()) {
    return std::nullopt;
  }

  const std::optional<std::uint64_t> event_id = ExtractUintField(object, "event_id");
  const std::optional<std::string> name = ExtractStringField(object, "name");
  const auto categories_it = object.find("categories");
  if (!event_id.has_value() || !name.has_value() || name->empty() ||
      categories_it == object.end() || !categories_it->is_array() ||
      categories_it->empty()) {
    return std::nullopt;
  }

  std::unordered_set<std::string> category_names;
  std::vector<PrimarySaleCategory> categories;
  categories.reserve(categories_it->size());
  for (const nlohmann::json& category_json : *categories_it) {
    const std::optional<PrimarySaleCategory> category = ParseCategory(category_json);
    if (!category.has_value() || !category_names.insert(category->name).second) {
      return std::nullopt;
    }
    categories.push_back(*category);
  }

  return Event{
      .id = EventId{*event_id},
      .name = *name,
      .categories = std::move(categories),
  };
}

nlohmann::json WalletToJson(std::uint64_t user_id, const ReplayWallet& wallet) {
  return nlohmann::json{
      {"user_id", user_id},
      {"available", wallet.available},
      {"locked", wallet.locked},
  };
}

std::optional<std::pair<std::uint64_t, ReplayWallet>> ParseWallet(
    const nlohmann::json& object) {
  if (!object.is_object()) {
    return std::nullopt;
  }

  const std::optional<std::uint64_t> user_id = ExtractUintField(object, "user_id");
  const std::optional<Money> available = ExtractMoneyField(object, "available");
  const std::optional<Money> locked = ExtractMoneyField(object, "locked");
  if (!user_id.has_value() || !available.has_value() || !locked.has_value() ||
      *available < 0 || *locked < 0) {
    return std::nullopt;
  }

  return std::pair<std::uint64_t, ReplayWallet>{
      *user_id,
      ReplayWallet{
          .available = *available,
          .locked = *locked,
      },
  };
}

nlohmann::json OrderToJson(const Order& order) {
  return nlohmann::json{
      {"order_id", order.id.value},
      {"user_id", order.user_id.value},
      {"event_id", order.event_id.value},
      {"category", order.category},
      {"side", std::string{SideName(order.side)}},
      {"type", std::string{OrderTypeName(order.type)}},
      {"limit_price", order.limit_price.has_value() ? nlohmann::json(*order.limit_price)
                                                     : nlohmann::json{nullptr}},
      {"status", std::string{OrderStatusName(order.status)}},
  };
}

std::optional<Order> ParseOrder(const nlohmann::json& object) {
  if (!object.is_object()) {
    return std::nullopt;
  }

  const std::optional<std::uint64_t> order_id = ExtractUintField(object, "order_id");
  const std::optional<std::uint64_t> user_id = ExtractUintField(object, "user_id");
  const std::optional<std::uint64_t> event_id = ExtractUintField(object, "event_id");
  const std::optional<std::string> category = ExtractStringField(object, "category");
  const std::optional<std::string> side_name = ExtractStringField(object, "side");
  const std::optional<std::string> type_name = ExtractStringField(object, "type");
  const std::optional<Money> limit_price = ExtractMoneyField(object, "limit_price");
  const std::optional<std::string> status_name = ExtractStringField(object, "status");
  if (!order_id.has_value() || !user_id.has_value() || !event_id.has_value() ||
      !category.has_value() || category->empty() || !side_name.has_value() ||
      !type_name.has_value() || !limit_price.has_value() || *limit_price <= 0 ||
      !status_name.has_value()) {
    return std::nullopt;
  }

  const std::optional<Side> side = ParseSide(*side_name);
  const std::optional<OrderType> type = ParseOrderType(*type_name);
  const std::optional<OrderStatus> status = ParseOrderStatus(*status_name);
  if (!side.has_value() || !type.has_value() || !status.has_value() ||
      *type != OrderType::Limit || *status != OrderStatus::Open) {
    return std::nullopt;
  }

  return Order{
      .id = OrderId{*order_id},
      .user_id = UserId{*user_id},
      .event_id = EventId{*event_id},
      .category = *category,
      .side = *side,
      .type = *type,
      .limit_price = *limit_price,
      .status = *status,
  };
}

nlohmann::json TicketToJson(const Ticket& ticket) {
  return nlohmann::json{
      {"ticket_id", ticket.id.value},
      {"event_id", ticket.event_id.value},
      {"category", ticket.category},
      {"owner_user_id", ticket.owner_user_id.value},
      {"status", std::string{TicketStatusName(ticket.status)}},
      {"credential_version", ticket.credential_version},
  };
}

std::optional<Ticket> ParseTicket(const nlohmann::json& object) {
  if (!object.is_object()) {
    return std::nullopt;
  }

  const std::optional<std::uint64_t> ticket_id = ExtractUintField(object, "ticket_id");
  const std::optional<std::uint64_t> event_id = ExtractUintField(object, "event_id");
  const std::optional<std::string> category = ExtractStringField(object, "category");
  const std::optional<std::uint64_t> owner_user_id =
      ExtractUintField(object, "owner_user_id");
  const std::optional<std::string> status_name = ExtractStringField(object, "status");
  const std::optional<std::uint64_t> credential_version =
      ExtractUintField(object, "credential_version");
  if (!ticket_id.has_value() || !event_id.has_value() || !category.has_value() ||
      category->empty() || !owner_user_id.has_value() || !status_name.has_value() ||
      !credential_version.has_value() || *credential_version == 0) {
    return std::nullopt;
  }

  const std::optional<TicketStatus> status = ParseTicketStatus(*status_name);
  if (!status.has_value() ||
      (*status != TicketStatus::Owned && *status != TicketStatus::LockedForSell)) {
    return std::nullopt;
  }

  return Ticket{
      .id = TicketId{*ticket_id},
      .event_id = EventId{*event_id},
      .category = *category,
      .owner_user_id = UserId{*owner_user_id},
      .status = *status,
      .credential_version = *credential_version,
  };
}

template <typename Map>
std::vector<std::uint64_t> SortedKeys(const Map& map) {
  std::vector<std::uint64_t> keys;
  keys.reserve(map.size());
  for (const auto& [key, unused_value] : map) {
    (void)unused_value;
    keys.push_back(key);
  }
  std::sort(keys.begin(), keys.end());
  return keys;
}

nlohmann::json ReplayStateToJson(const ReplayState& state) {
  nlohmann::json events = nlohmann::json::array();
  for (const std::uint64_t event_id : SortedKeys(state.events)) {
    events.push_back(EventToJson(state.events.at(event_id)));
  }

  nlohmann::json wallets = nlohmann::json::array();
  for (const std::uint64_t user_id : SortedKeys(state.wallets)) {
    wallets.push_back(WalletToJson(user_id, state.wallets.at(user_id)));
  }

  nlohmann::json open_orders = nlohmann::json::array();
  for (const std::uint64_t order_id : SortedKeys(state.open_orders)) {
    open_orders.push_back(OrderToJson(state.open_orders.at(order_id)));
  }

  nlohmann::json tickets = nlohmann::json::array();
  for (const std::uint64_t ticket_id : SortedKeys(state.tickets)) {
    tickets.push_back(TicketToJson(state.tickets.at(ticket_id)));
  }

  return nlohmann::json{
      {"summary", ReplaySummaryToJson(state.summary)},
      {"events", std::move(events)},
      {"wallets", std::move(wallets)},
      {"open_orders", std::move(open_orders)},
      {"open_order_sequence", state.open_order_sequence},
      {"tickets", std::move(tickets)},
      {"max_ticket_id", state.max_ticket_id},
  };
}

std::optional<ReplayState> ParseReplayState(const nlohmann::json& object) {
  if (!object.is_object()) {
    return std::nullopt;
  }

  const auto summary_it = object.find("summary");
  const auto events_it = object.find("events");
  const auto wallets_it = object.find("wallets");
  const auto open_orders_it = object.find("open_orders");
  const auto open_order_sequence_it = object.find("open_order_sequence");
  const auto tickets_it = object.find("tickets");
  const std::optional<std::uint64_t> max_ticket_id =
      ExtractUintField(object, "max_ticket_id");
  if (summary_it == object.end() || events_it == object.end() ||
      wallets_it == object.end() || open_orders_it == object.end() ||
      open_order_sequence_it == object.end() || tickets_it == object.end() ||
      !events_it->is_array() || !wallets_it->is_array() ||
      !open_orders_it->is_array() || !open_order_sequence_it->is_array() ||
      !tickets_it->is_array() || !max_ticket_id.has_value()) {
    return std::nullopt;
  }

  std::optional<ReplaySummary> summary = ParseReplaySummary(*summary_it);
  if (!summary.has_value()) {
    return std::nullopt;
  }

  ReplayState state{
      .summary = *summary,
      .max_ticket_id = *max_ticket_id,
  };

  for (const nlohmann::json& event_json : *events_it) {
    const std::optional<Event> event = ParseEvent(event_json);
    if (!event.has_value() ||
        !state.events.emplace(event->id.value, *event).second) {
      return std::nullopt;
    }
  }

  for (const nlohmann::json& wallet_json : *wallets_it) {
    const std::optional<std::pair<std::uint64_t, ReplayWallet>> wallet =
        ParseWallet(wallet_json);
    if (!wallet.has_value() || !state.wallets.emplace(wallet->first, wallet->second).second) {
      return std::nullopt;
    }
  }

  for (const nlohmann::json& order_json : *open_orders_it) {
    const std::optional<Order> order = ParseOrder(order_json);
    if (!order.has_value() ||
        !state.open_orders.emplace(order->id.value, *order).second) {
      return std::nullopt;
    }
  }

  std::unordered_set<std::uint64_t> sequenced_order_ids;
  state.open_order_sequence.reserve(open_order_sequence_it->size());
  for (const nlohmann::json& order_id_json : *open_order_sequence_it) {
    if (!order_id_json.is_number_unsigned() && !order_id_json.is_number_integer()) {
      return std::nullopt;
    }
    const std::optional<std::uint64_t> order_id =
        order_id_json.is_number_unsigned()
            ? std::optional<std::uint64_t>{order_id_json.get<std::uint64_t>()}
            : (order_id_json.get<std::int64_t>() >= 0
                   ? std::optional<std::uint64_t>{
                         static_cast<std::uint64_t>(order_id_json.get<std::int64_t>())}
                   : std::nullopt);
    if (!order_id.has_value() || !state.open_orders.contains(*order_id) ||
        !sequenced_order_ids.insert(*order_id).second) {
      return std::nullopt;
    }
    state.open_order_sequence.push_back(*order_id);
  }
  if (state.open_order_sequence.size() != state.open_orders.size()) {
    return std::nullopt;
  }

  for (const nlohmann::json& ticket_json : *tickets_it) {
    const std::optional<Ticket> ticket = ParseTicket(ticket_json);
    if (!ticket.has_value() || ticket->id.value > state.max_ticket_id ||
        !state.tickets.emplace(ticket->id.value, *ticket).second) {
      return std::nullopt;
    }
  }

  return state;
}

bool OrdersWouldCross(const Order& lhs, const Order& rhs) {
  if (lhs.event_id.value != rhs.event_id.value || lhs.category != rhs.category ||
      lhs.side == rhs.side || !lhs.limit_price.has_value() || !rhs.limit_price.has_value()) {
    return false;
  }

  const Order& buy_order = lhs.side == Side::Buy ? lhs : rhs;
  const Order& sell_order = lhs.side == Side::Sell ? lhs : rhs;
  return *buy_order.limit_price >= *sell_order.limit_price;
}

std::optional<TicketStatus> TicketStatusForOwnerMarket(
    const ReplayState& state, const OwnerMarketKey& key) {
  for (const auto& [unused_ticket_id, ticket] : state.tickets) {
    (void)unused_ticket_id;
    if (ticket.owner_user_id.value == key.user_id &&
        ticket.event_id.value == key.event_id && ticket.category == key.category) {
      return ticket.status;
    }
  }
  return std::nullopt;
}

bool ValidateReplayStateInvariants(const ReplayState& state) {
  if (!state.summary.sequence_contiguous || !state.summary.trade_groups_complete) {
    return false;
  }
  if (state.summary.wallet_settled_amount < 0 ||
      state.summary.last_sequence_id == std::numeric_limits<std::uint64_t>::max() ||
      state.summary.last_sequence_id >
          static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
      state.summary.event_count != static_cast<std::size_t>(state.summary.last_sequence_id) ||
      state.summary.order_placed_count > state.summary.event_count ||
      state.summary.order_cancelled_count > state.summary.event_count ||
      state.summary.trade_count > state.summary.event_count ||
      state.summary.ticket_transfer_count > state.summary.event_count ||
      state.summary.incomplete_trade_group_count != 0) {
    return false;
  }

  for (const auto& [unused_user_id, wallet] : state.wallets) {
    (void)unused_user_id;
    if (wallet.available < 0 || wallet.locked < 0 ||
        !CanAddMoney(wallet.available, wallet.locked)) {
      return false;
    }
  }

  std::uint64_t max_ticket_id = 0;
  std::unordered_set<OwnerEventKey, OwnerEventKeyHash> active_tickets;
  std::unordered_set<OwnerMarketKey, OwnerMarketKeyHash> locked_tickets;
  for (const auto& [ticket_id, ticket] : state.tickets) {
    if (ticket.id.value != ticket_id || ticket.category.empty() ||
        ticket.credential_version == 0 ||
        (ticket.status != TicketStatus::Owned &&
         ticket.status != TicketStatus::LockedForSell)) {
      return false;
    }

    max_ticket_id = std::max(max_ticket_id, ticket_id);
    if (!active_tickets
             .insert(OwnerEventKey{
                 .user_id = ticket.owner_user_id.value,
                 .event_id = ticket.event_id.value,
             })
             .second) {
      return false;
    }

    if (ticket.status == TicketStatus::LockedForSell) {
      locked_tickets.insert(OwnerMarketKey{
          .user_id = ticket.owner_user_id.value,
          .event_id = ticket.event_id.value,
          .category = ticket.category,
      });
    }
  }
  if (max_ticket_id != state.max_ticket_id) {
    return false;
  }

  std::unordered_map<std::uint64_t, Money> expected_locked_by_user;
  std::unordered_set<OwnerEventKey, OwnerEventKeyHash> open_buys;
  std::unordered_set<OwnerMarketKey, OwnerMarketKeyHash> open_sells;
  std::vector<Order> open_orders;
  open_orders.reserve(state.open_orders.size());
  for (const auto& [order_id, order] : state.open_orders) {
    if (order.id.value != order_id || order.status != OrderStatus::Open ||
        order.type != OrderType::Limit || !order.limit_price.has_value() ||
        *order.limit_price <= 0 || order.category.empty()) {
      return false;
    }

    if (order.side == Side::Buy) {
      const OwnerEventKey owner_event_key{
          .user_id = order.user_id.value,
          .event_id = order.event_id.value,
      };
      if (active_tickets.contains(owner_event_key) ||
          !open_buys.insert(owner_event_key).second) {
        return false;
      }

      Money& expected_locked = expected_locked_by_user[order.user_id.value];
      if (!CanAddMoney(expected_locked, *order.limit_price)) {
        return false;
      }
      expected_locked += *order.limit_price;
    } else {
      const OwnerMarketKey owner_market_key{
          .user_id = order.user_id.value,
          .event_id = order.event_id.value,
          .category = order.category,
      };
      const std::optional<TicketStatus> ticket_status =
          TicketStatusForOwnerMarket(state, owner_market_key);
      if (!ticket_status.has_value() ||
          *ticket_status != TicketStatus::LockedForSell ||
          !open_sells.insert(owner_market_key).second) {
        return false;
      }
    }

    for (const Order& existing_order : open_orders) {
      if (OrdersWouldCross(existing_order, order)) {
        return false;
      }
    }
    open_orders.push_back(order);
  }

  for (const auto& [user_id, wallet] : state.wallets) {
    const auto expected_it = expected_locked_by_user.find(user_id);
    const Money expected_locked =
        expected_it == expected_locked_by_user.end() ? 0 : expected_it->second;
    if (wallet.locked != expected_locked) {
      return false;
    }
  }
  for (const auto& [user_id, expected_locked] : expected_locked_by_user) {
    if (expected_locked > 0 && !state.wallets.contains(user_id)) {
      return false;
    }
  }

  for (const OwnerMarketKey& locked_ticket : locked_tickets) {
    if (!open_sells.contains(locked_ticket)) {
      return false;
    }
  }

  return true;
}

nlohmann::json SnapshotToJson(const Snapshot& snapshot) {
  return nlohmann::json{
      {"last_sequence_id", snapshot.last_sequence_id},
      {"state", ReplayStateToJson(snapshot.state)},
  };
}

std::filesystem::path TemporarySnapshotPath(const std::filesystem::path& path) {
  std::filesystem::path temporary_path = path;
  temporary_path += ".tmp";
  return temporary_path;
}

std::optional<Snapshot> ParseSnapshot(const nlohmann::json& object) {
  if (!object.is_object()) {
    return std::nullopt;
  }

  const std::optional<std::uint64_t> last_sequence_id =
      ExtractUintField(object, "last_sequence_id");
  const auto state_it = object.find("state");
  if (!last_sequence_id.has_value() || state_it == object.end()) {
    return std::nullopt;
  }

  std::optional<ReplayState> state = ParseReplayState(*state_it);
  if (!state.has_value() || state->summary.last_sequence_id != *last_sequence_id ||
      !ValidateReplayStateInvariants(*state)) {
    return std::nullopt;
  }

  return Snapshot{
      .last_sequence_id = *last_sequence_id,
      .state = std::move(*state),
  };
}

} // namespace

bool save_snapshot(const std::filesystem::path& path, const Snapshot& snapshot) {
  if (snapshot.state.summary.last_sequence_id != snapshot.last_sequence_id ||
      !ValidateReplayStateInvariants(snapshot.state)) {
    return false;
  }
  if (path.empty()) {
    return false;
  }

  const std::filesystem::path temporary_path = TemporarySnapshotPath(path);
  std::error_code ignored_error;
  std::filesystem::remove(temporary_path, ignored_error);

  {
    std::ofstream output{temporary_path, std::ios::out | std::ios::trunc};
    if (!output.is_open()) {
      return false;
    }

    output << SnapshotToJson(snapshot).dump(2) << '\n';
    output.flush();
    output.close();
    if (!output.good()) {
      std::filesystem::remove(temporary_path, ignored_error);
      return false;
    }
  }

  std::error_code rename_error;
  std::filesystem::rename(temporary_path, path, rename_error);
  if (rename_error) {
    std::filesystem::remove(temporary_path, ignored_error);
    return false;
  }
  return true;
}

std::optional<Snapshot> load_snapshot(const std::filesystem::path& path) {
  std::ifstream input{path};
  if (!input.is_open()) {
    return std::nullopt;
  }

  const nlohmann::json object = nlohmann::json::parse(input, nullptr, false);
  if (object.is_discarded()) {
    return std::nullopt;
  }
  return ParseSnapshot(object);
}

} // namespace ticketx
