#include "ticketx/wallet_ledger.hpp"

#include <limits>

namespace ticketx {

namespace {

bool IsPositive(Money amount) { return amount > 0; }

bool CanAdd(Money current, Money amount) {
  return current >= 0 && amount <= std::numeric_limits<Money>::max() - current;
}

} // namespace

WalletBalance WalletLedger::balance(UserId user_id) const {
  const auto it = balances_.find(user_id.value);
  if (it == balances_.end()) {
    return WalletBalance{};
  }
  return it->second;
}

bool WalletLedger::deposit(UserId user_id, Money amount) {
  if (!IsPositive(amount)) {
    return false;
  }

  WalletBalance& balance = balances_[user_id.value];
  if (!CanAdd(balance.available, amount)) {
    return false;
  }

  balance.available += amount;
  return true;
}

bool WalletLedger::withdraw(UserId user_id, Money amount) {
  if (!IsPositive(amount)) {
    return false;
  }

  auto it = balances_.find(user_id.value);
  if (it == balances_.end() || it->second.available < amount) {
    return false;
  }

  it->second.available -= amount;
  return true;
}

bool WalletLedger::lock_funds(UserId user_id, Money amount) {
  if (!IsPositive(amount)) {
    return false;
  }

  auto it = balances_.find(user_id.value);
  if (it == balances_.end() || it->second.available < amount) {
    return false;
  }
  if (!CanAdd(it->second.locked, amount)) {
    return false;
  }

  it->second.available -= amount;
  it->second.locked += amount;
  return true;
}

bool WalletLedger::unlock_funds(UserId user_id, Money amount) {
  if (!IsPositive(amount)) {
    return false;
  }

  auto it = balances_.find(user_id.value);
  if (it == balances_.end() || it->second.locked < amount) {
    return false;
  }
  if (!CanAdd(it->second.available, amount)) {
    return false;
  }

  it->second.locked -= amount;
  it->second.available += amount;
  return true;
}

bool WalletLedger::debit_locked(UserId user_id, Money amount) {
  if (!IsPositive(amount)) {
    return false;
  }

  auto it = balances_.find(user_id.value);
  if (it == balances_.end() || it->second.locked < amount) {
    return false;
  }

  it->second.locked -= amount;
  return true;
}

bool WalletLedger::credit(UserId user_id, Money amount) {
  if (!IsPositive(amount)) {
    return false;
  }

  WalletBalance& balance = balances_[user_id.value];
  if (!CanAdd(balance.available, amount)) {
    return false;
  }

  balance.available += amount;
  return true;
}

} // namespace ticketx
