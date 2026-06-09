#pragma once

#include <cstdint>

#include <string>
#include <string_view>
#include <vector>

namespace ticketx {

struct EventRecord {
  std::uint64_t sequence_id;
  std::string type;
  std::string payload_json;
};

using EventLog = std::vector<EventRecord>;

namespace event_type {

inline constexpr std::string_view EventCreated{"EventCreated"};
inline constexpr std::string_view WalletDeposited{"WalletDeposited"};
inline constexpr std::string_view TicketIssued{"TicketIssued"};
inline constexpr std::string_view PrimaryTicketBought{"PrimaryTicketBought"};
inline constexpr std::string_view OrderPlaced{"OrderPlaced"};
inline constexpr std::string_view OrderCancelled{"OrderCancelled"};
inline constexpr std::string_view OrderMatched{"OrderMatched"};
inline constexpr std::string_view WalletSettled{"WalletSettled"};
inline constexpr std::string_view TicketTransferred{"TicketTransferred"};

} // namespace event_type

} // namespace ticketx
