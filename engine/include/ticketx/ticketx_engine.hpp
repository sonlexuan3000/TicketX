#pragma once

#include "ticketx/async_event_writer.hpp"
#include "ticketx/event.hpp"
#include "ticketx/event_log.hpp"
#include "ticketx/matching_engine.hpp"
#include "ticketx/ticket_ledger.hpp"
#include "ticketx/wallet_ledger.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ticketx {

struct ReplayState;

class TicketXEngine {
public:
  TicketXEngine() = default;
  explicit TicketXEngine(std::filesystem::path event_log_path);

  bool create_event(Event event);
  std::optional<Event> event(EventId event_id) const;
  PrimaryBuyResult primary_buy(UserId user_id, EventId event_id, const std::string& category);
  [[nodiscard]] const EventLog& event_log() const noexcept { return event_log_; }
  [[nodiscard]] bool event_log_recovery_ok() const noexcept { return event_log_recovery_ok_; }
  [[nodiscard]] const std::vector<std::string>& event_log_recovery_errors() const noexcept {
    return event_log_recovery_errors_;
  }

  bool deposit(UserId user_id, Money amount);
  bool issue_ticket(Ticket ticket);

  WalletBalance wallet_balance(UserId user_id) const;
  std::optional<Ticket> active_ticket(UserId user_id, EventId event_id) const;
  std::optional<Ticket> unlocked_ticket(UserId user_id, EventId event_id,
                                        const std::string& category) const;
  std::optional<Ticket> locked_ticket(UserId user_id, EventId event_id,
                                      const std::string& category) const;

  ExecutionReport place_limit_order(Order order);
  ExecutionReport place_market_order(Order order);
  std::optional<Order> cancel_order(OrderId order_id);

  std::optional<Order> best_bid(EventId event_id, const std::string& category) const;
  std::optional<Order> best_ask(EventId event_id, const std::string& category) const;

private:
  MatchingEngine matching_engine_;
  WalletLedger wallets_;
  TicketLedger tickets_;
  EventLog event_log_;
  std::unique_ptr<AsyncEventWriter> event_writer_;
  std::vector<std::string> event_log_recovery_errors_;
  std::unordered_map<std::uint64_t, Event> events_;
  std::unordered_map<std::uint64_t, Order> open_orders_;
  std::unordered_map<std::uint64_t, Money> locked_buy_amounts_;
  std::uint64_t next_ticket_id_{1};
  std::uint64_t next_event_sequence_id_{1};
  bool event_log_recovery_ok_{true};

  ExecutionReport place_buy_limit(Order order, Money limit_price);
  ExecutionReport place_sell_limit(Order order);
  ExecutionReport place_market_buy(Order order);
  ExecutionReport place_market_sell(Order order);

  bool settle_locked_buyer_trade(const Trade& trade);
  bool settle_market_buy_trade(const Trade& trade);
  bool can_credit(UserId user_id, Money amount) const;
  bool can_transfer_ticket(const Trade& trade) const;
  bool has_open_buy_for_event(UserId user_id, EventId event_id) const;
  PrimarySaleCategory* find_category(Event& event, const std::string& category);
  const PrimarySaleCategory* find_category(const Event& event, const std::string& category) const;
  void append_event(std::string_view type, std::string payload_json);
  void append_trade_events(const Trade& trade);
  void clear_filled_orders(const Trade& trade);
  bool can_settle_locked_buyer_trade(const Trade& trade) const;
  bool can_settle_market_buy_trade(const Trade& trade) const;

  std::optional<Trade> preview_limit_trade(const Order& order) const;
  std::optional<Trade> preview_market_trade(const Order& order) const;
  bool accepts_commands() const noexcept;
  void mark_recovery_failed(std::vector<std::string> errors);
  bool hydrate_from_replay_state(const ReplayState& state);
};

} // namespace ticketx
