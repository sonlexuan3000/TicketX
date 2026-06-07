#include "ticketx/matching_engine.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <utility>

namespace {

ticketx::Order MakeLimitOrder(std::uint64_t id, ticketx::Side side, ticketx::Money price,
                              std::uint64_t event_id, std::string category = "standard") {
  return ticketx::Order{
      .id = ticketx::OrderId{id},
      .user_id = ticketx::UserId{id},
      .event_id = ticketx::EventId{event_id},
      .category = std::move(category),
      .side = side,
      .type = ticketx::OrderType::Limit,
      .limit_price = price,
  };
}

ticketx::Order MakeMarketOrder(std::uint64_t id, ticketx::Side side, std::uint64_t event_id,
                               std::string category = "standard") {
  return ticketx::Order{
      .id = ticketx::OrderId{id},
      .user_id = ticketx::UserId{id},
      .event_id = ticketx::EventId{event_id},
      .category = std::move(category),
      .side = side,
      .type = ticketx::OrderType::Market,
      .limit_price = std::nullopt,
  };
}

} // namespace

TEST(MatchingEngineTest, LimitOrdersInSameMarketMatch) {
  ticketx::MatchingEngine engine;
  ASSERT_EQ(engine.place_limit_order(MakeLimitOrder(1, ticketx::Side::Sell, 1'200'000, 10))
                .status,
            ticketx::OrderStatus::Open);

  const ticketx::ExecutionReport report =
      engine.place_limit_order(MakeLimitOrder(2, ticketx::Side::Buy, 1'250'000, 10));

  EXPECT_EQ(report.status, ticketx::OrderStatus::Filled);
  ASSERT_TRUE(report.trade.has_value());
  EXPECT_EQ(report.trade->buy_order_id.value, 2);
  EXPECT_EQ(report.trade->sell_order_id.value, 1);
  EXPECT_EQ(report.trade->price, 1'200'000);
  EXPECT_FALSE(engine.best_ask(ticketx::EventId{10}, "standard").has_value());
}

TEST(MatchingEngineTest, DifferentEventsDoNotMatch) {
  ticketx::MatchingEngine engine;
  ASSERT_EQ(engine.place_limit_order(MakeLimitOrder(1, ticketx::Side::Sell, 1'100'000, 10))
                .status,
            ticketx::OrderStatus::Open);

  const ticketx::ExecutionReport report =
      engine.place_limit_order(MakeLimitOrder(2, ticketx::Side::Buy, 1'300'000, 20));

  EXPECT_EQ(report.status, ticketx::OrderStatus::Open);
  EXPECT_FALSE(report.trade.has_value());

  const std::optional<ticketx::Order> event10_ask =
      engine.best_ask(ticketx::EventId{10}, "standard");
  ASSERT_TRUE(event10_ask.has_value());
  EXPECT_EQ(event10_ask->id.value, 1);

  const std::optional<ticketx::Order> event20_bid =
      engine.best_bid(ticketx::EventId{20}, "standard");
  ASSERT_TRUE(event20_bid.has_value());
  EXPECT_EQ(event20_bid->id.value, 2);
}

TEST(MatchingEngineTest, DifferentCategoriesDoNotMatch) {
  ticketx::MatchingEngine engine;
  ASSERT_EQ(engine.place_limit_order(MakeLimitOrder(1, ticketx::Side::Sell, 1'100'000, 10, "vip"))
                .status,
            ticketx::OrderStatus::Open);

  const ticketx::ExecutionReport report =
      engine.place_limit_order(MakeLimitOrder(2, ticketx::Side::Buy, 1'300'000, 10, "standard"));

  EXPECT_EQ(report.status, ticketx::OrderStatus::Open);
  EXPECT_FALSE(report.trade.has_value());

  const std::optional<ticketx::Order> vip_ask = engine.best_ask(ticketx::EventId{10}, "vip");
  ASSERT_TRUE(vip_ask.has_value());
  EXPECT_EQ(vip_ask->id.value, 1);

  const std::optional<ticketx::Order> standard_bid =
      engine.best_bid(ticketx::EventId{10}, "standard");
  ASSERT_TRUE(standard_bid.has_value());
  EXPECT_EQ(standard_bid->id.value, 2);
}

TEST(MatchingEngineTest, BestBidIsScopedByMarket) {
  ticketx::MatchingEngine engine;
  ASSERT_EQ(engine.place_limit_order(MakeLimitOrder(1, ticketx::Side::Buy, 1'000'000, 10))
                .status,
            ticketx::OrderStatus::Open);
  ASSERT_EQ(engine.place_limit_order(MakeLimitOrder(2, ticketx::Side::Buy, 1'500'000, 20))
                .status,
            ticketx::OrderStatus::Open);
  ASSERT_EQ(engine.place_limit_order(MakeLimitOrder(3, ticketx::Side::Buy, 1'300'000, 10))
                .status,
            ticketx::OrderStatus::Open);

  const std::optional<ticketx::Order> event10_bid =
      engine.best_bid(ticketx::EventId{10}, "standard");
  ASSERT_TRUE(event10_bid.has_value());
  EXPECT_EQ(event10_bid->id.value, 3);

  const std::optional<ticketx::Order> event20_bid =
      engine.best_bid(ticketx::EventId{20}, "standard");
  ASSERT_TRUE(event20_bid.has_value());
  EXPECT_EQ(event20_bid->id.value, 2);
}

TEST(MatchingEngineTest, MarketBuyOnlyTakesAskFromSameMarket) {
  ticketx::MatchingEngine engine;
  ASSERT_EQ(engine.place_limit_order(MakeLimitOrder(1, ticketx::Side::Sell, 900'000, 20))
                .status,
            ticketx::OrderStatus::Open);
  ASSERT_EQ(engine.place_limit_order(MakeLimitOrder(2, ticketx::Side::Sell, 1'200'000, 10))
                .status,
            ticketx::OrderStatus::Open);

  const ticketx::ExecutionReport report =
      engine.place_market_order(MakeMarketOrder(3, ticketx::Side::Buy, 10));

  EXPECT_EQ(report.status, ticketx::OrderStatus::Filled);
  ASSERT_TRUE(report.trade.has_value());
  EXPECT_EQ(report.trade->sell_order_id.value, 2);
  EXPECT_EQ(report.trade->price, 1'200'000);

  const std::optional<ticketx::Order> event20_ask =
      engine.best_ask(ticketx::EventId{20}, "standard");
  ASSERT_TRUE(event20_ask.has_value());
  EXPECT_EQ(event20_ask->id.value, 1);
}

TEST(MatchingEngineTest, MarketBuyRejectsWhenSameMarketHasNoAsk) {
  ticketx::MatchingEngine engine;
  ASSERT_EQ(engine.place_limit_order(MakeLimitOrder(1, ticketx::Side::Sell, 900'000, 20))
                .status,
            ticketx::OrderStatus::Open);

  const ticketx::ExecutionReport report =
      engine.place_market_order(MakeMarketOrder(2, ticketx::Side::Buy, 10));

  EXPECT_EQ(report.status, ticketx::OrderStatus::Rejected);
  EXPECT_FALSE(report.trade.has_value());

  const std::optional<ticketx::Order> event20_ask =
      engine.best_ask(ticketx::EventId{20}, "standard");
  ASSERT_TRUE(event20_ask.has_value());
  EXPECT_EQ(event20_ask->id.value, 1);
}

TEST(MatchingEngineTest, InvalidLimitOrderDoesNotCreateMarketBook) {
  ticketx::MatchingEngine engine;
  ticketx::Order invalid_order = MakeLimitOrder(1, ticketx::Side::Buy, 0, 10);

  const ticketx::ExecutionReport report = engine.place_limit_order(invalid_order);

  EXPECT_EQ(report.status, ticketx::OrderStatus::Rejected);
  EXPECT_FALSE(report.trade.has_value());
  EXPECT_FALSE(engine.best_bid(ticketx::EventId{10}, "standard").has_value());
  EXPECT_FALSE(engine.best_ask(ticketx::EventId{10}, "standard").has_value());

  const ticketx::ExecutionReport valid_report =
      engine.place_limit_order(MakeLimitOrder(2, ticketx::Side::Buy, 1'200'000, 10));
  EXPECT_EQ(valid_report.status, ticketx::OrderStatus::Open);
  ASSERT_TRUE(engine.best_bid(ticketx::EventId{10}, "standard").has_value());
  EXPECT_EQ(engine.best_bid(ticketx::EventId{10}, "standard")->id.value, 2);
}

TEST(MatchingEngineTest, WrongOrderTypeDoesNotCreateMarketBook) {
  ticketx::MatchingEngine engine;

  const ticketx::ExecutionReport limit_api_report =
      engine.place_limit_order(MakeMarketOrder(1, ticketx::Side::Buy, 10));
  const ticketx::ExecutionReport market_api_report =
      engine.place_market_order(MakeLimitOrder(2, ticketx::Side::Buy, 1'200'000, 20));

  EXPECT_EQ(limit_api_report.status, ticketx::OrderStatus::Rejected);
  EXPECT_EQ(market_api_report.status, ticketx::OrderStatus::Rejected);
  EXPECT_FALSE(engine.best_bid(ticketx::EventId{10}, "standard").has_value());
  EXPECT_FALSE(engine.best_ask(ticketx::EventId{10}, "standard").has_value());
  EXPECT_FALSE(engine.best_bid(ticketx::EventId{20}, "standard").has_value());
  EXPECT_FALSE(engine.best_ask(ticketx::EventId{20}, "standard").has_value());
}

TEST(MatchingEngineTest, MarketOrderInMissingMarketRejectsWithoutCreatingBook) {
  ticketx::MatchingEngine engine;

  const ticketx::ExecutionReport report =
      engine.place_market_order(MakeMarketOrder(1, ticketx::Side::Buy, 10));

  EXPECT_EQ(report.status, ticketx::OrderStatus::Rejected);
  EXPECT_FALSE(report.trade.has_value());
  EXPECT_FALSE(engine.best_bid(ticketx::EventId{10}, "standard").has_value());
  EXPECT_FALSE(engine.best_ask(ticketx::EventId{10}, "standard").has_value());

  const ticketx::ExecutionReport resting_report =
      engine.place_limit_order(MakeLimitOrder(2, ticketx::Side::Sell, 1'200'000, 10));
  EXPECT_EQ(resting_report.status, ticketx::OrderStatus::Open);
  ASSERT_TRUE(engine.best_ask(ticketx::EventId{10}, "standard").has_value());
  EXPECT_EQ(engine.best_ask(ticketx::EventId{10}, "standard")->id.value, 2);
}

TEST(MatchingEngineTest, CancelOpenOrderRemovesItFromCorrectMarket) {
  ticketx::MatchingEngine engine;
  ASSERT_EQ(engine.place_limit_order(MakeLimitOrder(1, ticketx::Side::Buy, 1'200'000, 10))
                .status,
            ticketx::OrderStatus::Open);

  const std::optional<ticketx::Order> cancelled = engine.cancel_order(ticketx::OrderId{1});

  ASSERT_TRUE(cancelled.has_value());
  EXPECT_EQ(cancelled->id.value, 1);
  EXPECT_EQ(cancelled->status, ticketx::OrderStatus::Cancelled);
  EXPECT_FALSE(engine.best_bid(ticketx::EventId{10}, "standard").has_value());
}

TEST(MatchingEngineTest, CancelMissingOrderReturnsNullopt) {
  ticketx::MatchingEngine engine;

  EXPECT_FALSE(engine.cancel_order(ticketx::OrderId{999}).has_value());
}

TEST(MatchingEngineTest, CancelOneMarketDoesNotAffectAnotherMarket) {
  ticketx::MatchingEngine engine;
  ASSERT_EQ(engine.place_limit_order(MakeLimitOrder(1, ticketx::Side::Buy, 1'200'000, 10))
                .status,
            ticketx::OrderStatus::Open);
  ASSERT_EQ(engine.place_limit_order(MakeLimitOrder(2, ticketx::Side::Buy, 1'300'000, 20))
                .status,
            ticketx::OrderStatus::Open);

  ASSERT_TRUE(engine.cancel_order(ticketx::OrderId{1}).has_value());

  EXPECT_FALSE(engine.best_bid(ticketx::EventId{10}, "standard").has_value());
  const std::optional<ticketx::Order> event20_bid =
      engine.best_bid(ticketx::EventId{20}, "standard");
  ASSERT_TRUE(event20_bid.has_value());
  EXPECT_EQ(event20_bid->id.value, 2);
}

TEST(MatchingEngineTest, FilledMakerCannotBeCancelledThroughEngine) {
  ticketx::MatchingEngine engine;
  ASSERT_EQ(engine.place_limit_order(MakeLimitOrder(1, ticketx::Side::Sell, 1'100'000, 10))
                .status,
            ticketx::OrderStatus::Open);

  const ticketx::ExecutionReport fill_report =
      engine.place_limit_order(MakeLimitOrder(2, ticketx::Side::Buy, 1'200'000, 10));
  ASSERT_EQ(fill_report.status, ticketx::OrderStatus::Filled);

  EXPECT_FALSE(engine.cancel_order(ticketx::OrderId{1}).has_value());
  EXPECT_FALSE(engine.cancel_order(ticketx::OrderId{2}).has_value());
}

TEST(MatchingEngineTest, MarketOrderCannotBeCancelledAfterFill) {
  ticketx::MatchingEngine engine;
  ASSERT_EQ(engine.place_limit_order(MakeLimitOrder(1, ticketx::Side::Sell, 1'100'000, 10))
                .status,
            ticketx::OrderStatus::Open);

  const ticketx::ExecutionReport report =
      engine.place_market_order(MakeMarketOrder(2, ticketx::Side::Buy, 10));
  ASSERT_EQ(report.status, ticketx::OrderStatus::Filled);

  EXPECT_FALSE(engine.cancel_order(ticketx::OrderId{2}).has_value());
  EXPECT_FALSE(engine.cancel_order(ticketx::OrderId{1}).has_value());
}

TEST(MatchingEngineTest, DuplicateRestingOrderIdInAnotherMarketRejects) {
  ticketx::MatchingEngine engine;
  ASSERT_EQ(engine.place_limit_order(MakeLimitOrder(1, ticketx::Side::Buy, 1'200'000, 10))
                .status,
            ticketx::OrderStatus::Open);

  const ticketx::ExecutionReport duplicate_report =
      engine.place_limit_order(MakeLimitOrder(1, ticketx::Side::Buy, 1'300'000, 20));

  EXPECT_EQ(duplicate_report.status, ticketx::OrderStatus::Rejected);
  EXPECT_FALSE(duplicate_report.trade.has_value());
  EXPECT_FALSE(engine.best_bid(ticketx::EventId{20}, "standard").has_value());

  const std::optional<ticketx::Order> original_bid =
      engine.best_bid(ticketx::EventId{10}, "standard");
  ASSERT_TRUE(original_bid.has_value());
  EXPECT_EQ(original_bid->id.value, 1);
}

TEST(MatchingEngineTest, CancelledOrderIdCanBeReused) {
  ticketx::MatchingEngine engine;
  ASSERT_EQ(engine.place_limit_order(MakeLimitOrder(1, ticketx::Side::Buy, 1'200'000, 10))
                .status,
            ticketx::OrderStatus::Open);
  ASSERT_TRUE(engine.cancel_order(ticketx::OrderId{1}).has_value());

  const ticketx::ExecutionReport report =
      engine.place_limit_order(MakeLimitOrder(1, ticketx::Side::Buy, 1'300'000, 20));

  EXPECT_EQ(report.status, ticketx::OrderStatus::Open);
  const std::optional<ticketx::Order> event20_bid =
      engine.best_bid(ticketx::EventId{20}, "standard");
  ASSERT_TRUE(event20_bid.has_value());
  EXPECT_EQ(event20_bid->id.value, 1);
}
