#include "ticketx/wallet_ledger.hpp"

#include <gtest/gtest.h>

#include <limits>

namespace {

void ExpectBalance(const ticketx::WalletLedger& ledger, ticketx::UserId user_id,
                   ticketx::Money available, ticketx::Money locked) {
  const ticketx::WalletBalance balance = ledger.balance(user_id);
  EXPECT_EQ(balance.available, available);
  EXPECT_EQ(balance.locked, locked);
}

} // namespace

TEST(WalletLedgerTest, NewUserBalanceIsZero) {
  const ticketx::WalletLedger ledger;

  ExpectBalance(ledger, ticketx::UserId{1}, 0, 0);
}

TEST(WalletLedgerTest, DepositCreatesUserAndIncreasesAvailableBalance) {
  ticketx::WalletLedger ledger;

  EXPECT_TRUE(ledger.deposit(ticketx::UserId{1}, 1'000'000));
  EXPECT_TRUE(ledger.deposit(ticketx::UserId{1}, 250'000));

  ExpectBalance(ledger, ticketx::UserId{1}, 1'250'000, 0);
}

TEST(WalletLedgerTest, DepositRejectsNonPositiveAmount) {
  ticketx::WalletLedger ledger;

  EXPECT_FALSE(ledger.deposit(ticketx::UserId{1}, 0));
  EXPECT_FALSE(ledger.deposit(ticketx::UserId{1}, -1));

  ExpectBalance(ledger, ticketx::UserId{1}, 0, 0);
}

TEST(WalletLedgerTest, DepositRejectsAvailableBalanceOverflow) {
  ticketx::WalletLedger ledger;
  constexpr ticketx::Money kMaxMoney = std::numeric_limits<ticketx::Money>::max();
  ASSERT_TRUE(ledger.deposit(ticketx::UserId{1}, kMaxMoney));

  EXPECT_FALSE(ledger.deposit(ticketx::UserId{1}, 1));

  ExpectBalance(ledger, ticketx::UserId{1}, kMaxMoney, 0);
}

TEST(WalletLedgerTest, WithdrawUsesOnlyAvailableBalance) {
  ticketx::WalletLedger ledger;
  ASSERT_TRUE(ledger.deposit(ticketx::UserId{1}, 1'000'000));
  ASSERT_TRUE(ledger.lock_funds(ticketx::UserId{1}, 700'000));

  EXPECT_FALSE(ledger.withdraw(ticketx::UserId{1}, 500'000));
  EXPECT_TRUE(ledger.withdraw(ticketx::UserId{1}, 300'000));

  ExpectBalance(ledger, ticketx::UserId{1}, 0, 700'000);
}

TEST(WalletLedgerTest, WithdrawRejectsMissingInsufficientAndNonPositiveAmount) {
  ticketx::WalletLedger ledger;
  ASSERT_TRUE(ledger.deposit(ticketx::UserId{1}, 500'000));

  EXPECT_FALSE(ledger.withdraw(ticketx::UserId{2}, 1));
  EXPECT_FALSE(ledger.withdraw(ticketx::UserId{1}, 600'000));
  EXPECT_FALSE(ledger.withdraw(ticketx::UserId{1}, 0));
  EXPECT_FALSE(ledger.withdraw(ticketx::UserId{1}, -1));

  ExpectBalance(ledger, ticketx::UserId{1}, 500'000, 0);
  ExpectBalance(ledger, ticketx::UserId{2}, 0, 0);
}

TEST(WalletLedgerTest, LockFundsMovesAvailableToLocked) {
  ticketx::WalletLedger ledger;
  ASSERT_TRUE(ledger.deposit(ticketx::UserId{1}, 1'000'000));

  EXPECT_TRUE(ledger.lock_funds(ticketx::UserId{1}, 400'000));

  ExpectBalance(ledger, ticketx::UserId{1}, 600'000, 400'000);
}

TEST(WalletLedgerTest, LockFundsRejectsMissingInsufficientAndNonPositiveAmount) {
  ticketx::WalletLedger ledger;
  ASSERT_TRUE(ledger.deposit(ticketx::UserId{1}, 500'000));

  EXPECT_FALSE(ledger.lock_funds(ticketx::UserId{2}, 1));
  EXPECT_FALSE(ledger.lock_funds(ticketx::UserId{1}, 600'000));
  EXPECT_FALSE(ledger.lock_funds(ticketx::UserId{1}, 0));
  EXPECT_FALSE(ledger.lock_funds(ticketx::UserId{1}, -1));

  ExpectBalance(ledger, ticketx::UserId{1}, 500'000, 0);
  ExpectBalance(ledger, ticketx::UserId{2}, 0, 0);
}

TEST(WalletLedgerTest, LockFundsRejectsLockedBalanceOverflow) {
  ticketx::WalletLedger ledger;
  constexpr ticketx::Money kMaxMoney = std::numeric_limits<ticketx::Money>::max();
  ASSERT_TRUE(ledger.deposit(ticketx::UserId{1}, kMaxMoney));
  ASSERT_TRUE(ledger.lock_funds(ticketx::UserId{1}, kMaxMoney - 1));
  ASSERT_TRUE(ledger.credit(ticketx::UserId{1}, 2));

  EXPECT_FALSE(ledger.lock_funds(ticketx::UserId{1}, 2));

  ExpectBalance(ledger, ticketx::UserId{1}, 3, kMaxMoney - 1);
}

TEST(WalletLedgerTest, UnlockFundsMovesLockedBackToAvailable) {
  ticketx::WalletLedger ledger;
  ASSERT_TRUE(ledger.deposit(ticketx::UserId{1}, 1'000'000));
  ASSERT_TRUE(ledger.lock_funds(ticketx::UserId{1}, 700'000));

  EXPECT_TRUE(ledger.unlock_funds(ticketx::UserId{1}, 250'000));

  ExpectBalance(ledger, ticketx::UserId{1}, 550'000, 450'000);
}

TEST(WalletLedgerTest, UnlockFundsRejectsMissingInsufficientAndNonPositiveAmount) {
  ticketx::WalletLedger ledger;
  ASSERT_TRUE(ledger.deposit(ticketx::UserId{1}, 500'000));
  ASSERT_TRUE(ledger.lock_funds(ticketx::UserId{1}, 200'000));

  EXPECT_FALSE(ledger.unlock_funds(ticketx::UserId{2}, 1));
  EXPECT_FALSE(ledger.unlock_funds(ticketx::UserId{1}, 300'000));
  EXPECT_FALSE(ledger.unlock_funds(ticketx::UserId{1}, 0));
  EXPECT_FALSE(ledger.unlock_funds(ticketx::UserId{1}, -1));

  ExpectBalance(ledger, ticketx::UserId{1}, 300'000, 200'000);
  ExpectBalance(ledger, ticketx::UserId{2}, 0, 0);
}

TEST(WalletLedgerTest, UnlockFundsRejectsAvailableBalanceOverflow) {
  ticketx::WalletLedger ledger;
  constexpr ticketx::Money kMaxMoney = std::numeric_limits<ticketx::Money>::max();
  ASSERT_TRUE(ledger.deposit(ticketx::UserId{1}, 100));
  ASSERT_TRUE(ledger.lock_funds(ticketx::UserId{1}, 100));
  ASSERT_TRUE(ledger.credit(ticketx::UserId{1}, kMaxMoney));

  EXPECT_FALSE(ledger.unlock_funds(ticketx::UserId{1}, 1));

  ExpectBalance(ledger, ticketx::UserId{1}, kMaxMoney, 100);
}

TEST(WalletLedgerTest, DebitLockedConsumesLockedFunds) {
  ticketx::WalletLedger ledger;
  ASSERT_TRUE(ledger.deposit(ticketx::UserId{1}, 1'000'000));
  ASSERT_TRUE(ledger.lock_funds(ticketx::UserId{1}, 700'000));

  EXPECT_TRUE(ledger.debit_locked(ticketx::UserId{1}, 450'000));

  ExpectBalance(ledger, ticketx::UserId{1}, 300'000, 250'000);
}

TEST(WalletLedgerTest, DebitLockedRejectsMissingInsufficientAndNonPositiveAmount) {
  ticketx::WalletLedger ledger;
  ASSERT_TRUE(ledger.deposit(ticketx::UserId{1}, 500'000));
  ASSERT_TRUE(ledger.lock_funds(ticketx::UserId{1}, 200'000));

  EXPECT_FALSE(ledger.debit_locked(ticketx::UserId{2}, 1));
  EXPECT_FALSE(ledger.debit_locked(ticketx::UserId{1}, 300'000));
  EXPECT_FALSE(ledger.debit_locked(ticketx::UserId{1}, 0));
  EXPECT_FALSE(ledger.debit_locked(ticketx::UserId{1}, -1));

  ExpectBalance(ledger, ticketx::UserId{1}, 300'000, 200'000);
  ExpectBalance(ledger, ticketx::UserId{2}, 0, 0);
}

TEST(WalletLedgerTest, CreditCreatesUserAndIncreasesAvailableBalance) {
  ticketx::WalletLedger ledger;

  EXPECT_TRUE(ledger.credit(ticketx::UserId{1}, 800'000));
  EXPECT_TRUE(ledger.credit(ticketx::UserId{1}, 200'000));

  ExpectBalance(ledger, ticketx::UserId{1}, 1'000'000, 0);
}

TEST(WalletLedgerTest, CreditRejectsNonPositiveAmount) {
  ticketx::WalletLedger ledger;

  EXPECT_FALSE(ledger.credit(ticketx::UserId{1}, 0));
  EXPECT_FALSE(ledger.credit(ticketx::UserId{1}, -1));

  ExpectBalance(ledger, ticketx::UserId{1}, 0, 0);
}

TEST(WalletLedgerTest, CreditRejectsAvailableBalanceOverflow) {
  ticketx::WalletLedger ledger;
  constexpr ticketx::Money kMaxMoney = std::numeric_limits<ticketx::Money>::max();
  ASSERT_TRUE(ledger.credit(ticketx::UserId{1}, kMaxMoney));

  EXPECT_FALSE(ledger.credit(ticketx::UserId{1}, 1));

  ExpectBalance(ledger, ticketx::UserId{1}, kMaxMoney, 0);
}
