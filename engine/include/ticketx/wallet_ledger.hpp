#pragma once

#include "ticketx/types.hpp"

#include <cstdint>
#include <unordered_map>

namespace ticketx {

struct WalletBalance {
  Money available{0};
  Money locked{0};
};

class WalletLedger {
public:
  WalletBalance balance(UserId user_id) const;

  bool deposit(UserId user_id, Money amount);
  bool withdraw(UserId user_id, Money amount);

  bool lock_funds(UserId user_id, Money amount);
  bool unlock_funds(UserId user_id, Money amount);

  bool debit_locked(UserId user_id, Money amount);
  bool credit(UserId user_id, Money amount);

private:
  std::unordered_map<std::uint64_t, WalletBalance> balances_;
};

} // namespace ticketx
