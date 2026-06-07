#include "ticketx/order_book.hpp"

#include <gtest/gtest.h>

#include <optional>

namespace {

ticketx::Order MakeLimitOrder(std::uint64_t id, ticketx::Side side, ticketx::Money price) {
  return ticketx::Order{
      .id = ticketx::OrderId{id},
      .user_id = ticketx::UserId{id},
      .event_id = ticketx::EventId{1},
      .category = "standard",
      .side = side,
      .type = ticketx::OrderType::Limit,
      .limit_price = price,
  };
}

ticketx::Order MakeMarketOrder(std::uint64_t id, ticketx::Side side) {
  return ticketx::Order{
      .id = ticketx::OrderId{id},
      .user_id = ticketx::UserId{id},
      .event_id = ticketx::EventId{1},
      .category = "standard",
      .side = side,
      .type = ticketx::OrderType::Market,
      .limit_price = std::nullopt,
  };
}

} // namespace

TEST(OrderBookTest, EmptyBookHasNoBestBidOrAsk) {
  const ticketx::OrderBook book;
  EXPECT_TRUE(book.empty());
  EXPECT_EQ(book.size(), 0);
  EXPECT_FALSE(book.best_bid().has_value());
  EXPECT_FALSE(book.best_ask().has_value());
}

TEST(OrderBookTest, AddLimitOrderRejectsMarketOrder) {
  ticketx::OrderBook book;

  book.add_limit_order(MakeMarketOrder(1, ticketx::Side::Buy));

  EXPECT_TRUE(book.empty());
  EXPECT_FALSE(book.best_bid().has_value());
  EXPECT_FALSE(book.best_ask().has_value());
}

TEST(OrderBookTest, AddLimitOrderRejectsMissingPrice) {
  ticketx::OrderBook book;
  ticketx::Order order = MakeLimitOrder(1, ticketx::Side::Buy, 1'200'000);
  order.limit_price = std::nullopt;

  book.add_limit_order(order);

  EXPECT_TRUE(book.empty());
}

TEST(OrderBookTest, AddLimitOrderRejectsNonPositivePrice) {
  ticketx::OrderBook book;

  book.add_limit_order(MakeLimitOrder(1, ticketx::Side::Buy, 0));
  book.add_limit_order(MakeLimitOrder(2, ticketx::Side::Sell, -1));

  EXPECT_TRUE(book.empty());
  EXPECT_FALSE(book.best_bid().has_value());
  EXPECT_FALSE(book.best_ask().has_value());
}

TEST(OrderBookTest, BuyLimitCreatesBestBid) {
  ticketx::OrderBook book;
  book.add_limit_order(MakeLimitOrder(1, ticketx::Side::Buy, 1'200'000));

  const std::optional<ticketx::Order> best = book.best_bid();
  ASSERT_TRUE(best.has_value());
  EXPECT_EQ(best->id.value, 1);
  ASSERT_TRUE(best->limit_price.has_value());
  EXPECT_EQ(*best->limit_price, 1'200'000);
  EXPECT_EQ(best->status, ticketx::OrderStatus::Open);
  EXPECT_EQ(book.size(), 1);
}

TEST(OrderBookTest, SellLimitCreatesBestAsk) {
  ticketx::OrderBook book;
  book.add_limit_order(MakeLimitOrder(1, ticketx::Side::Sell, 1'150'000));

  const std::optional<ticketx::Order> best = book.best_ask();
  ASSERT_TRUE(best.has_value());
  EXPECT_EQ(best->id.value, 1);
  ASSERT_TRUE(best->limit_price.has_value());
  EXPECT_EQ(*best->limit_price, 1'150'000);
  EXPECT_EQ(best->status, ticketx::OrderStatus::Open);
  EXPECT_EQ(book.size(), 1);
}

TEST(OrderBookTest, HighestBidIsBestBid) {
  ticketx::OrderBook book;
  book.add_limit_order(MakeLimitOrder(1, ticketx::Side::Buy, 1'000'000));
  book.add_limit_order(MakeLimitOrder(2, ticketx::Side::Buy, 1'250'000));
  book.add_limit_order(MakeLimitOrder(3, ticketx::Side::Buy, 1'100'000));

  const std::optional<ticketx::Order> best = book.best_bid();
  ASSERT_TRUE(best.has_value());
  EXPECT_EQ(best->id.value, 2);
  ASSERT_TRUE(best->limit_price.has_value());
  EXPECT_EQ(*best->limit_price, 1'250'000);
  EXPECT_EQ(book.size(), 3);
}

TEST(OrderBookTest, LowestAskIsBestAsk) {
  ticketx::OrderBook book;
  book.add_limit_order(MakeLimitOrder(1, ticketx::Side::Sell, 1'300'000));
  book.add_limit_order(MakeLimitOrder(2, ticketx::Side::Sell, 1'180'000));
  book.add_limit_order(MakeLimitOrder(3, ticketx::Side::Sell, 1'240'000));

  const std::optional<ticketx::Order> best = book.best_ask();
  ASSERT_TRUE(best.has_value());
  EXPECT_EQ(best->id.value, 2);
  ASSERT_TRUE(best->limit_price.has_value());
  EXPECT_EQ(*best->limit_price, 1'180'000);
  EXPECT_EQ(book.size(), 3);
}

TEST(OrderBookTest, CancelExistingBuyRemovesItFromBestBid) {
  ticketx::OrderBook book;
  book.add_limit_order(MakeLimitOrder(1, ticketx::Side::Buy, 1'200'000));

  const std::optional<ticketx::Order> cancelled = book.cancel_order(ticketx::OrderId{1});
  ASSERT_TRUE(cancelled.has_value());
  EXPECT_EQ(cancelled->id.value, 1);
  EXPECT_EQ(cancelled->status, ticketx::OrderStatus::Cancelled);
  EXPECT_FALSE(book.best_bid().has_value());
  EXPECT_TRUE(book.empty());
}

TEST(OrderBookTest, CancelExistingSellRemovesItFromBestAsk) {
  ticketx::OrderBook book;
  book.add_limit_order(MakeLimitOrder(1, ticketx::Side::Sell, 1'150'000));

  const std::optional<ticketx::Order> cancelled = book.cancel_order(ticketx::OrderId{1});
  ASSERT_TRUE(cancelled.has_value());
  EXPECT_EQ(cancelled->id.value, 1);
  EXPECT_EQ(cancelled->status, ticketx::OrderStatus::Cancelled);
  EXPECT_FALSE(book.best_ask().has_value());
  EXPECT_TRUE(book.empty());
}

TEST(OrderBookTest, CancelMissingOrderReturnsNullopt) {
  ticketx::OrderBook book;
  book.add_limit_order(MakeLimitOrder(1, ticketx::Side::Buy, 1'200'000));

  EXPECT_FALSE(book.cancel_order(ticketx::OrderId{999}).has_value());
  EXPECT_EQ(book.size(), 1);
  EXPECT_TRUE(book.best_bid().has_value());
}

TEST(OrderBookTest, CancelBestBidRevealsNextBestBid) {
  ticketx::OrderBook book;
  book.add_limit_order(MakeLimitOrder(1, ticketx::Side::Buy, 1'100'000));
  book.add_limit_order(MakeLimitOrder(2, ticketx::Side::Buy, 1'300'000));
  book.add_limit_order(MakeLimitOrder(3, ticketx::Side::Buy, 1'200'000));

  ASSERT_TRUE(book.cancel_order(ticketx::OrderId{2}).has_value());

  const std::optional<ticketx::Order> best = book.best_bid();
  ASSERT_TRUE(best.has_value());
  EXPECT_EQ(best->id.value, 3);
  ASSERT_TRUE(best->limit_price.has_value());
  EXPECT_EQ(*best->limit_price, 1'200'000);
  EXPECT_EQ(book.size(), 2);
}

TEST(OrderBookTest, CancelBestAskRevealsNextBestAsk) {
  ticketx::OrderBook book;
  book.add_limit_order(MakeLimitOrder(1, ticketx::Side::Sell, 1'300'000));
  book.add_limit_order(MakeLimitOrder(2, ticketx::Side::Sell, 1'100'000));
  book.add_limit_order(MakeLimitOrder(3, ticketx::Side::Sell, 1'200'000));

  ASSERT_TRUE(book.cancel_order(ticketx::OrderId{2}).has_value());

  const std::optional<ticketx::Order> best = book.best_ask();
  ASSERT_TRUE(best.has_value());
  EXPECT_EQ(best->id.value, 3);
  ASSERT_TRUE(best->limit_price.has_value());
  EXPECT_EQ(*best->limit_price, 1'200'000);
  EXPECT_EQ(book.size(), 2);
}

TEST(OrderBookTest, SamePriceBidUsesFifoPriority) {
  ticketx::OrderBook book;
  book.add_limit_order(MakeLimitOrder(1, ticketx::Side::Buy, 1'200'000));
  book.add_limit_order(MakeLimitOrder(2, ticketx::Side::Buy, 1'200'000));

  const std::optional<ticketx::Order> best = book.best_bid();
  ASSERT_TRUE(best.has_value());
  EXPECT_EQ(best->id.value, 1);
}

TEST(OrderBookTest, SamePriceAskUsesFifoPriority) {
  ticketx::OrderBook book;
  book.add_limit_order(MakeLimitOrder(1, ticketx::Side::Sell, 1'200'000));
  book.add_limit_order(MakeLimitOrder(2, ticketx::Side::Sell, 1'200'000));

  const std::optional<ticketx::Order> best = book.best_ask();
  ASSERT_TRUE(best.has_value());
  EXPECT_EQ(best->id.value, 1);
}

TEST(OrderBookTest, BuyLimitBelowBestAskRestsInBidBook) {
  ticketx::OrderBook book;
  book.add_limit_order(MakeLimitOrder(1, ticketx::Side::Sell, 1'200'000));

  const ticketx::ExecutionReport report =
      book.place_limit_order(MakeLimitOrder(2, ticketx::Side::Buy, 1'100'000));

  EXPECT_EQ(report.status, ticketx::OrderStatus::Open);
  EXPECT_FALSE(report.trade.has_value());
  EXPECT_EQ(book.size(), 2);

  const std::optional<ticketx::Order> best_bid = book.best_bid();
  ASSERT_TRUE(best_bid.has_value());
  EXPECT_EQ(best_bid->id.value, 2);
}

TEST(OrderBookTest, PlaceLimitRejectsMarketOrderWithoutMutatingBook) {
  ticketx::OrderBook book;
  book.add_limit_order(MakeLimitOrder(1, ticketx::Side::Sell, 1'200'000));

  const ticketx::ExecutionReport report =
      book.place_limit_order(MakeMarketOrder(2, ticketx::Side::Buy));

  EXPECT_EQ(report.status, ticketx::OrderStatus::Rejected);
  EXPECT_FALSE(report.trade.has_value());
  EXPECT_EQ(book.size(), 1);
  ASSERT_TRUE(book.best_ask().has_value());
  EXPECT_EQ(book.best_ask()->id.value, 1);
}

TEST(OrderBookTest, PlaceLimitRejectsNonPositivePriceWithoutMutatingBook) {
  ticketx::OrderBook book;
  book.add_limit_order(MakeLimitOrder(1, ticketx::Side::Sell, 1'200'000));

  const ticketx::ExecutionReport zero_report =
      book.place_limit_order(MakeLimitOrder(2, ticketx::Side::Buy, 0));
  const ticketx::ExecutionReport negative_report =
      book.place_limit_order(MakeLimitOrder(3, ticketx::Side::Sell, -1));

  EXPECT_EQ(zero_report.status, ticketx::OrderStatus::Rejected);
  EXPECT_FALSE(zero_report.trade.has_value());
  EXPECT_EQ(negative_report.status, ticketx::OrderStatus::Rejected);
  EXPECT_FALSE(negative_report.trade.has_value());
  EXPECT_EQ(book.size(), 1);
  ASSERT_TRUE(book.best_ask().has_value());
  EXPECT_EQ(book.best_ask()->id.value, 1);
}

TEST(OrderBookTest, BuyLimitEqualToBestAskMatchesAndRemovesAsk) {
  ticketx::OrderBook book;
  book.add_limit_order(MakeLimitOrder(1, ticketx::Side::Sell, 1'200'000));

  const ticketx::ExecutionReport report =
      book.place_limit_order(MakeLimitOrder(2, ticketx::Side::Buy, 1'200'000));

  EXPECT_EQ(report.status, ticketx::OrderStatus::Filled);
  ASSERT_TRUE(report.trade.has_value());
  EXPECT_EQ(report.trade->buy_order_id.value, 2);
  EXPECT_EQ(report.trade->sell_order_id.value, 1);
  EXPECT_EQ(report.trade->price, 1'200'000);
  EXPECT_TRUE(book.empty());
  EXPECT_FALSE(book.best_ask().has_value());
}

TEST(OrderBookTest, BuyLimitAboveBestAskMatchesAtAskPrice) {
  ticketx::OrderBook book;
  book.add_limit_order(MakeLimitOrder(1, ticketx::Side::Sell, 1'150'000));

  const ticketx::ExecutionReport report =
      book.place_limit_order(MakeLimitOrder(2, ticketx::Side::Buy, 1'300'000));

  EXPECT_EQ(report.status, ticketx::OrderStatus::Filled);
  ASSERT_TRUE(report.trade.has_value());
  EXPECT_EQ(report.trade->price, 1'150'000);
  EXPECT_TRUE(book.empty());
}

TEST(OrderBookTest, SellLimitAboveBestBidRestsInAskBook) {
  ticketx::OrderBook book;
  book.add_limit_order(MakeLimitOrder(1, ticketx::Side::Buy, 1'100'000));

  const ticketx::ExecutionReport report =
      book.place_limit_order(MakeLimitOrder(2, ticketx::Side::Sell, 1'200'000));

  EXPECT_EQ(report.status, ticketx::OrderStatus::Open);
  EXPECT_FALSE(report.trade.has_value());
  EXPECT_EQ(book.size(), 2);

  const std::optional<ticketx::Order> best_ask = book.best_ask();
  ASSERT_TRUE(best_ask.has_value());
  EXPECT_EQ(best_ask->id.value, 2);
}

TEST(OrderBookTest, SellLimitEqualToBestBidMatchesAndRemovesBid) {
  ticketx::OrderBook book;
  book.add_limit_order(MakeLimitOrder(1, ticketx::Side::Buy, 1'200'000));

  const ticketx::ExecutionReport report =
      book.place_limit_order(MakeLimitOrder(2, ticketx::Side::Sell, 1'200'000));

  EXPECT_EQ(report.status, ticketx::OrderStatus::Filled);
  ASSERT_TRUE(report.trade.has_value());
  EXPECT_EQ(report.trade->buy_order_id.value, 1);
  EXPECT_EQ(report.trade->sell_order_id.value, 2);
  EXPECT_EQ(report.trade->price, 1'200'000);
  EXPECT_TRUE(book.empty());
  EXPECT_FALSE(book.best_bid().has_value());
}

TEST(OrderBookTest, SellLimitBelowBestBidMatchesAtBidPrice) {
  ticketx::OrderBook book;
  book.add_limit_order(MakeLimitOrder(1, ticketx::Side::Buy, 1'250'000));

  const ticketx::ExecutionReport report =
      book.place_limit_order(MakeLimitOrder(2, ticketx::Side::Sell, 1'100'000));

  EXPECT_EQ(report.status, ticketx::OrderStatus::Filled);
  ASSERT_TRUE(report.trade.has_value());
  EXPECT_EQ(report.trade->price, 1'250'000);
  EXPECT_TRUE(book.empty());
}

TEST(OrderBookTest, FilledMakerCannotBeCancelledAgain) {
  ticketx::OrderBook book;
  book.add_limit_order(MakeLimitOrder(1, ticketx::Side::Sell, 1'150'000));

  const ticketx::ExecutionReport report =
      book.place_limit_order(MakeLimitOrder(2, ticketx::Side::Buy, 1'150'000));

  ASSERT_EQ(report.status, ticketx::OrderStatus::Filled);
  EXPECT_FALSE(book.cancel_order(ticketx::OrderId{1}).has_value());
  EXPECT_TRUE(book.empty());
}

TEST(OrderBookTest, MarketBuyWithNoAskRejects) {
  ticketx::OrderBook book;

  const ticketx::ExecutionReport report =
      book.place_market_order(MakeMarketOrder(1, ticketx::Side::Buy));

  EXPECT_EQ(report.status, ticketx::OrderStatus::Rejected);
  EXPECT_FALSE(report.trade.has_value());
  EXPECT_TRUE(book.empty());
}

TEST(OrderBookTest, MarketBuyWithNoAskLeavesExistingBidsUntouched) {
  ticketx::OrderBook book;
  book.add_limit_order(MakeLimitOrder(1, ticketx::Side::Buy, 1'200'000));

  const ticketx::ExecutionReport report =
      book.place_market_order(MakeMarketOrder(2, ticketx::Side::Buy));

  EXPECT_EQ(report.status, ticketx::OrderStatus::Rejected);
  EXPECT_FALSE(report.trade.has_value());
  EXPECT_EQ(book.size(), 1);
  ASSERT_TRUE(book.best_bid().has_value());
  EXPECT_EQ(book.best_bid()->id.value, 1);
}

TEST(OrderBookTest, MarketSellWithNoBidRejects) {
  ticketx::OrderBook book;

  const ticketx::ExecutionReport report =
      book.place_market_order(MakeMarketOrder(1, ticketx::Side::Sell));

  EXPECT_EQ(report.status, ticketx::OrderStatus::Rejected);
  EXPECT_FALSE(report.trade.has_value());
  EXPECT_TRUE(book.empty());
}

TEST(OrderBookTest, MarketSellWithNoBidLeavesExistingAsksUntouched) {
  ticketx::OrderBook book;
  book.add_limit_order(MakeLimitOrder(1, ticketx::Side::Sell, 1'200'000));

  const ticketx::ExecutionReport report =
      book.place_market_order(MakeMarketOrder(2, ticketx::Side::Sell));

  EXPECT_EQ(report.status, ticketx::OrderStatus::Rejected);
  EXPECT_FALSE(report.trade.has_value());
  EXPECT_EQ(book.size(), 1);
  ASSERT_TRUE(book.best_ask().has_value());
  EXPECT_EQ(book.best_ask()->id.value, 1);
}

TEST(OrderBookTest, PlaceMarketRejectsLimitOrderWithoutMutatingBook) {
  ticketx::OrderBook book;
  book.add_limit_order(MakeLimitOrder(1, ticketx::Side::Sell, 1'150'000));

  const ticketx::ExecutionReport report =
      book.place_market_order(MakeLimitOrder(2, ticketx::Side::Buy, 1'200'000));

  EXPECT_EQ(report.status, ticketx::OrderStatus::Rejected);
  EXPECT_FALSE(report.trade.has_value());
  EXPECT_EQ(book.size(), 1);
  ASSERT_TRUE(book.best_ask().has_value());
  EXPECT_EQ(book.best_ask()->id.value, 1);
}

TEST(OrderBookTest, PlaceMarketRejectsDuplicateRestingOrderIdWithoutMutatingBook) {
  ticketx::OrderBook book;
  book.add_limit_order(MakeLimitOrder(1, ticketx::Side::Sell, 1'150'000));

  const ticketx::ExecutionReport report =
      book.place_market_order(MakeMarketOrder(1, ticketx::Side::Buy));

  EXPECT_EQ(report.status, ticketx::OrderStatus::Rejected);
  EXPECT_FALSE(report.trade.has_value());
  EXPECT_EQ(book.size(), 1);
  ASSERT_TRUE(book.best_ask().has_value());
  EXPECT_EQ(book.best_ask()->id.value, 1);
}

TEST(OrderBookTest, MarketBuyTakesBestAsk) {
  ticketx::OrderBook book;
  book.add_limit_order(MakeLimitOrder(1, ticketx::Side::Sell, 1'300'000));
  book.add_limit_order(MakeLimitOrder(2, ticketx::Side::Sell, 1'150'000));
  book.add_limit_order(MakeLimitOrder(3, ticketx::Side::Sell, 1'250'000));

  const ticketx::ExecutionReport report =
      book.place_market_order(MakeMarketOrder(4, ticketx::Side::Buy));

  EXPECT_EQ(report.status, ticketx::OrderStatus::Filled);
  ASSERT_TRUE(report.trade.has_value());
  EXPECT_EQ(report.trade->buy_order_id.value, 4);
  EXPECT_EQ(report.trade->sell_order_id.value, 2);
}

TEST(OrderBookTest, MarketBuyUsesFifoForSamePriceAsk) {
  ticketx::OrderBook book;
  book.add_limit_order(MakeLimitOrder(1, ticketx::Side::Sell, 1'150'000));
  book.add_limit_order(MakeLimitOrder(2, ticketx::Side::Sell, 1'150'000));

  const ticketx::ExecutionReport report =
      book.place_market_order(MakeMarketOrder(3, ticketx::Side::Buy));

  EXPECT_EQ(report.status, ticketx::OrderStatus::Filled);
  ASSERT_TRUE(report.trade.has_value());
  EXPECT_EQ(report.trade->sell_order_id.value, 1);
  ASSERT_TRUE(book.best_ask().has_value());
  EXPECT_EQ(book.best_ask()->id.value, 2);
}

TEST(OrderBookTest, MarketSellTakesBestBid) {
  ticketx::OrderBook book;
  book.add_limit_order(MakeLimitOrder(1, ticketx::Side::Buy, 1'100'000));
  book.add_limit_order(MakeLimitOrder(2, ticketx::Side::Buy, 1'300'000));
  book.add_limit_order(MakeLimitOrder(3, ticketx::Side::Buy, 1'200'000));

  const ticketx::ExecutionReport report =
      book.place_market_order(MakeMarketOrder(4, ticketx::Side::Sell));

  EXPECT_EQ(report.status, ticketx::OrderStatus::Filled);
  ASSERT_TRUE(report.trade.has_value());
  EXPECT_EQ(report.trade->buy_order_id.value, 2);
  EXPECT_EQ(report.trade->sell_order_id.value, 4);
}

TEST(OrderBookTest, MarketSellUsesFifoForSamePriceBid) {
  ticketx::OrderBook book;
  book.add_limit_order(MakeLimitOrder(1, ticketx::Side::Buy, 1'250'000));
  book.add_limit_order(MakeLimitOrder(2, ticketx::Side::Buy, 1'250'000));

  const ticketx::ExecutionReport report =
      book.place_market_order(MakeMarketOrder(3, ticketx::Side::Sell));

  EXPECT_EQ(report.status, ticketx::OrderStatus::Filled);
  ASSERT_TRUE(report.trade.has_value());
  EXPECT_EQ(report.trade->buy_order_id.value, 1);
  ASSERT_TRUE(book.best_bid().has_value());
  EXPECT_EQ(book.best_bid()->id.value, 2);
}

TEST(OrderBookTest, MarketBuyRemovesAskFromBook) {
  ticketx::OrderBook book;
  book.add_limit_order(MakeLimitOrder(1, ticketx::Side::Sell, 1'150'000));

  const ticketx::ExecutionReport report =
      book.place_market_order(MakeMarketOrder(2, ticketx::Side::Buy));

  EXPECT_EQ(report.status, ticketx::OrderStatus::Filled);
  EXPECT_TRUE(book.empty());
  EXPECT_FALSE(book.best_ask().has_value());
}

TEST(OrderBookTest, MarketSellRemovesBidFromBook) {
  ticketx::OrderBook book;
  book.add_limit_order(MakeLimitOrder(1, ticketx::Side::Buy, 1'200'000));

  const ticketx::ExecutionReport report =
      book.place_market_order(MakeMarketOrder(2, ticketx::Side::Sell));

  EXPECT_EQ(report.status, ticketx::OrderStatus::Filled);
  EXPECT_TRUE(book.empty());
  EXPECT_FALSE(book.best_bid().has_value());
}

TEST(OrderBookTest, MarketBuyFillsAtAskPrice) {
  ticketx::OrderBook book;
  book.add_limit_order(MakeLimitOrder(1, ticketx::Side::Sell, 1'150'000));

  const ticketx::ExecutionReport report =
      book.place_market_order(MakeMarketOrder(2, ticketx::Side::Buy));

  EXPECT_EQ(report.status, ticketx::OrderStatus::Filled);
  ASSERT_TRUE(report.trade.has_value());
  EXPECT_EQ(report.trade->price, 1'150'000);
}

TEST(OrderBookTest, MarketSellFillsAtBidPrice) {
  ticketx::OrderBook book;
  book.add_limit_order(MakeLimitOrder(1, ticketx::Side::Buy, 1'250'000));

  const ticketx::ExecutionReport report =
      book.place_market_order(MakeMarketOrder(2, ticketx::Side::Sell));

  EXPECT_EQ(report.status, ticketx::OrderStatus::Filled);
  ASSERT_TRUE(report.trade.has_value());
  EXPECT_EQ(report.trade->price, 1'250'000);
}
