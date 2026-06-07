#include "ticketx/matching_engine.hpp"
#include "ticketx/ticket_ledger.hpp"
#include "ticketx/version.hpp"
#include "ticketx/wallet_ledger.hpp"

namespace ticketx {

static_assert(sizeof(Money) == sizeof(std::int64_t));

std::string_view version() noexcept { return "0.1.0"; }

} // namespace ticketx
