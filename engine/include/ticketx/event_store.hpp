#pragma once

#include "ticketx/event_log.hpp"

#include <filesystem>
#include <optional>

namespace ticketx {

bool append_event_record(const std::filesystem::path& path, const EventRecord& record);
std::optional<EventLog> load_event_log(const std::filesystem::path& path);

} // namespace ticketx
