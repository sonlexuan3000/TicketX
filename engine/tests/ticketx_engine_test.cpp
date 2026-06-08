#include "ticketx/ticketx_engine.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

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

ticketx::Order MakeMarketOrder(std::uint64_t order_id, std::uint64_t user_id,
                               ticketx::Side side, std::uint64_t event_id = 10,
                               std::string category = "standard") {
  return ticketx::Order{
      .id = ticketx::OrderId{order_id},
      .user_id = ticketx::UserId{user_id},
      .event_id = ticketx::EventId{event_id},
      .category = std::move(category),
      .side = side,
      .type = ticketx::OrderType::Market,
      .limit_price = std::nullopt,
  };
}

ticketx::Ticket MakeTicket(std::uint64_t ticket_id, std::uint64_t owner_id,
                           std::uint64_t event_id = 10,
                           std::string category = "standard") {
  return ticketx::Ticket{
      .id = ticketx::TicketId{ticket_id},
      .event_id = ticketx::EventId{event_id},
      .category = std::move(category),
      .owner_user_id = ticketx::UserId{owner_id},
      .status = ticketx::TicketStatus::Owned,
      .credential_version = 1,
  };
}

void ExpectBalance(const ticketx::TicketXEngine& engine, ticketx::UserId user_id,
                   ticketx::Money available, ticketx::Money locked) {
  const ticketx::WalletBalance balance = engine.wallet_balance(user_id);
  EXPECT_EQ(balance.available, available);
  EXPECT_EQ(balance.locked, locked);
}

void ExpectEventTypes(const ticketx::EventLog& event_log,
                      const std::vector<std::string>& expected_types) {
  ASSERT_EQ(event_log.size(), expected_types.size());
  for (std::size_t i = 0; i < expected_types.size(); ++i) {
    EXPECT_EQ(event_log[i].type, expected_types[i]);
    EXPECT_FALSE(event_log[i].payload_json.empty());
  }
}

} // namespace

TEST(TicketXEngineTest, LimitBuyRejectsWhenBuyerHasInsufficientFunds) {
  ticketx::TicketXEngine engine;
  ASSERT_TRUE(engine.deposit(ticketx::UserId{100}, 1'000'000));

  const ticketx::ExecutionReport report =
      engine.place_limit_order(MakeLimitOrder(1, 100, ticketx::Side::Buy, 1'200'000));

  EXPECT_EQ(report.status, ticketx::OrderStatus::Rejected);
  EXPECT_FALSE(report.trade.has_value());
  ExpectBalance(engine, ticketx::UserId{100}, 1'000'000, 0);
  EXPECT_FALSE(engine.best_bid(ticketx::EventId{10}, "standard").has_value());
}

TEST(TicketXEngineTest, LimitBuyRejectsWhenBuyerAlreadyOwnsTicketForEvent) {
  ticketx::TicketXEngine engine;
  ASSERT_TRUE(engine.deposit(ticketx::UserId{100}, 1'500'000));
  ASSERT_TRUE(engine.issue_ticket(MakeTicket(1, 100)));

  const ticketx::ExecutionReport report =
      engine.place_limit_order(MakeLimitOrder(2, 100, ticketx::Side::Buy, 1'200'000));

  EXPECT_EQ(report.status, ticketx::OrderStatus::Rejected);
  EXPECT_FALSE(report.trade.has_value());
  ExpectBalance(engine, ticketx::UserId{100}, 1'500'000, 0);
  EXPECT_FALSE(engine.best_bid(ticketx::EventId{10}, "standard").has_value());
}

TEST(TicketXEngineTest, SellLimitRejectsWhenSellerHasNoTicket) {
  ticketx::TicketXEngine engine;

  const ticketx::ExecutionReport report =
      engine.place_limit_order(MakeLimitOrder(1, 200, ticketx::Side::Sell, 1'100'000));

  EXPECT_EQ(report.status, ticketx::OrderStatus::Rejected);
  EXPECT_FALSE(report.trade.has_value());
  EXPECT_FALSE(engine.best_ask(ticketx::EventId{10}, "standard").has_value());
}

TEST(TicketXEngineTest, LimitBuyLocksFundsWhileOpen) {
  ticketx::TicketXEngine engine;
  ASSERT_TRUE(engine.deposit(ticketx::UserId{100}, 1'200'000));

  const ticketx::ExecutionReport report =
      engine.place_limit_order(MakeLimitOrder(1, 100, ticketx::Side::Buy, 1'200'000));

  EXPECT_EQ(report.status, ticketx::OrderStatus::Open);
  EXPECT_FALSE(report.trade.has_value());
  ExpectBalance(engine, ticketx::UserId{100}, 0, 1'200'000);
  ASSERT_TRUE(engine.best_bid(ticketx::EventId{10}, "standard").has_value());
  EXPECT_EQ(engine.best_bid(ticketx::EventId{10}, "standard")->id.value, 1);
}

TEST(TicketXEngineTest, IssueTicketRejectsWhenUserHasOpenBuyForSameEvent) {
  ticketx::TicketXEngine engine;
  ASSERT_TRUE(engine.deposit(ticketx::UserId{100}, 1'200'000));
  ASSERT_EQ(engine.place_limit_order(MakeLimitOrder(1, 100, ticketx::Side::Buy, 1'200'000))
                .status,
            ticketx::OrderStatus::Open);

  EXPECT_FALSE(engine.issue_ticket(MakeTicket(1, 100)));

  EXPECT_FALSE(engine.active_ticket(ticketx::UserId{100}, ticketx::EventId{10}).has_value());
  ASSERT_TRUE(engine.best_bid(ticketx::EventId{10}, "standard").has_value());
  ExpectBalance(engine, ticketx::UserId{100}, 0, 1'200'000);
}

TEST(TicketXEngineTest, SellLimitLocksTicketWhileOpen) {
  ticketx::TicketXEngine engine;
  ASSERT_TRUE(engine.issue_ticket(MakeTicket(1, 200)));

  const ticketx::ExecutionReport report =
      engine.place_limit_order(MakeLimitOrder(2, 200, ticketx::Side::Sell, 1'100'000));

  EXPECT_EQ(report.status, ticketx::OrderStatus::Open);
  EXPECT_FALSE(report.trade.has_value());
  EXPECT_FALSE(engine.unlocked_ticket(ticketx::UserId{200}, ticketx::EventId{10}, "standard")
                   .has_value());
  ASSERT_TRUE(engine.locked_ticket(ticketx::UserId{200}, ticketx::EventId{10}, "standard")
                  .has_value());
  EXPECT_EQ(engine.locked_ticket(ticketx::UserId{200}, ticketx::EventId{10}, "standard")
                ->status,
            ticketx::TicketStatus::LockedForSell);
}

TEST(TicketXEngineTest, CancelBuyUnlocksFunds) {
  ticketx::TicketXEngine engine;
  ASSERT_TRUE(engine.deposit(ticketx::UserId{100}, 1'200'000));
  ASSERT_EQ(engine.place_limit_order(MakeLimitOrder(1, 100, ticketx::Side::Buy, 1'200'000))
                .status,
            ticketx::OrderStatus::Open);

  const std::optional<ticketx::Order> cancelled = engine.cancel_order(ticketx::OrderId{1});

  ASSERT_TRUE(cancelled.has_value());
  EXPECT_EQ(cancelled->status, ticketx::OrderStatus::Cancelled);
  ExpectBalance(engine, ticketx::UserId{100}, 1'200'000, 0);
  EXPECT_FALSE(engine.best_bid(ticketx::EventId{10}, "standard").has_value());
}

TEST(TicketXEngineTest, EventLogRecordsOpenOrderAndCancel) {
  ticketx::TicketXEngine engine;
  ASSERT_TRUE(engine.deposit(ticketx::UserId{100}, 1'200'000));
  ASSERT_EQ(engine.place_limit_order(MakeLimitOrder(1, 100, ticketx::Side::Buy, 1'200'000))
                .status,
            ticketx::OrderStatus::Open);
  ASSERT_TRUE(engine.cancel_order(ticketx::OrderId{1}).has_value());

  ExpectEventTypes(engine.event_log(),
                   {
                       std::string{ticketx::event_type::WalletDeposited},
                       std::string{ticketx::event_type::OrderPlaced},
                       std::string{ticketx::event_type::OrderCancelled},
                   });
  EXPECT_NE(engine.event_log()[1].payload_json.find("\"order_id\":1"), std::string::npos);
  EXPECT_NE(engine.event_log()[2].payload_json.find("\"order_id\":1"), std::string::npos);
}

TEST(TicketXEngineTest, CancelSellUnlocksTicket) {
  ticketx::TicketXEngine engine;
  ASSERT_TRUE(engine.issue_ticket(MakeTicket(1, 200)));
  ASSERT_EQ(engine.place_limit_order(MakeLimitOrder(2, 200, ticketx::Side::Sell, 1'100'000))
                .status,
            ticketx::OrderStatus::Open);

  const std::optional<ticketx::Order> cancelled = engine.cancel_order(ticketx::OrderId{2});

  ASSERT_TRUE(cancelled.has_value());
  EXPECT_EQ(cancelled->status, ticketx::OrderStatus::Cancelled);
  EXPECT_TRUE(engine.unlocked_ticket(ticketx::UserId{200}, ticketx::EventId{10}, "standard")
                  .has_value());
  EXPECT_FALSE(engine.locked_ticket(ticketx::UserId{200}, ticketx::EventId{10}, "standard")
                   .has_value());
  EXPECT_FALSE(engine.best_ask(ticketx::EventId{10}, "standard").has_value());
}

TEST(TicketXEngineTest, LimitTradeSettlesWalletsTransfersTicketAndRefundsBuyer) {
  ticketx::TicketXEngine engine;
  ASSERT_TRUE(engine.issue_ticket(MakeTicket(1, 200)));
  ASSERT_TRUE(engine.deposit(ticketx::UserId{100}, 1'300'000));
  ASSERT_EQ(engine.place_limit_order(MakeLimitOrder(2, 200, ticketx::Side::Sell, 1'100'000))
                .status,
            ticketx::OrderStatus::Open);

  const ticketx::ExecutionReport report =
      engine.place_limit_order(MakeLimitOrder(3, 100, ticketx::Side::Buy, 1'300'000));

  EXPECT_EQ(report.status, ticketx::OrderStatus::Filled);
  ASSERT_TRUE(report.trade.has_value());
  EXPECT_EQ(report.trade->price, 1'100'000);
  ExpectBalance(engine, ticketx::UserId{100}, 200'000, 0);
  ExpectBalance(engine, ticketx::UserId{200}, 1'100'000, 0);

  EXPECT_FALSE(engine.active_ticket(ticketx::UserId{200}, ticketx::EventId{10}).has_value());
  const std::optional<ticketx::Ticket> buyer_ticket =
      engine.active_ticket(ticketx::UserId{100}, ticketx::EventId{10});
  ASSERT_TRUE(buyer_ticket.has_value());
  EXPECT_EQ(buyer_ticket->id.value, 1);
  EXPECT_EQ(buyer_ticket->owner_user_id.value, 100);
  EXPECT_EQ(buyer_ticket->credential_version, 2);
  EXPECT_FALSE(engine.best_ask(ticketx::EventId{10}, "standard").has_value());
}

TEST(TicketXEngineTest, EventLogRecordsSuccessfulTradeSettlement) {
  ticketx::TicketXEngine engine;
  ASSERT_TRUE(engine.issue_ticket(MakeTicket(1, 200)));
  ASSERT_TRUE(engine.deposit(ticketx::UserId{100}, 1'300'000));
  ASSERT_EQ(engine.place_limit_order(MakeLimitOrder(2, 200, ticketx::Side::Sell, 1'100'000))
                .status,
            ticketx::OrderStatus::Open);
  ASSERT_EQ(engine.place_limit_order(MakeLimitOrder(3, 100, ticketx::Side::Buy, 1'300'000))
                .status,
            ticketx::OrderStatus::Filled);

  ExpectEventTypes(engine.event_log(),
                   {
                       std::string{ticketx::event_type::WalletDeposited},
                       std::string{ticketx::event_type::OrderPlaced},
                       std::string{ticketx::event_type::OrderMatched},
                       std::string{ticketx::event_type::WalletSettled},
                       std::string{ticketx::event_type::TicketTransferred},
                   });
  EXPECT_NE(engine.event_log()[2].payload_json.find("\"buy_order_id\":3"), std::string::npos);
  EXPECT_NE(engine.event_log()[2].payload_json.find("\"sell_order_id\":2"), std::string::npos);
  EXPECT_NE(engine.event_log()[3].payload_json.find("\"price\":1100000"), std::string::npos);
}

TEST(TicketXEngineTest, LimitBuyRejectsBeforeMatchingWhenSellerCreditWouldOverflow) {
  constexpr ticketx::Money kMaxMoney = std::numeric_limits<ticketx::Money>::max();
  ticketx::TicketXEngine engine;
  ASSERT_TRUE(engine.deposit(ticketx::UserId{200}, kMaxMoney));
  ASSERT_TRUE(engine.issue_ticket(MakeTicket(1, 200)));
  ASSERT_EQ(engine.place_limit_order(MakeLimitOrder(2, 200, ticketx::Side::Sell, 1'100'000))
                .status,
            ticketx::OrderStatus::Open);
  ASSERT_TRUE(engine.deposit(ticketx::UserId{100}, 1'300'000));

  const ticketx::ExecutionReport report =
      engine.place_limit_order(MakeLimitOrder(3, 100, ticketx::Side::Buy, 1'300'000));

  EXPECT_EQ(report.status, ticketx::OrderStatus::Rejected);
  EXPECT_FALSE(report.trade.has_value());
  ExpectBalance(engine, ticketx::UserId{100}, 1'300'000, 0);
  ExpectBalance(engine, ticketx::UserId{200}, kMaxMoney, 0);
  ASSERT_TRUE(engine.best_ask(ticketx::EventId{10}, "standard").has_value());
  EXPECT_EQ(engine.best_ask(ticketx::EventId{10}, "standard")->id.value, 2);
  EXPECT_FALSE(engine.best_bid(ticketx::EventId{10}, "standard").has_value());
  EXPECT_TRUE(engine.locked_ticket(ticketx::UserId{200}, ticketx::EventId{10}, "standard")
                  .has_value());
  EXPECT_FALSE(engine.active_ticket(ticketx::UserId{100}, ticketx::EventId{10}).has_value());
}

TEST(TicketXEngineTest, SellLimitRejectsBeforeMatchingWhenSellerCreditWouldOverflow) {
  constexpr ticketx::Money kMaxMoney = std::numeric_limits<ticketx::Money>::max();
  ticketx::TicketXEngine engine;
  ASSERT_TRUE(engine.deposit(ticketx::UserId{100}, 1'200'000));
  ASSERT_EQ(engine.place_limit_order(MakeLimitOrder(2, 100, ticketx::Side::Buy, 1'200'000))
                .status,
            ticketx::OrderStatus::Open);
  ASSERT_TRUE(engine.deposit(ticketx::UserId{200}, kMaxMoney));
  ASSERT_TRUE(engine.issue_ticket(MakeTicket(1, 200)));

  const ticketx::ExecutionReport report =
      engine.place_limit_order(MakeLimitOrder(3, 200, ticketx::Side::Sell, 1'100'000));

  EXPECT_EQ(report.status, ticketx::OrderStatus::Rejected);
  EXPECT_FALSE(report.trade.has_value());
  ExpectBalance(engine, ticketx::UserId{100}, 0, 1'200'000);
  ExpectBalance(engine, ticketx::UserId{200}, kMaxMoney, 0);
  ASSERT_TRUE(engine.best_bid(ticketx::EventId{10}, "standard").has_value());
  EXPECT_EQ(engine.best_bid(ticketx::EventId{10}, "standard")->id.value, 2);
  EXPECT_FALSE(engine.best_ask(ticketx::EventId{10}, "standard").has_value());
  EXPECT_TRUE(engine.unlocked_ticket(ticketx::UserId{200}, ticketx::EventId{10}, "standard")
                  .has_value());
  EXPECT_FALSE(engine.locked_ticket(ticketx::UserId{200}, ticketx::EventId{10}, "standard")
                   .has_value());
}

TEST(TicketXEngineTest, LimitBuyRejectsBeforeMatchingWhenTicketCredentialWouldOverflow) {
  ticketx::TicketXEngine engine;
  ticketx::Ticket max_version_ticket = MakeTicket(1, 200);
  max_version_ticket.credential_version = std::numeric_limits<std::uint64_t>::max();
  ASSERT_TRUE(engine.issue_ticket(max_version_ticket));
  ASSERT_EQ(engine.place_limit_order(MakeLimitOrder(2, 200, ticketx::Side::Sell, 1'100'000))
                .status,
            ticketx::OrderStatus::Open);
  ASSERT_TRUE(engine.deposit(ticketx::UserId{100}, 1'300'000));

  const ticketx::ExecutionReport report =
      engine.place_limit_order(MakeLimitOrder(3, 100, ticketx::Side::Buy, 1'300'000));

  EXPECT_EQ(report.status, ticketx::OrderStatus::Rejected);
  EXPECT_FALSE(report.trade.has_value());
  ExpectBalance(engine, ticketx::UserId{100}, 1'300'000, 0);
  ASSERT_TRUE(engine.best_ask(ticketx::EventId{10}, "standard").has_value());
  EXPECT_EQ(engine.best_ask(ticketx::EventId{10}, "standard")->id.value, 2);
  ASSERT_TRUE(engine.locked_ticket(ticketx::UserId{200}, ticketx::EventId{10}, "standard")
                  .has_value());
  EXPECT_EQ(engine.locked_ticket(ticketx::UserId{200}, ticketx::EventId{10}, "standard")
                ->credential_version,
            std::numeric_limits<std::uint64_t>::max());
  EXPECT_FALSE(engine.active_ticket(ticketx::UserId{100}, ticketx::EventId{10}).has_value());
}

TEST(TicketXEngineTest, MarketBuyRejectsBeforeMatchingWhenSellerCreditWouldOverflow) {
  constexpr ticketx::Money kMaxMoney = std::numeric_limits<ticketx::Money>::max();
  ticketx::TicketXEngine engine;
  ASSERT_TRUE(engine.deposit(ticketx::UserId{200}, kMaxMoney));
  ASSERT_TRUE(engine.issue_ticket(MakeTicket(1, 200)));
  ASSERT_EQ(engine.place_limit_order(MakeLimitOrder(2, 200, ticketx::Side::Sell, 1'100'000))
                .status,
            ticketx::OrderStatus::Open);
  ASSERT_TRUE(engine.deposit(ticketx::UserId{100}, 1'100'000));

  const ticketx::ExecutionReport report =
      engine.place_market_order(MakeMarketOrder(3, 100, ticketx::Side::Buy));

  EXPECT_EQ(report.status, ticketx::OrderStatus::Rejected);
  EXPECT_FALSE(report.trade.has_value());
  ExpectBalance(engine, ticketx::UserId{100}, 1'100'000, 0);
  ExpectBalance(engine, ticketx::UserId{200}, kMaxMoney, 0);
  ASSERT_TRUE(engine.best_ask(ticketx::EventId{10}, "standard").has_value());
  EXPECT_EQ(engine.best_ask(ticketx::EventId{10}, "standard")->id.value, 2);
  EXPECT_TRUE(engine.locked_ticket(ticketx::UserId{200}, ticketx::EventId{10}, "standard")
                  .has_value());
  EXPECT_FALSE(engine.active_ticket(ticketx::UserId{100}, ticketx::EventId{10}).has_value());
}

TEST(TicketXEngineTest, MarketSellRejectsBeforeMatchingWhenSellerCreditWouldOverflow) {
  constexpr ticketx::Money kMaxMoney = std::numeric_limits<ticketx::Money>::max();
  ticketx::TicketXEngine engine;
  ASSERT_TRUE(engine.deposit(ticketx::UserId{100}, 1'200'000));
  ASSERT_EQ(engine.place_limit_order(MakeLimitOrder(2, 100, ticketx::Side::Buy, 1'200'000))
                .status,
            ticketx::OrderStatus::Open);
  ASSERT_TRUE(engine.deposit(ticketx::UserId{200}, kMaxMoney));
  ASSERT_TRUE(engine.issue_ticket(MakeTicket(1, 200)));

  const ticketx::ExecutionReport report =
      engine.place_market_order(MakeMarketOrder(3, 200, ticketx::Side::Sell));

  EXPECT_EQ(report.status, ticketx::OrderStatus::Rejected);
  EXPECT_FALSE(report.trade.has_value());
  ExpectBalance(engine, ticketx::UserId{100}, 0, 1'200'000);
  ExpectBalance(engine, ticketx::UserId{200}, kMaxMoney, 0);
  ASSERT_TRUE(engine.best_bid(ticketx::EventId{10}, "standard").has_value());
  EXPECT_EQ(engine.best_bid(ticketx::EventId{10}, "standard")->id.value, 2);
  EXPECT_TRUE(engine.unlocked_ticket(ticketx::UserId{200}, ticketx::EventId{10}, "standard")
                  .has_value());
  EXPECT_FALSE(engine.locked_ticket(ticketx::UserId{200}, ticketx::EventId{10}, "standard")
                   .has_value());
  EXPECT_FALSE(engine.active_ticket(ticketx::UserId{100}, ticketx::EventId{10}).has_value());
}

TEST(TicketXEngineTest, MarketBuySettlesAgainstBestAsk) {
  ticketx::TicketXEngine engine;
  ASSERT_TRUE(engine.issue_ticket(MakeTicket(1, 200)));
  ASSERT_TRUE(engine.deposit(ticketx::UserId{100}, 1'100'000));
  ASSERT_EQ(engine.place_limit_order(MakeLimitOrder(2, 200, ticketx::Side::Sell, 1'100'000))
                .status,
            ticketx::OrderStatus::Open);

  const ticketx::ExecutionReport report =
      engine.place_market_order(MakeMarketOrder(3, 100, ticketx::Side::Buy));

  EXPECT_EQ(report.status, ticketx::OrderStatus::Filled);
  ASSERT_TRUE(report.trade.has_value());
  EXPECT_EQ(report.trade->price, 1'100'000);
  ExpectBalance(engine, ticketx::UserId{100}, 0, 0);
  ExpectBalance(engine, ticketx::UserId{200}, 1'100'000, 0);
  ASSERT_TRUE(engine.active_ticket(ticketx::UserId{100}, ticketx::EventId{10}).has_value());
  EXPECT_FALSE(engine.best_ask(ticketx::EventId{10}, "standard").has_value());
}

TEST(TicketXEngineTest, MarketSellSettlesAgainstBestBid) {
  ticketx::TicketXEngine engine;
  ASSERT_TRUE(engine.deposit(ticketx::UserId{100}, 1'200'000));
  ASSERT_TRUE(engine.issue_ticket(MakeTicket(1, 200)));
  ASSERT_EQ(engine.place_limit_order(MakeLimitOrder(2, 100, ticketx::Side::Buy, 1'200'000))
                .status,
            ticketx::OrderStatus::Open);

  const ticketx::ExecutionReport report =
      engine.place_market_order(MakeMarketOrder(3, 200, ticketx::Side::Sell));

  EXPECT_EQ(report.status, ticketx::OrderStatus::Filled);
  ASSERT_TRUE(report.trade.has_value());
  EXPECT_EQ(report.trade->price, 1'200'000);
  ExpectBalance(engine, ticketx::UserId{100}, 0, 0);
  ExpectBalance(engine, ticketx::UserId{200}, 1'200'000, 0);
  ASSERT_TRUE(engine.active_ticket(ticketx::UserId{100}, ticketx::EventId{10}).has_value());
  EXPECT_FALSE(engine.best_bid(ticketx::EventId{10}, "standard").has_value());
}
