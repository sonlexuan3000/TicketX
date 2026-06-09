#include "ticketx/event_store.hpp"

#include <fstream>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace ticketx {

namespace {

std::optional<std::uint64_t> ExtractSequenceId(const nlohmann::json& object) {
  const auto it = object.find("sequence_id");
  if (it == object.end()) {
    return std::nullopt;
  }

  if (it->is_number_unsigned()) {
    return it->get<std::uint64_t>();
  }
  if (it->is_number_integer()) {
    const std::int64_t value = it->get<std::int64_t>();
    if (value >= 0) {
      return static_cast<std::uint64_t>(value);
    }
  }
  return std::nullopt;
}

std::optional<std::string> ExtractString(const nlohmann::json& object,
                                         const char* field_name) {
  const auto it = object.find(field_name);
  if (it == object.end() || !it->is_string()) {
    return std::nullopt;
  }
  return it->get<std::string>();
}

std::optional<EventRecord> ParseEventRecordLine(const std::string& line) {
  const nlohmann::json object = nlohmann::json::parse(line, nullptr, false);
  if (object.is_discarded() || !object.is_object()) {
    return std::nullopt;
  }

  const std::optional<std::uint64_t> sequence_id = ExtractSequenceId(object);
  const std::optional<std::string> type = ExtractString(object, "type");
  const std::optional<std::string> payload_json = ExtractString(object, "payload_json");
  if (!sequence_id.has_value() || !type.has_value() || !payload_json.has_value()) {
    return std::nullopt;
  }

  return EventRecord{
      .sequence_id = *sequence_id,
      .type = *type,
      .payload_json = *payload_json,
  };
}

} // namespace

bool append_event_record(const std::filesystem::path& path, const EventRecord& record) {
  std::ofstream output{path, std::ios::out | std::ios::app};
  if (!output.is_open()) {
    return false;
  }

  const nlohmann::json object{
      {"sequence_id", record.sequence_id},
      {"type", record.type},
      {"payload_json", record.payload_json},
  };
  output << object.dump() << '\n';
  output.flush();
  return output.good();
}

std::optional<EventLog> load_event_log(const std::filesystem::path& path) {
  std::ifstream input{path};
  if (!input.is_open()) {
    return std::nullopt;
  }

  EventLog event_log;
  std::string line;
  while (std::getline(input, line)) {
    const std::optional<EventRecord> record = ParseEventRecordLine(line);
    if (!record.has_value()) {
      return std::nullopt;
    }
    event_log.push_back(*record);
  }

  if (input.bad()) {
    return std::nullopt;
  }
  return event_log;
}

} // namespace ticketx
