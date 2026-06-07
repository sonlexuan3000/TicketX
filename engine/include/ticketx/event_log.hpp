#pragma once

#include <string>
#include <vector>

namespace ticketx {

struct EventRecord {
  std::string type;
  std::string payload_json;
};

using EventLog = std::vector<EventRecord>;

} // namespace ticketx
