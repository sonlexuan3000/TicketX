#include "ticketx/ticket_ledger.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace {

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

} // namespace

TEST(TicketLedgerTest, NewLedgerHasNoTickets) {
  const ticketx::TicketLedger ledger;

  EXPECT_FALSE(ledger.ticket(ticketx::TicketId{1}).has_value());
  EXPECT_FALSE(ledger.active_ticket(ticketx::UserId{1}, ticketx::EventId{10}).has_value());
  EXPECT_FALSE(ledger.owns_active_ticket(ticketx::UserId{1}, ticketx::EventId{10}));
}

TEST(TicketLedgerTest, IssueTicketCreatesActiveUnlockedTicket) {
  ticketx::TicketLedger ledger;

  ASSERT_TRUE(ledger.issue_ticket(MakeTicket(1, 100)));

  const std::optional<ticketx::Ticket> ticket = ledger.ticket(ticketx::TicketId{1});
  ASSERT_TRUE(ticket.has_value());
  EXPECT_EQ(ticket->owner_user_id.value, 100);
  EXPECT_EQ(ticket->event_id.value, 10);
  EXPECT_EQ(ticket->category, "standard");
  EXPECT_EQ(ticket->status, ticketx::TicketStatus::Owned);

  EXPECT_TRUE(ledger.owns_active_ticket(ticketx::UserId{100}, ticketx::EventId{10}));
  EXPECT_TRUE(ledger.unlocked_ticket(ticketx::UserId{100}, ticketx::EventId{10}, "standard")
                  .has_value());
  EXPECT_FALSE(ledger.locked_ticket(ticketx::UserId{100}, ticketx::EventId{10}, "standard")
                   .has_value());
}

TEST(TicketLedgerTest, IssueTicketRejectsDuplicateTicketId) {
  ticketx::TicketLedger ledger;
  ASSERT_TRUE(ledger.issue_ticket(MakeTicket(1, 100)));

  EXPECT_FALSE(ledger.issue_ticket(MakeTicket(1, 200, 20, "vip")));

  const std::optional<ticketx::Ticket> ticket = ledger.ticket(ticketx::TicketId{1});
  ASSERT_TRUE(ticket.has_value());
  EXPECT_EQ(ticket->owner_user_id.value, 100);
  EXPECT_EQ(ticket->event_id.value, 10);
  EXPECT_EQ(ticket->category, "standard");
}

TEST(TicketLedgerTest, IssueTicketRejectsSecondActiveTicketForSameUserAndEvent) {
  ticketx::TicketLedger ledger;
  ASSERT_TRUE(ledger.issue_ticket(MakeTicket(1, 100, 10, "standard")));

  EXPECT_FALSE(ledger.issue_ticket(MakeTicket(2, 100, 10, "vip")));
  EXPECT_TRUE(ledger.issue_ticket(MakeTicket(3, 100, 20, "vip")));
  EXPECT_TRUE(ledger.issue_ticket(MakeTicket(4, 200, 10, "vip")));

  EXPECT_FALSE(ledger.ticket(ticketx::TicketId{2}).has_value());
  EXPECT_TRUE(ledger.ticket(ticketx::TicketId{3}).has_value());
  EXPECT_TRUE(ledger.ticket(ticketx::TicketId{4}).has_value());
}

TEST(TicketLedgerTest, IssueTicketRejectsEmptyCategoryAndNonOwnedStatus) {
  ticketx::TicketLedger ledger;
  ticketx::Ticket empty_category = MakeTicket(1, 100, 10, "");
  ticketx::Ticket locked_ticket = MakeTicket(2, 100, 20, "standard");
  locked_ticket.status = ticketx::TicketStatus::LockedForSell;

  EXPECT_FALSE(ledger.issue_ticket(empty_category));
  EXPECT_FALSE(ledger.issue_ticket(locked_ticket));

  EXPECT_FALSE(ledger.ticket(ticketx::TicketId{1}).has_value());
  EXPECT_FALSE(ledger.ticket(ticketx::TicketId{2}).has_value());
}

TEST(TicketLedgerTest, LockTicketMovesUnlockedTicketToLocked) {
  ticketx::TicketLedger ledger;
  ASSERT_TRUE(ledger.issue_ticket(MakeTicket(1, 100)));

  const std::optional<ticketx::Ticket> locked =
      ledger.lock_ticket(ticketx::UserId{100}, ticketx::EventId{10}, "standard");

  ASSERT_TRUE(locked.has_value());
  EXPECT_EQ(locked->id.value, 1);
  EXPECT_EQ(locked->status, ticketx::TicketStatus::LockedForSell);
  EXPECT_TRUE(ledger.owns_active_ticket(ticketx::UserId{100}, ticketx::EventId{10}));
  EXPECT_FALSE(ledger.unlocked_ticket(ticketx::UserId{100}, ticketx::EventId{10}, "standard")
                   .has_value());
  EXPECT_TRUE(ledger.locked_ticket(ticketx::UserId{100}, ticketx::EventId{10}, "standard")
                  .has_value());
}

TEST(TicketLedgerTest, LockTicketRejectsMissingWrongCategoryAndDoubleLock) {
  ticketx::TicketLedger ledger;
  ASSERT_TRUE(ledger.issue_ticket(MakeTicket(1, 100, 10, "standard")));

  EXPECT_FALSE(ledger.lock_ticket(ticketx::UserId{999}, ticketx::EventId{10}, "standard")
                   .has_value());
  EXPECT_FALSE(ledger.lock_ticket(ticketx::UserId{100}, ticketx::EventId{10}, "vip")
                   .has_value());

  ASSERT_TRUE(ledger.lock_ticket(ticketx::UserId{100}, ticketx::EventId{10}, "standard")
                  .has_value());
  EXPECT_FALSE(ledger.lock_ticket(ticketx::UserId{100}, ticketx::EventId{10}, "standard")
                   .has_value());

  const std::optional<ticketx::Ticket> locked =
      ledger.locked_ticket(ticketx::UserId{100}, ticketx::EventId{10}, "standard");
  ASSERT_TRUE(locked.has_value());
  EXPECT_EQ(locked->status, ticketx::TicketStatus::LockedForSell);
}

TEST(TicketLedgerTest, UnlockTicketMovesLockedTicketBackToUnlocked) {
  ticketx::TicketLedger ledger;
  ASSERT_TRUE(ledger.issue_ticket(MakeTicket(1, 100)));
  ASSERT_TRUE(
      ledger.lock_ticket(ticketx::UserId{100}, ticketx::EventId{10}, "standard").has_value());

  const std::optional<ticketx::Ticket> unlocked =
      ledger.unlock_ticket(ticketx::UserId{100}, ticketx::EventId{10}, "standard");

  ASSERT_TRUE(unlocked.has_value());
  EXPECT_EQ(unlocked->id.value, 1);
  EXPECT_EQ(unlocked->status, ticketx::TicketStatus::Owned);
  EXPECT_TRUE(ledger.unlocked_ticket(ticketx::UserId{100}, ticketx::EventId{10}, "standard")
                  .has_value());
  EXPECT_FALSE(ledger.locked_ticket(ticketx::UserId{100}, ticketx::EventId{10}, "standard")
                   .has_value());
}

TEST(TicketLedgerTest, UnlockTicketRejectsMissingUnlockedAndWrongCategory) {
  ticketx::TicketLedger ledger;
  ASSERT_TRUE(ledger.issue_ticket(MakeTicket(1, 100, 10, "standard")));

  EXPECT_FALSE(ledger.unlock_ticket(ticketx::UserId{100}, ticketx::EventId{10}, "standard")
                   .has_value());
  EXPECT_FALSE(ledger.unlock_ticket(ticketx::UserId{100}, ticketx::EventId{10}, "vip")
                   .has_value());

  ASSERT_TRUE(ledger.lock_ticket(ticketx::UserId{100}, ticketx::EventId{10}, "standard")
                  .has_value());
  EXPECT_FALSE(ledger.unlock_ticket(ticketx::UserId{999}, ticketx::EventId{10}, "standard")
                   .has_value());

  const std::optional<ticketx::Ticket> locked =
      ledger.locked_ticket(ticketx::UserId{100}, ticketx::EventId{10}, "standard");
  ASSERT_TRUE(locked.has_value());
  EXPECT_EQ(locked->status, ticketx::TicketStatus::LockedForSell);
}

TEST(TicketLedgerTest, TransferUnlockedTicketChangesOwnerAndCredentialVersion) {
  ticketx::TicketLedger ledger;
  ASSERT_TRUE(ledger.issue_ticket(MakeTicket(1, 100, 10, "standard")));

  const std::optional<ticketx::Ticket> transferred =
      ledger.transfer_ticket(ticketx::UserId{100}, ticketx::UserId{200}, ticketx::EventId{10},
                             "standard");

  ASSERT_TRUE(transferred.has_value());
  EXPECT_EQ(transferred->id.value, 1);
  EXPECT_EQ(transferred->owner_user_id.value, 200);
  EXPECT_EQ(transferred->status, ticketx::TicketStatus::Owned);
  EXPECT_EQ(transferred->credential_version, 2);

  EXPECT_FALSE(ledger.owns_active_ticket(ticketx::UserId{100}, ticketx::EventId{10}));
  EXPECT_TRUE(ledger.owns_active_ticket(ticketx::UserId{200}, ticketx::EventId{10}));
  EXPECT_FALSE(ledger.unlocked_ticket(ticketx::UserId{100}, ticketx::EventId{10}, "standard")
                   .has_value());
  EXPECT_TRUE(ledger.unlocked_ticket(ticketx::UserId{200}, ticketx::EventId{10}, "standard")
                  .has_value());
}

TEST(TicketLedgerTest, TransferLockedTicketChangesOwnerAndClearsLockedState) {
  ticketx::TicketLedger ledger;
  ASSERT_TRUE(ledger.issue_ticket(MakeTicket(1, 100, 10, "standard")));
  ASSERT_TRUE(ledger.lock_ticket(ticketx::UserId{100}, ticketx::EventId{10}, "standard")
                  .has_value());

  const std::optional<ticketx::Ticket> transferred =
      ledger.transfer_ticket(ticketx::UserId{100}, ticketx::UserId{200}, ticketx::EventId{10},
                             "standard");

  ASSERT_TRUE(transferred.has_value());
  EXPECT_EQ(transferred->owner_user_id.value, 200);
  EXPECT_EQ(transferred->status, ticketx::TicketStatus::Owned);
  EXPECT_FALSE(ledger.locked_ticket(ticketx::UserId{100}, ticketx::EventId{10}, "standard")
                   .has_value());
  EXPECT_TRUE(ledger.unlocked_ticket(ticketx::UserId{200}, ticketx::EventId{10}, "standard")
                  .has_value());
}

TEST(TicketLedgerTest, TransferRejectsWhenBuyerAlreadyHasActiveTicketForEvent) {
  ticketx::TicketLedger ledger;
  ASSERT_TRUE(ledger.issue_ticket(MakeTicket(1, 100, 10, "standard")));
  ASSERT_TRUE(ledger.issue_ticket(MakeTicket(2, 200, 10, "vip")));

  EXPECT_FALSE(ledger.transfer_ticket(ticketx::UserId{100}, ticketx::UserId{200},
                                      ticketx::EventId{10}, "standard")
                   .has_value());

  const std::optional<ticketx::Ticket> seller_ticket =
      ledger.unlocked_ticket(ticketx::UserId{100}, ticketx::EventId{10}, "standard");
  ASSERT_TRUE(seller_ticket.has_value());
  EXPECT_EQ(seller_ticket->owner_user_id.value, 100);
}

TEST(TicketLedgerTest, TransferRejectsMissingSellerTicketAndWrongCategory) {
  ticketx::TicketLedger ledger;
  ASSERT_TRUE(ledger.issue_ticket(MakeTicket(1, 100, 10, "standard")));

  EXPECT_FALSE(ledger.transfer_ticket(ticketx::UserId{999}, ticketx::UserId{200},
                                      ticketx::EventId{10}, "standard")
                   .has_value());
  EXPECT_FALSE(ledger.transfer_ticket(ticketx::UserId{100}, ticketx::UserId{200},
                                      ticketx::EventId{10}, "vip")
                   .has_value());

  EXPECT_TRUE(ledger.owns_active_ticket(ticketx::UserId{100}, ticketx::EventId{10}));
  EXPECT_FALSE(ledger.owns_active_ticket(ticketx::UserId{200}, ticketx::EventId{10}));
}

TEST(TicketLedgerTest, TransferRejectsCredentialVersionOverflow) {
  ticketx::TicketLedger ledger;
  ticketx::Ticket ticket = MakeTicket(1, 100, 10, "standard");
  ticket.credential_version = std::numeric_limits<std::uint64_t>::max();
  ASSERT_TRUE(ledger.issue_ticket(ticket));

  EXPECT_FALSE(ledger.transfer_ticket(ticketx::UserId{100}, ticketx::UserId{200},
                                      ticketx::EventId{10}, "standard")
                   .has_value());

  const std::optional<ticketx::Ticket> unchanged = ledger.ticket(ticketx::TicketId{1});
  ASSERT_TRUE(unchanged.has_value());
  EXPECT_EQ(unchanged->owner_user_id.value, 100);
  EXPECT_EQ(unchanged->credential_version, std::numeric_limits<std::uint64_t>::max());
}
