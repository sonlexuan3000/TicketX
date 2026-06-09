#include "ticketx/event_replay.hpp"
#include "ticketx/ticketx_engine.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace {

ticketx::EventRecord MakeEvent(std::uint64_t sequence_id, std::string type,
                               std::string payload_json = "{}") {
  return ticketx::EventRecord{
      .sequence_id = sequence_id,
      .type = std::move(type),
      .payload_json = std::move(payload_json),
  };
}

ticketx::Event MakeTicketEvent(std::uint64_t event_id = 10,
                               std::string category = "standard") {
  return ticketx::Event{
      .id = ticketx::EventId{event_id},
      .name = "TicketX Live",
      .categories = {
          ticketx::PrimarySaleCategory{
              .name = std::move(category),
              .price = 1'000'000,
              .remaining = 100,
          },
      },
  };
}

ticketx::Order MakeLimitOrder(std::uint64_t order_id, std::uint64_t user_id,
                              ticketx::Side side, ticketx::Money price,
                              std::uint64_t event_id = 10,
                              std::string category = "standard") {
  return ticketx::Order{
      .id = ticketx::OrderId{order_id},
      .user_id = ticketx::UserId{user_id},
      .event_id = ticketx::EventId{event_id},
      .category = std::move(category),
      .side = side,
      .type = ticketx::OrderType::Limit,
      .limit_price = price,
  };
}

ticketx::Ticket MakeTicket(std::uint64_t ticket_id, std::uint64_t owner_user_id,
                           std::uint64_t event_id = 10,
                           std::string category = "standard") {
  return ticketx::Ticket{
      .id = ticketx::TicketId{ticket_id},
      .event_id = ticketx::EventId{event_id},
      .category = std::move(category),
      .owner_user_id = ticketx::UserId{owner_user_id},
      .status = ticketx::TicketStatus::Owned,
      .credential_version = 1,
  };
}

} // namespace

TEST(EventReplayTest, EmptyLogProducesEmptySummary) {
  const ticketx::ReplaySummary summary = ticketx::replay_summary(ticketx::EventLog{});

  EXPECT_EQ(summary.event_count, 0U);
  EXPECT_EQ(summary.order_placed_count, 0U);
  EXPECT_EQ(summary.order_cancelled_count, 0U);
  EXPECT_EQ(summary.trade_count, 0U);
  EXPECT_EQ(summary.wallet_settled_amount, 0);
  EXPECT_EQ(summary.ticket_transfer_count, 0U);
  EXPECT_EQ(summary.last_sequence_id, 0U);
  EXPECT_TRUE(summary.sequence_contiguous);
  EXPECT_EQ(summary.incomplete_trade_group_count, 0U);
  EXPECT_TRUE(summary.trade_groups_complete);
}

TEST(EventReplayTest, CountsOrderAndTradeEvents) {
  const ticketx::EventLog event_log{
      MakeEvent(1, std::string{ticketx::event_type::OrderPlaced}),
      MakeEvent(2, std::string{ticketx::event_type::OrderCancelled}),
      MakeEvent(3, std::string{ticketx::event_type::OrderMatched}),
      MakeEvent(4, std::string{ticketx::event_type::WalletSettled}, "{\"price\":1000000}"),
      MakeEvent(5, std::string{ticketx::event_type::TicketTransferred}),
  };

  const ticketx::ReplaySummary summary = ticketx::replay_summary(event_log);

  EXPECT_EQ(summary.event_count, 5U);
  EXPECT_EQ(summary.order_placed_count, 1U);
  EXPECT_EQ(summary.order_cancelled_count, 1U);
  EXPECT_EQ(summary.trade_count, 1U);
  EXPECT_EQ(summary.ticket_transfer_count, 1U);
  EXPECT_EQ(summary.wallet_settled_amount, 1'000'000);
  EXPECT_EQ(summary.last_sequence_id, 5U);
  EXPECT_TRUE(summary.sequence_contiguous);
  EXPECT_EQ(summary.incomplete_trade_group_count, 0U);
  EXPECT_TRUE(summary.trade_groups_complete);
}

TEST(EventReplayTest, SumsWalletSettlementPrices) {
  const ticketx::EventLog event_log{
      MakeEvent(10, std::string{ticketx::event_type::OrderMatched}),
      MakeEvent(11, std::string{ticketx::event_type::WalletSettled},
                "{\"buyer_user_id\":1,\"seller_user_id\":2,\"price\":1000000}"),
      MakeEvent(12, std::string{ticketx::event_type::TicketTransferred}),
      MakeEvent(13, std::string{ticketx::event_type::OrderMatched}),
      MakeEvent(14, std::string{ticketx::event_type::WalletSettled},
                "{\"buyer_user_id\":3,\"seller_user_id\":4,\"price\":250000}"),
      MakeEvent(15, std::string{ticketx::event_type::TicketTransferred}),
  };

  const ticketx::ReplaySummary summary = ticketx::replay_summary(event_log);

  EXPECT_EQ(summary.event_count, 6U);
  EXPECT_EQ(summary.wallet_settled_amount, 1'250'000);
  EXPECT_EQ(summary.trade_count, 2U);
  EXPECT_EQ(summary.ticket_transfer_count, 2U);
  EXPECT_EQ(summary.last_sequence_id, 15U);
  EXPECT_TRUE(summary.sequence_contiguous);
  EXPECT_EQ(summary.incomplete_trade_group_count, 0U);
  EXPECT_TRUE(summary.trade_groups_complete);
}

TEST(EventReplayTest, DetectsSequenceGap) {
  const ticketx::EventLog event_log{
      MakeEvent(1, std::string{ticketx::event_type::OrderPlaced}),
      MakeEvent(3, std::string{ticketx::event_type::OrderCancelled}),
  };

  const ticketx::ReplaySummary summary = ticketx::replay_summary(event_log);

  EXPECT_EQ(summary.event_count, 2U);
  EXPECT_EQ(summary.last_sequence_id, 3U);
  EXPECT_FALSE(summary.sequence_contiguous);
}

TEST(EventReplayTest, DetectsOutOfOrderSequence) {
  const ticketx::EventLog event_log{
      MakeEvent(2, std::string{ticketx::event_type::OrderPlaced}),
      MakeEvent(1, std::string{ticketx::event_type::OrderCancelled}),
  };

  const ticketx::ReplaySummary summary = ticketx::replay_summary(event_log);

  EXPECT_EQ(summary.event_count, 2U);
  EXPECT_EQ(summary.last_sequence_id, 1U);
  EXPECT_FALSE(summary.sequence_contiguous);
}

TEST(EventReplayTest, DetectsMissingTradeGroupEvent) {
  const ticketx::EventLog event_log{
      MakeEvent(1, std::string{ticketx::event_type::OrderMatched}),
      MakeEvent(2, std::string{ticketx::event_type::WalletSettled}, "{\"price\":1000000}"),
  };

  const ticketx::ReplaySummary summary = ticketx::replay_summary(event_log);

  EXPECT_EQ(summary.trade_count, 1U);
  EXPECT_EQ(summary.wallet_settled_amount, 1'000'000);
  EXPECT_EQ(summary.incomplete_trade_group_count, 1U);
  EXPECT_FALSE(summary.trade_groups_complete);
}

TEST(EventReplayTest, DetectsOutOfOrderTradeGroup) {
  const ticketx::EventLog event_log{
      MakeEvent(1, std::string{ticketx::event_type::OrderMatched}),
      MakeEvent(2, std::string{ticketx::event_type::TicketTransferred}),
      MakeEvent(3, std::string{ticketx::event_type::WalletSettled}, "{\"price\":1000000}"),
  };

  const ticketx::ReplaySummary summary = ticketx::replay_summary(event_log);

  EXPECT_EQ(summary.trade_count, 1U);
  EXPECT_EQ(summary.ticket_transfer_count, 1U);
  EXPECT_EQ(summary.incomplete_trade_group_count, 1U);
  EXPECT_FALSE(summary.trade_groups_complete);
}

TEST(EventReplayTest, DetectsOrphanTradeEffectEvent) {
  const ticketx::EventLog event_log{
      MakeEvent(1, std::string{ticketx::event_type::WalletSettled}, "{\"price\":1000000}"),
  };

  const ticketx::ReplaySummary summary = ticketx::replay_summary(event_log);

  EXPECT_EQ(summary.trade_count, 0U);
  EXPECT_EQ(summary.wallet_settled_amount, 1'000'000);
  EXPECT_EQ(summary.incomplete_trade_group_count, 1U);
  EXPECT_FALSE(summary.trade_groups_complete);
}

TEST(EventReplayTest, ReplayStateAppliesWalletDeposits) {
  const ticketx::EventLog event_log{
      MakeEvent(1, std::string{ticketx::event_type::WalletDeposited},
                "{\"user_id\":100,\"amount\":1000000}"),
      MakeEvent(2, std::string{ticketx::event_type::WalletDeposited},
                "{\"user_id\":100,\"amount\":250000}"),
      MakeEvent(3, std::string{ticketx::event_type::WalletDeposited},
                "{\"user_id\":200,\"amount\":500000}"),
  };

  const ticketx::ReplayState state = ticketx::replay_state(event_log);

  ASSERT_TRUE(state.wallets.contains(100));
  EXPECT_EQ(state.wallets.at(100).available, 1'250'000);
  EXPECT_EQ(state.wallets.at(100).locked, 0);
  ASSERT_TRUE(state.wallets.contains(200));
  EXPECT_EQ(state.wallets.at(200).available, 500'000);
  EXPECT_EQ(state.summary.event_count, 3U);
  EXPECT_TRUE(state.summary.sequence_contiguous);
}

TEST(EventReplayTest, ReplayStateTracksOpenOrdersAndCancels) {
  const ticketx::EventLog event_log{
      MakeEvent(1, std::string{ticketx::event_type::OrderPlaced},
                "{\"order_id\":10,\"user_id\":100,\"event_id\":7,\"category\":\"vip\","
                "\"side\":\"Buy\",\"type\":\"Limit\",\"limit_price\":1200000,"
                "\"status\":\"Open\"}"),
      MakeEvent(2, std::string{ticketx::event_type::OrderPlaced},
                "{\"order_id\":11,\"user_id\":200,\"event_id\":7,\"category\":\"standard\","
                "\"side\":\"Sell\",\"type\":\"Limit\",\"limit_price\":900000,"
                "\"status\":\"Open\"}"),
      MakeEvent(3, std::string{ticketx::event_type::OrderCancelled},
                "{\"order_id\":10,\"user_id\":100,\"event_id\":7,\"category\":\"vip\","
                "\"side\":\"Buy\",\"type\":\"Limit\",\"limit_price\":1200000,"
                "\"status\":\"Cancelled\"}"),
  };

  const ticketx::ReplayState state = ticketx::replay_state(event_log);

  EXPECT_FALSE(state.open_orders.contains(10));
  ASSERT_TRUE(state.open_orders.contains(11));
  const ticketx::Order& order = state.open_orders.at(11);
  EXPECT_EQ(order.id.value, 11U);
  EXPECT_EQ(order.user_id.value, 200U);
  EXPECT_EQ(order.event_id.value, 7U);
  EXPECT_EQ(order.category, "standard");
  EXPECT_EQ(order.side, ticketx::Side::Sell);
  EXPECT_EQ(order.type, ticketx::OrderType::Limit);
  ASSERT_TRUE(order.limit_price.has_value());
  EXPECT_EQ(*order.limit_price, 900'000);
  EXPECT_EQ(order.status, ticketx::OrderStatus::Open);
  EXPECT_EQ(state.summary.order_placed_count, 2U);
  EXPECT_EQ(state.summary.order_cancelled_count, 1U);
}

TEST(EventReplayTest, ReplayStateLocksFundsForOpenBuyOrders) {
  const ticketx::EventLog event_log{
      MakeEvent(1, std::string{ticketx::event_type::WalletDeposited},
                "{\"user_id\":100,\"amount\":1000000}"),
      MakeEvent(2, std::string{ticketx::event_type::OrderPlaced},
                "{\"order_id\":10,\"user_id\":100,\"event_id\":7,\"category\":\"vip\","
                "\"side\":\"Buy\",\"type\":\"Limit\",\"limit_price\":700000,"
                "\"status\":\"Open\"}"),
  };

  const ticketx::ReplayState state = ticketx::replay_state(event_log);

  ASSERT_TRUE(state.wallets.contains(100));
  EXPECT_EQ(state.wallets.at(100).available, 300'000);
  EXPECT_EQ(state.wallets.at(100).locked, 700'000);
  ASSERT_TRUE(state.open_orders.contains(10));
}

TEST(EventReplayTest, ReplayStateUnlocksFundsWhenBuyOrderIsCancelled) {
  const ticketx::EventLog event_log{
      MakeEvent(1, std::string{ticketx::event_type::WalletDeposited},
                "{\"user_id\":100,\"amount\":1000000}"),
      MakeEvent(2, std::string{ticketx::event_type::OrderPlaced},
                "{\"order_id\":10,\"user_id\":100,\"event_id\":7,\"category\":\"vip\","
                "\"side\":\"Buy\",\"type\":\"Limit\",\"limit_price\":700000,"
                "\"status\":\"Open\"}"),
      MakeEvent(3, std::string{ticketx::event_type::OrderCancelled}, "{\"order_id\":10}"),
  };

  const ticketx::ReplayState state = ticketx::replay_state(event_log);

  ASSERT_TRUE(state.wallets.contains(100));
  EXPECT_EQ(state.wallets.at(100).available, 1'000'000);
  EXPECT_EQ(state.wallets.at(100).locked, 0);
  EXPECT_FALSE(state.open_orders.contains(10));
}

TEST(EventReplayTest, ReplayStateDoesNotMutateWalletForSellOrderLifecycle) {
  const ticketx::EventLog event_log{
      MakeEvent(1, std::string{ticketx::event_type::WalletDeposited},
                "{\"user_id\":200,\"amount\":500000}"),
      MakeEvent(2, std::string{ticketx::event_type::OrderPlaced},
                "{\"order_id\":20,\"user_id\":200,\"event_id\":7,\"category\":\"vip\","
                "\"side\":\"Sell\",\"type\":\"Limit\",\"limit_price\":900000,"
                "\"status\":\"Open\"}"),
      MakeEvent(3, std::string{ticketx::event_type::OrderCancelled}, "{\"order_id\":20}"),
  };

  const ticketx::ReplayState state = ticketx::replay_state(event_log);

  ASSERT_TRUE(state.wallets.contains(200));
  EXPECT_EQ(state.wallets.at(200).available, 500'000);
  EXPECT_EQ(state.wallets.at(200).locked, 0);
  EXPECT_TRUE(state.open_orders.empty());
}

TEST(EventReplayTest, ReplayStateIgnoresCancelForMissingOrder) {
  const ticketx::EventLog event_log{
      MakeEvent(1, std::string{ticketx::event_type::WalletDeposited},
                "{\"user_id\":100,\"amount\":1000000}"),
      MakeEvent(2, std::string{ticketx::event_type::OrderCancelled}, "{\"order_id\":99}"),
  };

  const ticketx::ReplayState state = ticketx::replay_state(event_log);

  ASSERT_TRUE(state.wallets.contains(100));
  EXPECT_EQ(state.wallets.at(100).available, 1'000'000);
  EXPECT_EQ(state.wallets.at(100).locked, 0);
  EXPECT_TRUE(state.open_orders.empty());
}

TEST(EventReplayTest, ReplayStateKeepsWalletUnchangedWhenBuyLockCannotBeApplied) {
  const ticketx::EventLog event_log{
      MakeEvent(1, std::string{ticketx::event_type::WalletDeposited},
                "{\"user_id\":100,\"amount\":500000}"),
      MakeEvent(2, std::string{ticketx::event_type::OrderPlaced},
                "{\"order_id\":10,\"user_id\":100,\"event_id\":7,\"category\":\"vip\","
                "\"side\":\"Buy\",\"type\":\"Limit\",\"limit_price\":700000,"
                "\"status\":\"Open\"}"),
  };

  const ticketx::ReplayState state = ticketx::replay_state(event_log);

  ASSERT_TRUE(state.wallets.contains(100));
  EXPECT_EQ(state.wallets.at(100).available, 500'000);
  EXPECT_EQ(state.wallets.at(100).locked, 0);
  ASSERT_TRUE(state.open_orders.contains(10));
}

TEST(EventReplayTest, ReplayStateAppliesWalletSettlementFromLockedBuyer) {
  const ticketx::EventLog event_log{
      MakeEvent(1, std::string{ticketx::event_type::WalletDeposited},
                "{\"user_id\":100,\"amount\":1000000}"),
      MakeEvent(2, std::string{ticketx::event_type::WalletDeposited},
                "{\"user_id\":200,\"amount\":100000}"),
      MakeEvent(3, std::string{ticketx::event_type::OrderPlaced},
                "{\"order_id\":10,\"user_id\":100,\"event_id\":7,\"category\":\"vip\","
                "\"side\":\"Buy\",\"type\":\"Limit\",\"limit_price\":700000,"
                "\"status\":\"Open\"}"),
      MakeEvent(4, std::string{ticketx::event_type::WalletSettled},
                "{\"buyer_user_id\":100,\"seller_user_id\":200,\"price\":600000}"),
  };

  const ticketx::ReplayState state = ticketx::replay_state(event_log);

  ASSERT_TRUE(state.wallets.contains(100));
  EXPECT_EQ(state.wallets.at(100).available, 300'000);
  EXPECT_EQ(state.wallets.at(100).locked, 100'000);
  ASSERT_TRUE(state.wallets.contains(200));
  EXPECT_EQ(state.wallets.at(200).available, 700'000);
  EXPECT_EQ(state.wallets.at(200).locked, 0);
}

TEST(EventReplayTest, ReplayStateAppliesWalletSettlementFromAvailableBuyer) {
  const ticketx::EventLog event_log{
      MakeEvent(1, std::string{ticketx::event_type::WalletDeposited},
                "{\"user_id\":100,\"amount\":1000000}"),
      MakeEvent(2, std::string{ticketx::event_type::WalletSettled},
                "{\"buyer_user_id\":100,\"seller_user_id\":200,\"price\":400000}"),
  };

  const ticketx::ReplayState state = ticketx::replay_state(event_log);

  ASSERT_TRUE(state.wallets.contains(100));
  EXPECT_EQ(state.wallets.at(100).available, 600'000);
  EXPECT_EQ(state.wallets.at(100).locked, 0);
  ASSERT_TRUE(state.wallets.contains(200));
  EXPECT_EQ(state.wallets.at(200).available, 400'000);
  EXPECT_EQ(state.wallets.at(200).locked, 0);
}

TEST(EventReplayTest, ReplayStateSkipsWalletSettlementWhenBuyerCannotPay) {
  const ticketx::EventLog event_log{
      MakeEvent(1, std::string{ticketx::event_type::WalletDeposited},
                "{\"user_id\":100,\"amount\":300000}"),
      MakeEvent(2, std::string{ticketx::event_type::WalletDeposited},
                "{\"user_id\":200,\"amount\":100000}"),
      MakeEvent(3, std::string{ticketx::event_type::WalletSettled},
                "{\"buyer_user_id\":100,\"seller_user_id\":200,\"price\":400000}"),
  };

  const ticketx::ReplayState state = ticketx::replay_state(event_log);

  ASSERT_TRUE(state.wallets.contains(100));
  EXPECT_EQ(state.wallets.at(100).available, 300'000);
  EXPECT_EQ(state.wallets.at(100).locked, 0);
  ASSERT_TRUE(state.wallets.contains(200));
  EXPECT_EQ(state.wallets.at(200).available, 100'000);
  EXPECT_EQ(state.wallets.at(200).locked, 0);
}

TEST(EventReplayTest, ReplayStateSkipsWalletSettlementWhenSellerCreditWouldOverflow) {
  const ticketx::Money seller_balance = std::numeric_limits<ticketx::Money>::max() - 100;
  const ticketx::EventLog event_log{
      MakeEvent(1, std::string{ticketx::event_type::WalletDeposited},
                "{\"user_id\":100,\"amount\":1000000}"),
      MakeEvent(2, std::string{ticketx::event_type::WalletDeposited},
                "{\"user_id\":200,\"amount\":" + std::to_string(seller_balance) + "}"),
      MakeEvent(3, std::string{ticketx::event_type::WalletSettled},
                "{\"buyer_user_id\":100,\"seller_user_id\":200,\"price\":200}"),
  };

  const ticketx::ReplayState state = ticketx::replay_state(event_log);

  ASSERT_TRUE(state.wallets.contains(100));
  EXPECT_EQ(state.wallets.at(100).available, 1'000'000);
  EXPECT_EQ(state.wallets.at(100).locked, 0);
  ASSERT_TRUE(state.wallets.contains(200));
  EXPECT_EQ(state.wallets.at(200).available, seller_balance);
  EXPECT_EQ(state.wallets.at(200).locked, 0);
}

TEST(EventReplayTest, ReplayStateAppliesTicketIssued) {
  const ticketx::EventLog event_log{
      MakeEvent(1, std::string{ticketx::event_type::TicketIssued},
                "{\"ticket_id\":30,\"owner_user_id\":100,\"event_id\":7,"
                "\"category\":\"vip\",\"status\":\"Owned\",\"credential_version\":1}"),
  };

  const ticketx::ReplayState state = ticketx::replay_state(event_log);

  ASSERT_TRUE(state.tickets.contains(30));
  const ticketx::Ticket& ticket = state.tickets.at(30);
  EXPECT_EQ(ticket.id.value, 30U);
  EXPECT_EQ(ticket.owner_user_id.value, 100U);
  EXPECT_EQ(ticket.event_id.value, 7U);
  EXPECT_EQ(ticket.category, "vip");
  EXPECT_EQ(ticket.status, ticketx::TicketStatus::Owned);
  EXPECT_EQ(ticket.credential_version, 1U);
}

TEST(EventReplayTest, ReplayStateAppliesPrimaryTicketBought) {
  const ticketx::EventLog event_log{
      MakeEvent(1, std::string{ticketx::event_type::PrimaryTicketBought},
                "{\"ticket_id\":30,\"buyer_user_id\":100,\"event_id\":7,"
                "\"category\":\"vip\",\"price\":500000,\"credential_version\":1}"),
  };

  const ticketx::ReplayState state = ticketx::replay_state(event_log);

  ASSERT_TRUE(state.tickets.contains(30));
  const ticketx::Ticket& ticket = state.tickets.at(30);
  EXPECT_EQ(ticket.owner_user_id.value, 100U);
  EXPECT_EQ(ticket.event_id.value, 7U);
  EXPECT_EQ(ticket.category, "vip");
  EXPECT_EQ(ticket.status, ticketx::TicketStatus::Owned);
  EXPECT_EQ(ticket.credential_version, 1U);
}

TEST(EventReplayTest, ReplayStateAllowsSameUserTicketsForDifferentEvents) {
  const ticketx::EventLog event_log{
      MakeEvent(1, std::string{ticketx::event_type::TicketIssued},
                "{\"ticket_id\":30,\"owner_user_id\":100,\"event_id\":7,"
                "\"category\":\"vip\",\"status\":\"Owned\",\"credential_version\":1}"),
      MakeEvent(2, std::string{ticketx::event_type::TicketIssued},
                "{\"ticket_id\":31,\"owner_user_id\":100,\"event_id\":8,"
                "\"category\":\"vip\",\"status\":\"Owned\",\"credential_version\":1}"),
  };

  const ticketx::ReplayState state = ticketx::replay_state(event_log);

  ASSERT_TRUE(state.tickets.contains(30));
  ASSERT_TRUE(state.tickets.contains(31));
  EXPECT_EQ(state.tickets.at(30).event_id.value, 7U);
  EXPECT_EQ(state.tickets.at(31).event_id.value, 8U);
}

TEST(EventReplayTest, ReplayStateRejectsSecondActiveTicketForSameUserAndEvent) {
  const ticketx::EventLog event_log{
      MakeEvent(1, std::string{ticketx::event_type::TicketIssued},
                "{\"ticket_id\":30,\"owner_user_id\":100,\"event_id\":7,"
                "\"category\":\"vip\",\"status\":\"Owned\",\"credential_version\":1}"),
      MakeEvent(2, std::string{ticketx::event_type::TicketIssued},
                "{\"ticket_id\":31,\"owner_user_id\":100,\"event_id\":7,"
                "\"category\":\"standard\",\"status\":\"Owned\",\"credential_version\":1}"),
  };

  const ticketx::ReplayState state = ticketx::replay_state(event_log);

  ASSERT_TRUE(state.tickets.contains(30));
  EXPECT_FALSE(state.tickets.contains(31));
}

TEST(EventReplayTest, ReplayStateTransfersTicketOwnership) {
  const ticketx::EventLog event_log{
      MakeEvent(1, std::string{ticketx::event_type::TicketIssued},
                "{\"ticket_id\":30,\"owner_user_id\":100,\"event_id\":7,"
                "\"category\":\"vip\",\"status\":\"LockedForSell\","
                "\"credential_version\":1}"),
      MakeEvent(2, std::string{ticketx::event_type::TicketTransferred},
                "{\"buyer_user_id\":200,\"seller_user_id\":100,\"event_id\":7,"
                "\"category\":\"vip\"}"),
  };

  const ticketx::ReplayState state = ticketx::replay_state(event_log);

  ASSERT_TRUE(state.tickets.contains(30));
  const ticketx::Ticket& ticket = state.tickets.at(30);
  EXPECT_EQ(ticket.owner_user_id.value, 200U);
  EXPECT_EQ(ticket.event_id.value, 7U);
  EXPECT_EQ(ticket.category, "vip");
  EXPECT_EQ(ticket.status, ticketx::TicketStatus::Owned);
  EXPECT_EQ(ticket.credential_version, 2U);
}

TEST(EventReplayTest, ReplayStateSkipsTicketTransferWhenSellerTicketMissing) {
  const ticketx::EventLog event_log{
      MakeEvent(1, std::string{ticketx::event_type::TicketTransferred},
                "{\"buyer_user_id\":200,\"seller_user_id\":100,\"event_id\":7,"
                "\"category\":\"vip\"}"),
  };

  const ticketx::ReplayState state = ticketx::replay_state(event_log);

  EXPECT_TRUE(state.tickets.empty());
}

TEST(EventReplayTest, ReplayStateSkipsTicketTransferWhenBuyerAlreadyOwnsEventTicket) {
  const ticketx::EventLog event_log{
      MakeEvent(1, std::string{ticketx::event_type::TicketIssued},
                "{\"ticket_id\":30,\"owner_user_id\":100,\"event_id\":7,"
                "\"category\":\"vip\",\"status\":\"Owned\",\"credential_version\":1}"),
      MakeEvent(2, std::string{ticketx::event_type::TicketIssued},
                "{\"ticket_id\":31,\"owner_user_id\":200,\"event_id\":7,"
                "\"category\":\"standard\",\"status\":\"Owned\",\"credential_version\":1}"),
      MakeEvent(3, std::string{ticketx::event_type::TicketTransferred},
                "{\"buyer_user_id\":200,\"seller_user_id\":100,\"event_id\":7,"
                "\"category\":\"vip\"}"),
  };

  const ticketx::ReplayState state = ticketx::replay_state(event_log);

  ASSERT_TRUE(state.tickets.contains(30));
  EXPECT_EQ(state.tickets.at(30).owner_user_id.value, 100U);
  EXPECT_EQ(state.tickets.at(30).credential_version, 1U);
  ASSERT_TRUE(state.tickets.contains(31));
  EXPECT_EQ(state.tickets.at(31).owner_user_id.value, 200U);
}

TEST(EventReplayTest, ReplayStateSkipsTicketTransferWhenCredentialWouldOverflow) {
  const std::uint64_t max_version = std::numeric_limits<std::uint64_t>::max();
  const ticketx::EventLog event_log{
      MakeEvent(1, std::string{ticketx::event_type::TicketIssued},
                "{\"ticket_id\":30,\"owner_user_id\":100,\"event_id\":7,"
                "\"category\":\"vip\",\"status\":\"Owned\",\"credential_version\":" +
                    std::to_string(max_version) + "}"),
      MakeEvent(2, std::string{ticketx::event_type::TicketTransferred},
                "{\"buyer_user_id\":200,\"seller_user_id\":100,\"event_id\":7,"
                "\"category\":\"vip\"}"),
  };

  const ticketx::ReplayState state = ticketx::replay_state(event_log);

  ASSERT_TRUE(state.tickets.contains(30));
  EXPECT_EQ(state.tickets.at(30).owner_user_id.value, 100U);
  EXPECT_EQ(state.tickets.at(30).credential_version, max_version);
}

TEST(EventReplayTest, ReplayStateIgnoresMalformedPayloadsWithoutLosingSummary) {
  const ticketx::EventLog event_log{
      MakeEvent(1, std::string{ticketx::event_type::WalletDeposited},
                "{\"user_id\":100,\"amount\":-1}"),
      MakeEvent(2, std::string{ticketx::event_type::OrderPlaced},
                "{\"order_id\":10,\"user_id\":100,\"event_id\":7}"),
      MakeEvent(4, std::string{ticketx::event_type::OrderCancelled}, "{not json"),
  };

  const ticketx::ReplayState state = ticketx::replay_state(event_log);

  EXPECT_TRUE(state.wallets.empty());
  EXPECT_TRUE(state.open_orders.empty());
  EXPECT_EQ(state.summary.event_count, 3U);
  EXPECT_EQ(state.summary.order_placed_count, 1U);
  EXPECT_EQ(state.summary.order_cancelled_count, 1U);
  EXPECT_EQ(state.summary.last_sequence_id, 4U);
  EXPECT_FALSE(state.summary.sequence_contiguous);
}

TEST(EventReplayIntegrationTest, ReplayStateMatchesEngineLogForSuccessfulLimitTrade) {
  ticketx::TicketXEngine engine;
  ASSERT_TRUE(engine.create_event(MakeTicketEvent()));
  ASSERT_TRUE(engine.issue_ticket(MakeTicket(1, 200)));
  ASSERT_TRUE(engine.deposit(ticketx::UserId{100}, 1'300'000));
  ASSERT_EQ(engine.place_limit_order(MakeLimitOrder(2, 200, ticketx::Side::Sell, 1'100'000))
                .status,
            ticketx::OrderStatus::Open);

  const ticketx::ExecutionReport report =
      engine.place_limit_order(MakeLimitOrder(3, 100, ticketx::Side::Buy, 1'300'000));
  ASSERT_EQ(report.status, ticketx::OrderStatus::Filled);

  const ticketx::ReplayState state = ticketx::replay_state(engine.event_log());

  EXPECT_EQ(state.summary.event_count, engine.event_log().size());
  EXPECT_EQ(state.summary.order_placed_count, 1U);
  EXPECT_EQ(state.summary.trade_count, 1U);
  EXPECT_EQ(state.summary.wallet_settled_amount, 1'100'000);
  EXPECT_EQ(state.summary.ticket_transfer_count, 1U);
  EXPECT_TRUE(state.summary.sequence_contiguous);
  EXPECT_TRUE(state.summary.trade_groups_complete);

  ASSERT_TRUE(state.wallets.contains(100));
  EXPECT_EQ(state.wallets.at(100).available, 200'000);
  EXPECT_EQ(state.wallets.at(100).locked, 0);
  ASSERT_TRUE(state.wallets.contains(200));
  EXPECT_EQ(state.wallets.at(200).available, 1'100'000);
  EXPECT_EQ(state.wallets.at(200).locked, 0);

  EXPECT_TRUE(state.open_orders.empty());
  ASSERT_TRUE(state.tickets.contains(1));
  const ticketx::Ticket& replayed_ticket = state.tickets.at(1);
  EXPECT_EQ(replayed_ticket.owner_user_id.value, 100U);
  EXPECT_EQ(replayed_ticket.event_id.value, 10U);
  EXPECT_EQ(replayed_ticket.category, "standard");
  EXPECT_EQ(replayed_ticket.status, ticketx::TicketStatus::Owned);
  EXPECT_EQ(replayed_ticket.credential_version, 2U);
}
