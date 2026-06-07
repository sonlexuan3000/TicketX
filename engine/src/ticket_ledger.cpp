#include "ticketx/ticket_ledger.hpp"

#include <limits>
#include <utility>

namespace ticketx {

namespace {

bool IsActive(TicketStatus status) {
  return status == TicketStatus::Owned || status == TicketStatus::LockedForSell;
}

std::size_t HashCombine(std::size_t seed, std::size_t value) {
  return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
}

} // namespace

std::size_t TicketLedger::OwnerEventKeyHash::operator()(const OwnerEventKey& key) const {
  std::size_t seed = std::hash<std::uint64_t>{}(key.user_id);
  seed = HashCombine(seed, std::hash<std::uint64_t>{}(key.event_id));
  return seed;
}

std::size_t TicketLedger::OwnerMarketKeyHash::operator()(const OwnerMarketKey& key) const {
  std::size_t seed = std::hash<std::uint64_t>{}(key.user_id);
  seed = HashCombine(seed, std::hash<std::uint64_t>{}(key.event_id));
  seed = HashCombine(seed, std::hash<std::string>{}(key.category));
  return seed;
}

TicketLedger::OwnerEventKey TicketLedger::make_owner_event_key(UserId user_id,
                                                               EventId event_id) {
  return OwnerEventKey{
      .user_id = user_id.value,
      .event_id = event_id.value,
  };
}

TicketLedger::OwnerMarketKey TicketLedger::make_owner_market_key(
    UserId user_id, EventId event_id, const std::string& category) {
  return OwnerMarketKey{
      .user_id = user_id.value,
      .event_id = event_id.value,
      .category = category,
  };
}

bool TicketLedger::issue_ticket(Ticket ticket) {
  if (ticket.category.empty() || ticket.status != TicketStatus::Owned) {
    return false;
  }
  if (tickets_by_id_.contains(ticket.id.value)) {
    return false;
  }

  const OwnerEventKey owner_event_key =
      make_owner_event_key(ticket.owner_user_id, ticket.event_id);
  if (active_by_owner_event_.contains(owner_event_key)) {
    return false;
  }

  const OwnerMarketKey owner_market_key =
      make_owner_market_key(ticket.owner_user_id, ticket.event_id, ticket.category);
  const TicketId ticket_id = ticket.id;
  tickets_by_id_.emplace(ticket_id.value, std::move(ticket));
  active_by_owner_event_.emplace(owner_event_key, ticket_id);
  unlocked_by_owner_market_.emplace(owner_market_key, ticket_id);
  return true;
}

std::optional<Ticket> TicketLedger::ticket(TicketId ticket_id) const {
  const auto it = tickets_by_id_.find(ticket_id.value);
  if (it == tickets_by_id_.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::optional<Ticket> TicketLedger::active_ticket(UserId user_id, EventId event_id) const {
  const auto index_it = active_by_owner_event_.find(make_owner_event_key(user_id, event_id));
  if (index_it == active_by_owner_event_.end()) {
    return std::nullopt;
  }

  return ticket(index_it->second);
}

std::optional<Ticket> TicketLedger::unlocked_ticket(UserId user_id, EventId event_id,
                                                    const std::string& category) const {
  const auto index_it =
      unlocked_by_owner_market_.find(make_owner_market_key(user_id, event_id, category));
  if (index_it == unlocked_by_owner_market_.end()) {
    return std::nullopt;
  }

  return ticket(index_it->second);
}

std::optional<Ticket> TicketLedger::locked_ticket(UserId user_id, EventId event_id,
                                                  const std::string& category) const {
  const auto index_it =
      locked_by_owner_market_.find(make_owner_market_key(user_id, event_id, category));
  if (index_it == locked_by_owner_market_.end()) {
    return std::nullopt;
  }

  return ticket(index_it->second);
}

bool TicketLedger::owns_active_ticket(UserId user_id, EventId event_id) const {
  return active_by_owner_event_.contains(make_owner_event_key(user_id, event_id));
}

std::optional<Ticket> TicketLedger::lock_ticket(UserId user_id, EventId event_id,
                                                const std::string& category) {
  if (category.empty()) {
    return std::nullopt;
  }

  const OwnerMarketKey owner_market_key = make_owner_market_key(user_id, event_id, category);
  const auto index_it = unlocked_by_owner_market_.find(owner_market_key);
  if (index_it == unlocked_by_owner_market_.end()) {
    return std::nullopt;
  }

  auto ticket_it = tickets_by_id_.find(index_it->second.value);
  if (ticket_it == tickets_by_id_.end() || ticket_it->second.status != TicketStatus::Owned) {
    return std::nullopt;
  }

  ticket_it->second.status = TicketStatus::LockedForSell;
  unlocked_by_owner_market_.erase(index_it);
  locked_by_owner_market_.emplace(owner_market_key, ticket_it->second.id);
  return ticket_it->second;
}

std::optional<Ticket> TicketLedger::unlock_ticket(UserId user_id, EventId event_id,
                                                  const std::string& category) {
  if (category.empty()) {
    return std::nullopt;
  }

  const OwnerMarketKey owner_market_key = make_owner_market_key(user_id, event_id, category);
  const auto index_it = locked_by_owner_market_.find(owner_market_key);
  if (index_it == locked_by_owner_market_.end()) {
    return std::nullopt;
  }

  auto ticket_it = tickets_by_id_.find(index_it->second.value);
  if (ticket_it == tickets_by_id_.end() ||
      ticket_it->second.status != TicketStatus::LockedForSell) {
    return std::nullopt;
  }

  ticket_it->second.status = TicketStatus::Owned;
  locked_by_owner_market_.erase(index_it);
  unlocked_by_owner_market_.emplace(owner_market_key, ticket_it->second.id);
  return ticket_it->second;
}

std::optional<Ticket> TicketLedger::transfer_ticket(UserId seller_user_id,
                                                    UserId buyer_user_id, EventId event_id,
                                                    const std::string& category) {
  if (category.empty() || owns_active_ticket(buyer_user_id, event_id)) {
    return std::nullopt;
  }

  const OwnerMarketKey seller_market_key =
      make_owner_market_key(seller_user_id, event_id, category);
  auto locked_index_it = locked_by_owner_market_.find(seller_market_key);
  auto unlocked_index_it = unlocked_by_owner_market_.find(seller_market_key);
  if (locked_index_it == locked_by_owner_market_.end() &&
      unlocked_index_it == unlocked_by_owner_market_.end()) {
    return std::nullopt;
  }

  const TicketId ticket_id =
      locked_index_it != locked_by_owner_market_.end() ? locked_index_it->second
                                                       : unlocked_index_it->second;
  auto ticket_it = tickets_by_id_.find(ticket_id.value);
  if (ticket_it == tickets_by_id_.end() || !IsActive(ticket_it->second.status)) {
    return std::nullopt;
  }
  if (ticket_it->second.credential_version == std::numeric_limits<std::uint64_t>::max()) {
    return std::nullopt;
  }

  active_by_owner_event_.erase(make_owner_event_key(seller_user_id, event_id));
  if (locked_index_it != locked_by_owner_market_.end()) {
    locked_by_owner_market_.erase(locked_index_it);
  } else {
    unlocked_by_owner_market_.erase(unlocked_index_it);
  }

  ticket_it->second.owner_user_id = buyer_user_id;
  ticket_it->second.status = TicketStatus::Owned;
  ++ticket_it->second.credential_version;

  active_by_owner_event_.emplace(make_owner_event_key(buyer_user_id, event_id),
                                 ticket_it->second.id);
  unlocked_by_owner_market_.emplace(
      make_owner_market_key(buyer_user_id, event_id, ticket_it->second.category),
      ticket_it->second.id);
  return ticket_it->second;
}

} // namespace ticketx
