#include "ticketx/matching_engine.hpp"
#include "ticketx/version.hpp"
#include "ticketx/wallet_ledger.hpp"

#include <gtest/gtest.h>

#include <type_traits>

TEST(TicketXSmokeTest, MoneyIsIntegerBacked) {
  static_assert(std::is_same_v<ticketx::Money, std::int64_t>);
  SUCCEED();
}

TEST(TicketXSmokeTest, EngineStartsWithNoEvents) {
  const ticketx::MatchingEngine engine;
  EXPECT_EQ(ticketx::version(), "0.1.0");
  EXPECT_EQ(engine.name(), "TicketX MatchingEngine");
  EXPECT_TRUE(engine.event_log().empty());
}

TEST(TicketXSmokeTest, WalletDefaultsToZero) {
  const ticketx::WalletBalance balance;
  EXPECT_EQ(balance.available, 0);
  EXPECT_EQ(balance.locked, 0);
}
