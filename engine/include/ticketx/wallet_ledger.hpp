#pragma once

#include "ticketx/types.hpp"

namespace ticketx {

struct WalletBalance {
  Money available{0};
  Money locked{0};
};

class WalletLedger {};

} // namespace ticketx
