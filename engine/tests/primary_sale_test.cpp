#include "ticketx/ticketx_engine.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace {

ticketx::Event MakeEvent(std::uint64_t event_id = 10, std::uint64_t remaining = 500,
                         ticketx::Money price = 1'000'000) {
  return ticketx::Event{
      .id = ticketx::EventId{event_id},
      .name = "TicketX Live",
      .categories =
          std::vector<ticketx::PrimarySaleCategory>{
              ticketx::PrimarySaleCategory{
                  .name = "standard",
                  .price = price,
                  .remaining = remaining,
              },
              ticketx::PrimarySaleCategory{
                  .name = "vip",
                  .price = 2'000'000,
                  .remaining = 100,
              },
          },
  };
}

const ticketx::PrimarySaleCategory* FindCategory(const ticketx::Event& event,
                                                 const std::string& category) {
  for (const ticketx::PrimarySaleCategory& candidate : event.categories) {
    if (candidate.name == category) {
      return &candidate;
    }
  }
  return nullptr;
}

} // namespace

TEST(PrimarySaleTest, CreateEventStoresQueryableInventory) {
  ticketx::TicketXEngine engine;

  ASSERT_TRUE(engine.create_event(MakeEvent()));
  ASSERT_EQ(engine.event_log().size(), 1U);
  EXPECT_EQ(engine.event_log()[0].type, std::string{ticketx::event_type::EventCreated});
  EXPECT_NE(engine.event_log()[0].payload_json.find("\"event_id\":10"), std::string::npos);

  const std::optional<ticketx::Event> event = engine.event(ticketx::EventId{10});
  ASSERT_TRUE(event.has_value());
  EXPECT_EQ(event->name, "TicketX Live");

  const ticketx::PrimarySaleCategory* standard = FindCategory(*event, "standard");
  ASSERT_NE(standard, nullptr);
  EXPECT_EQ(standard->price, 1'000'000);
  EXPECT_EQ(standard->remaining, 500);
}

TEST(PrimarySaleTest, CreateEventRejectsInvalidDefinitions) {
  ticketx::TicketXEngine engine;

  ticketx::Event empty_name = MakeEvent(1);
  empty_name.name.clear();
  EXPECT_FALSE(engine.create_event(empty_name));

  ticketx::Event empty_categories = MakeEvent(2);
  empty_categories.categories.clear();
  EXPECT_FALSE(engine.create_event(empty_categories));

  ticketx::Event empty_category_name = MakeEvent(3);
  empty_category_name.categories.front().name.clear();
  EXPECT_FALSE(engine.create_event(empty_category_name));

  ticketx::Event zero_price = MakeEvent(4);
  zero_price.categories.front().price = 0;
  EXPECT_FALSE(engine.create_event(zero_price));

  ticketx::Event negative_price = MakeEvent(5);
  negative_price.categories.front().price = -1;
  EXPECT_FALSE(engine.create_event(negative_price));

  ticketx::Event duplicate_category = MakeEvent(6);
  duplicate_category.categories.back().name = "standard";
  EXPECT_FALSE(engine.create_event(duplicate_category));
  EXPECT_TRUE(engine.event_log().empty());

  ASSERT_TRUE(engine.create_event(MakeEvent(7)));
  EXPECT_EQ(engine.event_log().size(), 1U);
  EXPECT_FALSE(engine.create_event(MakeEvent(7)));
  EXPECT_EQ(engine.event_log().size(), 1U);
}

TEST(PrimarySaleTest, PrimaryBuySuccessDebitsWalletIssuesTicketAndDecrementsInventory) {
  ticketx::TicketXEngine engine;
  ASSERT_TRUE(engine.create_event(MakeEvent()));
  ASSERT_TRUE(engine.deposit(ticketx::UserId{100}, 1'500'000));

  const ticketx::PrimaryBuyResult result =
      engine.primary_buy(ticketx::UserId{100}, ticketx::EventId{10}, "standard");

  EXPECT_EQ(result.status, ticketx::PrimaryBuyStatus::Accepted);
  EXPECT_TRUE(result.accepted());
  ASSERT_TRUE(result.ticket.has_value());
  EXPECT_EQ(result.ticket->id.value, 1);
  EXPECT_EQ(result.ticket->owner_user_id.value, 100);
  EXPECT_EQ(result.ticket->event_id.value, 10);
  EXPECT_EQ(result.ticket->category, "standard");

  ASSERT_EQ(engine.event_log().size(), 3U);
  EXPECT_EQ(engine.event_log()[0].type, std::string{ticketx::event_type::EventCreated});
  EXPECT_EQ(engine.event_log()[1].type, std::string{ticketx::event_type::WalletDeposited});
  EXPECT_EQ(engine.event_log()[2].type, std::string{ticketx::event_type::PrimaryTicketBought});
  EXPECT_NE(engine.event_log()[2].payload_json.find("\"ticket_id\":1"), std::string::npos);
  EXPECT_NE(engine.event_log()[2].payload_json.find("\"price\":1000000"), std::string::npos);

  const ticketx::WalletBalance balance = engine.wallet_balance(ticketx::UserId{100});
  EXPECT_EQ(balance.available, 500'000);
  EXPECT_EQ(balance.locked, 0);

  const std::optional<ticketx::Ticket> active_ticket =
      engine.active_ticket(ticketx::UserId{100}, ticketx::EventId{10});
  ASSERT_TRUE(active_ticket.has_value());
  EXPECT_EQ(active_ticket->id.value, 1);

  const std::optional<ticketx::Event> event = engine.event(ticketx::EventId{10});
  ASSERT_TRUE(event.has_value());
  const ticketx::PrimarySaleCategory* standard = FindCategory(*event, "standard");
  ASSERT_NE(standard, nullptr);
  EXPECT_EQ(standard->remaining, 499);
}

TEST(PrimarySaleTest, PrimaryBuyRejectsMissingEventAndCategoryWithoutMutatingWallet) {
  ticketx::TicketXEngine engine;
  ASSERT_TRUE(engine.deposit(ticketx::UserId{100}, 2'000'000));

  const ticketx::PrimaryBuyResult missing_event =
      engine.primary_buy(ticketx::UserId{100}, ticketx::EventId{404}, "standard");
  EXPECT_EQ(missing_event.status, ticketx::PrimaryBuyStatus::EventNotFound);
  EXPECT_FALSE(missing_event.ticket.has_value());

  ASSERT_TRUE(engine.create_event(MakeEvent()));
  const ticketx::PrimaryBuyResult missing_category =
      engine.primary_buy(ticketx::UserId{100}, ticketx::EventId{10}, "balcony");
  EXPECT_EQ(missing_category.status, ticketx::PrimaryBuyStatus::CategoryNotFound);
  EXPECT_FALSE(missing_category.ticket.has_value());

  const ticketx::WalletBalance balance = engine.wallet_balance(ticketx::UserId{100});
  EXPECT_EQ(balance.available, 2'000'000);
  EXPECT_EQ(balance.locked, 0);
}

TEST(PrimarySaleTest, PrimaryBuyRejectsSoldOutCategoryWithoutMutatingWallet) {
  ticketx::TicketXEngine engine;
  ASSERT_TRUE(engine.create_event(MakeEvent(10, 0)));
  ASSERT_TRUE(engine.deposit(ticketx::UserId{100}, 2'000'000));

  const ticketx::PrimaryBuyResult result =
      engine.primary_buy(ticketx::UserId{100}, ticketx::EventId{10}, "standard");

  EXPECT_EQ(result.status, ticketx::PrimaryBuyStatus::SoldOut);
  EXPECT_FALSE(result.ticket.has_value());
  EXPECT_EQ(engine.wallet_balance(ticketx::UserId{100}).available, 2'000'000);
}

TEST(PrimarySaleTest, PrimaryBuyRejectsInsufficientFundsWithoutDecrementingInventory) {
  ticketx::TicketXEngine engine;
  ASSERT_TRUE(engine.create_event(MakeEvent()));
  ASSERT_TRUE(engine.deposit(ticketx::UserId{100}, 999'999));

  const ticketx::PrimaryBuyResult result =
      engine.primary_buy(ticketx::UserId{100}, ticketx::EventId{10}, "standard");

  EXPECT_EQ(result.status, ticketx::PrimaryBuyStatus::InsufficientFunds);
  EXPECT_FALSE(result.ticket.has_value());
  EXPECT_EQ(engine.wallet_balance(ticketx::UserId{100}).available, 999'999);
  EXPECT_FALSE(engine.active_ticket(ticketx::UserId{100}, ticketx::EventId{10}).has_value());

  const std::optional<ticketx::Event> event = engine.event(ticketx::EventId{10});
  ASSERT_TRUE(event.has_value());
  const ticketx::PrimarySaleCategory* standard = FindCategory(*event, "standard");
  ASSERT_NE(standard, nullptr);
  EXPECT_EQ(standard->remaining, 500);
}

TEST(PrimarySaleTest, PrimaryBuyRejectsBuyerWhoAlreadyOwnsTicketForEvent) {
  ticketx::TicketXEngine engine;
  ASSERT_TRUE(engine.create_event(MakeEvent()));
  ASSERT_TRUE(engine.deposit(ticketx::UserId{100}, 3'000'000));
  ASSERT_EQ(engine.primary_buy(ticketx::UserId{100}, ticketx::EventId{10}, "standard").status,
            ticketx::PrimaryBuyStatus::Accepted);

  const ticketx::PrimaryBuyResult second_buy =
      engine.primary_buy(ticketx::UserId{100}, ticketx::EventId{10}, "vip");

  EXPECT_EQ(second_buy.status, ticketx::PrimaryBuyStatus::BuyerAlreadyOwnsTicket);
  EXPECT_FALSE(second_buy.ticket.has_value());
  EXPECT_EQ(engine.wallet_balance(ticketx::UserId{100}).available, 2'000'000);

  const std::optional<ticketx::Event> event = engine.event(ticketx::EventId{10});
  ASSERT_TRUE(event.has_value());
  const ticketx::PrimarySaleCategory* vip = FindCategory(*event, "vip");
  ASSERT_NE(vip, nullptr);
  EXPECT_EQ(vip->remaining, 100);
}

TEST(PrimarySaleTest, PrimaryBuyRejectsBuyerWithOpenBuyOrderForSameEvent) {
  ticketx::TicketXEngine engine;
  ASSERT_TRUE(engine.create_event(MakeEvent()));
  ASSERT_TRUE(engine.deposit(ticketx::UserId{100}, 2'000'000));
  ASSERT_EQ(engine.place_limit_order(ticketx::Order{
                .id = ticketx::OrderId{1},
                .user_id = ticketx::UserId{100},
                .event_id = ticketx::EventId{10},
                .category = "standard",
                .side = ticketx::Side::Buy,
                .type = ticketx::OrderType::Limit,
                .limit_price = 1'000'000,
            })
                .status,
            ticketx::OrderStatus::Open);

  const ticketx::PrimaryBuyResult result =
      engine.primary_buy(ticketx::UserId{100}, ticketx::EventId{10}, "standard");

  EXPECT_EQ(result.status, ticketx::PrimaryBuyStatus::BuyerAlreadyOwnsTicket);
  EXPECT_FALSE(result.ticket.has_value());
  const ticketx::WalletBalance balance = engine.wallet_balance(ticketx::UserId{100});
  EXPECT_EQ(balance.available, 1'000'000);
  EXPECT_EQ(balance.locked, 1'000'000);
}

TEST(PrimarySaleTest, PrimarySaleBurstDoesNotOversell) {
  ticketx::TicketXEngine engine;
  ASSERT_TRUE(engine.create_event(MakeEvent(10, 500)));

  std::uint64_t accepted_count = 0;
  for (std::uint64_t i = 0; i < 10'000; ++i) {
    const ticketx::UserId user_id{i + 1};
    ASSERT_TRUE(engine.deposit(user_id, 1'000'000));

    const ticketx::PrimaryBuyResult result =
        engine.primary_buy(user_id, ticketx::EventId{10}, "standard");
    if (result.status == ticketx::PrimaryBuyStatus::Accepted) {
      ++accepted_count;
      ASSERT_TRUE(result.ticket.has_value());
      EXPECT_TRUE(engine.active_ticket(user_id, ticketx::EventId{10}).has_value());
    } else {
      EXPECT_EQ(result.status, ticketx::PrimaryBuyStatus::SoldOut);
      EXPECT_FALSE(engine.active_ticket(user_id, ticketx::EventId{10}).has_value());
    }
  }

  EXPECT_EQ(accepted_count, 500);
  const std::optional<ticketx::Event> event = engine.event(ticketx::EventId{10});
  ASSERT_TRUE(event.has_value());
  const ticketx::PrimarySaleCategory* standard = FindCategory(*event, "standard");
  ASSERT_NE(standard, nullptr);
  EXPECT_EQ(standard->remaining, 0);
}
