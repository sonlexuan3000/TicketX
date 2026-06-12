#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>

#include "ticketx/event_replay.hpp"

namespace ticketx {

struct Snapshot {
  std::uint64_t last_sequence_id{};
  ReplayState state;
};

bool save_snapshot(const std::filesystem::path& path, const Snapshot& snapshot);
std::optional<Snapshot> load_snapshot(const std::filesystem::path& path);

} // namespace ticketx
