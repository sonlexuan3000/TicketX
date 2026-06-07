#include "ticketx/matching_engine.hpp"

#include <nlohmann/json.hpp>

#include <iostream>

int main() {
  const ticketx::MatchingEngine engine;
  const nlohmann::json status{
      {"service", "ticketx_server"},
      {"state", "placeholder"},
      {"engine", engine.name()},
      {"events", engine.event_log().size()},
  };

  std::cout << status.dump(2) << '\n';
  return 0;
}
