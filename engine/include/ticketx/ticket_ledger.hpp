#pragma once

#include "ticketx/types.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>

namespace ticketx {

enum class TicketStatus {
  Owned,
  LockedForSell,
  Transferred,
  Used,
  Revoked,
};

struct Ticket {
  TicketId id;
  EventId event_id;
  std::string category;
  UserId owner_user_id;
  TicketStatus status{TicketStatus::Owned};
  std::uint64_t credential_version{1};
};

class TicketLedger {
public:
  bool issue_ticket(Ticket ticket);

  std::optional<Ticket> ticket(TicketId ticket_id) const;
  std::optional<Ticket> active_ticket(UserId user_id, EventId event_id) const;
  std::optional<Ticket> unlocked_ticket(UserId user_id, EventId event_id,
                                        const std::string& category) const;
  std::optional<Ticket> locked_ticket(UserId user_id, EventId event_id,
                                      const std::string& category) const;

  bool owns_active_ticket(UserId user_id, EventId event_id) const;

  std::optional<Ticket> lock_ticket(UserId user_id, EventId event_id,
                                    const std::string& category);
  std::optional<Ticket> unlock_ticket(UserId user_id, EventId event_id,
                                      const std::string& category);
  std::optional<Ticket> transfer_ticket(UserId seller_user_id, UserId buyer_user_id,
                                        EventId event_id, const std::string& category);

private:
  struct OwnerEventKey {
    std::uint64_t user_id{};
    std::uint64_t event_id{};

    bool operator==(const OwnerEventKey& other) const {
      return user_id == other.user_id && event_id == other.event_id;
    }
  };

  struct OwnerEventKeyHash {
    std::size_t operator()(const OwnerEventKey& key) const;
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
    std::size_t operator()(const OwnerMarketKey& key) const;
  };

  static OwnerEventKey make_owner_event_key(UserId user_id, EventId event_id);
  static OwnerMarketKey make_owner_market_key(UserId user_id, EventId event_id,
                                              const std::string& category);

  std::unordered_map<std::uint64_t, Ticket> tickets_by_id_;
  std::unordered_map<OwnerEventKey, TicketId, OwnerEventKeyHash> active_by_owner_event_;
  std::unordered_map<OwnerMarketKey, TicketId, OwnerMarketKeyHash> unlocked_by_owner_market_;
  std::unordered_map<OwnerMarketKey, TicketId, OwnerMarketKeyHash> locked_by_owner_market_;
};

} // namespace ticketx
