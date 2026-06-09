#pragma once

#include "ticketx/event_log.hpp"
#include "ticketx/order.hpp"
#include "ticketx/ticket_ledger.hpp"
#include "ticketx/types.hpp"

#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace ticketx {

struct ReplaySummary {
  std::size_t event_count{};
  std::size_t order_placed_count{};
  std::size_t order_cancelled_count{};
  std::size_t trade_count{};
  Money wallet_settled_amount{};
  std::size_t ticket_transfer_count{};
  std::uint64_t last_sequence_id{};
  bool sequence_contiguous{true};
  std::size_t incomplete_trade_group_count{};
  bool trade_groups_complete{true};
};

struct ReplayWallet {
  Money available{};
  Money locked{};
};

struct ReplayState {
  ReplaySummary summary;
  std::unordered_map<std::uint64_t, ReplayWallet> wallets;
  std::unordered_map<std::uint64_t, Order> open_orders;
  std::unordered_map<std::uint64_t, Ticket> tickets;
};

ReplaySummary replay_summary(const EventLog& event_log);
ReplayState replay_state(const EventLog& event_log);

} // namespace ticketx
