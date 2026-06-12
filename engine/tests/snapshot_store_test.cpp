#include "ticketx/event_replay.hpp"
#include "ticketx/snapshot_store.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

namespace {

std::filesystem::path TempSnapshotPath(std::string name) {
  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("ticketx_snapshot_" + std::move(name) + "_" + std::to_string(suffix) +
          ".json");
}

ticketx::EventRecord MakeEvent(std::uint64_t sequence_id, std::string type,
                               std::string payload_json) {
  return ticketx::EventRecord{
      .sequence_id = sequence_id,
      .type = std::move(type),
      .payload_json = std::move(payload_json),
  };
}

ticketx::EventLog SnapshotFixtureLog() {
  return ticketx::EventLog{
      MakeEvent(1, std::string{ticketx::event_type::EventCreated},
                "{\"event_id\":10,\"name\":\"TicketX Live\",\"categories\":["
                "{\"name\":\"standard\",\"price\":1000000,\"remaining\":5}]}"),
      MakeEvent(2, std::string{ticketx::event_type::WalletDeposited},
                "{\"user_id\":100,\"amount\":1000000}"),
      MakeEvent(3, std::string{ticketx::event_type::PrimaryTicketBought},
                "{\"ticket_id\":1,\"buyer_user_id\":100,\"event_id\":10,"
                "\"category\":\"standard\",\"price\":1000000,"
                "\"credential_version\":1}"),
      MakeEvent(4, std::string{ticketx::event_type::WalletDeposited},
                "{\"user_id\":200,\"amount\":1000000}"),
      MakeEvent(5, std::string{ticketx::event_type::OrderPlaced},
                "{\"order_id\":10,\"user_id\":200,\"event_id\":20,\"category\":\"vip\","
                "\"side\":\"Buy\",\"type\":\"Limit\",\"limit_price\":700000,"
                "\"status\":\"Open\"}"),
      MakeEvent(6, std::string{ticketx::event_type::TicketIssued},
                "{\"ticket_id\":30,\"owner_user_id\":300,\"event_id\":30,"
                "\"category\":\"balcony\",\"status\":\"Owned\","
                "\"credential_version\":1}"),
      MakeEvent(7, std::string{ticketx::event_type::OrderPlaced},
                "{\"order_id\":20,\"user_id\":300,\"event_id\":30,"
                "\"category\":\"balcony\",\"side\":\"Sell\",\"type\":\"Limit\","
                "\"limit_price\":500000,\"status\":\"Open\"}"),
  };
}

ticketx::Snapshot SnapshotFixture() {
  const ticketx::ReplayState state = ticketx::replay_state(SnapshotFixtureLog());
  return ticketx::Snapshot{
      .last_sequence_id = state.summary.last_sequence_id,
      .state = state,
  };
}

nlohmann::json JsonForSnapshot(const ticketx::Snapshot& snapshot) {
  const std::filesystem::path path = TempSnapshotPath("json_source");
  std::filesystem::remove(path);
  EXPECT_TRUE(ticketx::save_snapshot(path, snapshot));

  std::ifstream input{path};
  EXPECT_TRUE(input.is_open());
  nlohmann::json object;
  input >> object;
  std::filesystem::remove(path);
  return object;
}

void WriteJson(const std::filesystem::path& path, const nlohmann::json& object) {
  std::ofstream output{path};
  ASSERT_TRUE(output.is_open());
  output << object.dump(2) << '\n';
}

void ExpectSnapshotFixtureState(const ticketx::Snapshot& snapshot) {
  EXPECT_EQ(snapshot.last_sequence_id, 7U);
  EXPECT_EQ(snapshot.state.summary.event_count, 7U);
  EXPECT_EQ(snapshot.state.summary.last_sequence_id, 7U);
  EXPECT_TRUE(snapshot.state.summary.sequence_contiguous);

  ASSERT_TRUE(snapshot.state.events.contains(10));
  ASSERT_EQ(snapshot.state.events.at(10).categories.size(), 1U);
  EXPECT_EQ(snapshot.state.events.at(10).categories[0].remaining, 4U);

  ASSERT_TRUE(snapshot.state.wallets.contains(100));
  EXPECT_EQ(snapshot.state.wallets.at(100).available, 0);
  EXPECT_EQ(snapshot.state.wallets.at(100).locked, 0);
  ASSERT_TRUE(snapshot.state.wallets.contains(200));
  EXPECT_EQ(snapshot.state.wallets.at(200).available, 300'000);
  EXPECT_EQ(snapshot.state.wallets.at(200).locked, 700'000);

  ASSERT_EQ(snapshot.state.open_order_sequence.size(), 2U);
  EXPECT_EQ(snapshot.state.open_order_sequence[0], 10U);
  EXPECT_EQ(snapshot.state.open_order_sequence[1], 20U);
  ASSERT_TRUE(snapshot.state.open_orders.contains(10));
  EXPECT_EQ(snapshot.state.open_orders.at(10).side, ticketx::Side::Buy);
  ASSERT_TRUE(snapshot.state.open_orders.contains(20));
  EXPECT_EQ(snapshot.state.open_orders.at(20).side, ticketx::Side::Sell);

  ASSERT_TRUE(snapshot.state.tickets.contains(1));
  EXPECT_EQ(snapshot.state.tickets.at(1).status, ticketx::TicketStatus::Owned);
  ASSERT_TRUE(snapshot.state.tickets.contains(30));
  EXPECT_EQ(snapshot.state.tickets.at(30).status, ticketx::TicketStatus::LockedForSell);
  EXPECT_EQ(snapshot.state.max_ticket_id, 30U);
}

} // namespace

TEST(SnapshotStoreTest, SaveAndLoadRoundTripsReplayState) {
  const std::filesystem::path path = TempSnapshotPath("round_trip");
  std::filesystem::remove(path);

  const ticketx::Snapshot snapshot = SnapshotFixture();

  ASSERT_TRUE(ticketx::save_snapshot(path, snapshot));
  const std::optional<ticketx::Snapshot> loaded = ticketx::load_snapshot(path);

  ASSERT_TRUE(loaded.has_value());
  ExpectSnapshotFixtureState(*loaded);

  std::filesystem::remove(path);
}

TEST(SnapshotStoreTest, SaveAtomicallyReplacesExistingSnapshot) {
  const std::filesystem::path path = TempSnapshotPath("atomic_replace");
  std::filesystem::path temporary_path = path;
  temporary_path += ".tmp";
  std::filesystem::remove(path);
  std::filesystem::remove(temporary_path);

  ASSERT_TRUE(ticketx::save_snapshot(path, SnapshotFixture()));

  ticketx::EventLog replacement_log = SnapshotFixtureLog();
  replacement_log.push_back(MakeEvent(
      8, std::string{ticketx::event_type::WalletDeposited},
      "{\"user_id\":500,\"amount\":123000}"));
  const ticketx::ReplayState replacement_state = ticketx::replay_state(replacement_log);
  const ticketx::Snapshot replacement_snapshot{
      .last_sequence_id = replacement_state.summary.last_sequence_id,
      .state = replacement_state,
  };

  ASSERT_TRUE(ticketx::save_snapshot(path, replacement_snapshot));

  const std::optional<ticketx::Snapshot> loaded = ticketx::load_snapshot(path);
  ASSERT_TRUE(loaded.has_value());
  EXPECT_EQ(loaded->last_sequence_id, 8U);
  ASSERT_TRUE(loaded->state.wallets.contains(500));
  EXPECT_EQ(loaded->state.wallets.at(500).available, 123000);
  EXPECT_FALSE(std::filesystem::exists(temporary_path));

  std::filesystem::remove(path);
  std::filesystem::remove(temporary_path);
}

TEST(SnapshotStoreTest, SaveRejectsSequenceMismatch) {
  const std::filesystem::path path = TempSnapshotPath("sequence_mismatch");
  std::filesystem::remove(path);

  ticketx::Snapshot snapshot = SnapshotFixture();
  snapshot.last_sequence_id += 1;

  EXPECT_FALSE(ticketx::save_snapshot(path, snapshot));
  EXPECT_FALSE(std::filesystem::exists(path));
}

TEST(SnapshotStoreTest, SaveRejectsInvalidStateBeforeTruncatingExistingFile) {
  const std::filesystem::path path = TempSnapshotPath("invalid_state_no_truncate");
  {
    std::ofstream output{path};
    ASSERT_TRUE(output.is_open());
    output << "keep me\n";
  }

  ticketx::Snapshot snapshot = SnapshotFixture();
  snapshot.state.wallets.at(200).locked = 0;

  EXPECT_FALSE(ticketx::save_snapshot(path, snapshot));

  std::ifstream input{path};
  ASSERT_TRUE(input.is_open());
  std::string contents;
  std::getline(input, contents);
  EXPECT_EQ(contents, "keep me");

  std::filesystem::remove(path);
}

TEST(SnapshotStoreTest, LoadRejectsMalformedJson) {
  const std::filesystem::path path = TempSnapshotPath("malformed");
  {
    std::ofstream output{path};
    ASSERT_TRUE(output.is_open());
    output << "{not json}\n";
  }

  EXPECT_FALSE(ticketx::load_snapshot(path).has_value());

  std::filesystem::remove(path);
}

TEST(SnapshotStoreTest, LoadRejectsInvalidReplaySummary) {
  const std::filesystem::path path = TempSnapshotPath("invalid_summary");
  nlohmann::json object = JsonForSnapshot(SnapshotFixture());
  object["state"]["summary"]["event_count"] = 999;
  WriteJson(path, object);

  EXPECT_FALSE(ticketx::load_snapshot(path).has_value());

  std::filesystem::remove(path);
}

TEST(SnapshotStoreTest, LoadRejectsMaxSequenceIdSummary) {
  const std::filesystem::path path = TempSnapshotPath("max_sequence");
  nlohmann::json object = JsonForSnapshot(SnapshotFixture());
  object["last_sequence_id"] = std::numeric_limits<std::uint64_t>::max();
  object["state"]["summary"]["last_sequence_id"] =
      std::numeric_limits<std::uint64_t>::max();
  object["state"]["summary"]["event_count"] =
      std::numeric_limits<std::uint64_t>::max();
  WriteJson(path, object);

  EXPECT_FALSE(ticketx::load_snapshot(path).has_value());

  std::filesystem::remove(path);
}

TEST(SnapshotStoreTest, LoadRejectsInconsistentOpenOrderSequence) {
  const std::filesystem::path path = TempSnapshotPath("bad_order_sequence");
  {
    std::ofstream output{path};
    ASSERT_TRUE(output.is_open());
    output << R"({
      "last_sequence_id": 1,
      "state": {
        "summary": {
          "event_count": 1,
          "order_placed_count": 1,
          "order_cancelled_count": 0,
          "trade_count": 0,
          "wallet_settled_amount": 0,
          "ticket_transfer_count": 0,
          "last_sequence_id": 1,
          "sequence_contiguous": true,
          "incomplete_trade_group_count": 0,
          "trade_groups_complete": true
        },
        "events": [],
        "wallets": [
          {"user_id": 100, "available": 1000000, "locked": 0}
        ],
        "open_orders": [
          {
            "order_id": 10,
            "user_id": 100,
            "event_id": 7,
            "category": "vip",
            "side": "Buy",
            "type": "Limit",
            "limit_price": 700000,
            "status": "Open"
          }
        ],
        "open_order_sequence": [],
        "tickets": [],
        "max_ticket_id": 0
      }
    })";
  }

  EXPECT_FALSE(ticketx::load_snapshot(path).has_value());

  std::filesystem::remove(path);
}

TEST(SnapshotStoreTest, LoadRejectsTicketMaxIdMismatch) {
  const std::filesystem::path path = TempSnapshotPath("max_ticket_mismatch");
  nlohmann::json object = JsonForSnapshot(SnapshotFixture());
  object["state"]["max_ticket_id"] = 31;
  WriteJson(path, object);

  EXPECT_FALSE(ticketx::load_snapshot(path).has_value());

  std::filesystem::remove(path);
}

TEST(SnapshotStoreTest, LoadRejectsWalletBalanceOverflow) {
  const std::filesystem::path path = TempSnapshotPath("wallet_overflow");
  nlohmann::json object = JsonForSnapshot(SnapshotFixture());
  object["state"]["wallets"][0]["available"] =
      std::numeric_limits<ticketx::Money>::max();
  object["state"]["wallets"][0]["locked"] = 1;
  WriteJson(path, object);

  EXPECT_FALSE(ticketx::load_snapshot(path).has_value());

  std::filesystem::remove(path);
}

TEST(SnapshotStoreTest, LoadRejectsOpenBuyWithoutMatchingLockedFunds) {
  const std::filesystem::path path = TempSnapshotPath("buy_lock_mismatch");
  nlohmann::json object = JsonForSnapshot(SnapshotFixture());
  object["state"]["wallets"][1]["locked"] = 0;
  WriteJson(path, object);

  EXPECT_FALSE(ticketx::load_snapshot(path).has_value());

  std::filesystem::remove(path);
}

TEST(SnapshotStoreTest, LoadRejectsOpenBuyForCurrentTicketOwner) {
  const std::filesystem::path path = TempSnapshotPath("open_buy_for_ticket_owner");
  nlohmann::json object = JsonForSnapshot(SnapshotFixture());
  object["state"]["open_orders"][0]["user_id"] = 100;
  object["state"]["open_orders"][0]["event_id"] = 10;
  object["state"]["open_orders"][0]["category"] = "standard";
  object["state"]["wallets"][0]["available"] = 0;
  object["state"]["wallets"][0]["locked"] = 700000;
  object["state"]["wallets"][1]["available"] = 1000000;
  object["state"]["wallets"][1]["locked"] = 0;
  WriteJson(path, object);

  EXPECT_FALSE(ticketx::load_snapshot(path).has_value());

  std::filesystem::remove(path);
}

TEST(SnapshotStoreTest, LoadRejectsLockedTicketWithoutOpenSellOrder) {
  const std::filesystem::path path = TempSnapshotPath("locked_ticket_without_sell");
  nlohmann::json object = JsonForSnapshot(SnapshotFixture());
  object["state"]["open_orders"].erase(1);
  object["state"]["open_order_sequence"].erase(1);
  WriteJson(path, object);

  EXPECT_FALSE(ticketx::load_snapshot(path).has_value());

  std::filesystem::remove(path);
}

TEST(SnapshotStoreTest, LoadRejectsOpenSellWithoutLockedTicket) {
  const std::filesystem::path path = TempSnapshotPath("open_sell_without_locked_ticket");
  nlohmann::json object = JsonForSnapshot(SnapshotFixture());
  object["state"]["tickets"][1]["status"] = "Owned";
  WriteJson(path, object);

  EXPECT_FALSE(ticketx::load_snapshot(path).has_value());

  std::filesystem::remove(path);
}

TEST(SnapshotStoreTest, LoadRejectsDuplicateActiveTicketForSameUserAndEvent) {
  const std::filesystem::path path = TempSnapshotPath("duplicate_active_ticket");
  nlohmann::json object = JsonForSnapshot(SnapshotFixture());
  nlohmann::json duplicate_ticket = object["state"]["tickets"][0];
  duplicate_ticket["ticket_id"] = 2;
  object["state"]["tickets"].push_back(std::move(duplicate_ticket));
  WriteJson(path, object);

  EXPECT_FALSE(ticketx::load_snapshot(path).has_value());

  std::filesystem::remove(path);
}

TEST(SnapshotStoreTest, LoadRejectsCrossedOpenOrders) {
  const std::filesystem::path path = TempSnapshotPath("crossed_orders");
  nlohmann::json object = JsonForSnapshot(SnapshotFixture());
  object["state"]["tickets"].push_back(nlohmann::json{
      {"ticket_id", 31},
      {"event_id", 20},
      {"category", "vip"},
      {"owner_user_id", 400},
      {"status", "LockedForSell"},
      {"credential_version", 1},
  });
  object["state"]["max_ticket_id"] = 31;
  object["state"]["open_orders"].push_back(nlohmann::json{
      {"order_id", 30},
      {"user_id", 400},
      {"event_id", 20},
      {"category", "vip"},
      {"side", "Sell"},
      {"type", "Limit"},
      {"limit_price", 600000},
      {"status", "Open"},
  });
  object["state"]["open_order_sequence"].push_back(30);
  WriteJson(path, object);

  EXPECT_FALSE(ticketx::load_snapshot(path).has_value());

  std::filesystem::remove(path);
}

TEST(SnapshotStoreTest, ReplayFromSnapshotAndTailMatchesFullReplay) {
  const ticketx::EventLog snapshot_log{
      MakeEvent(1, std::string{ticketx::event_type::WalletDeposited},
                "{\"user_id\":100,\"amount\":2000000}"),
      MakeEvent(2, std::string{ticketx::event_type::OrderPlaced},
                "{\"order_id\":10,\"user_id\":100,\"event_id\":7,\"category\":\"vip\","
                "\"side\":\"Buy\",\"type\":\"Limit\",\"limit_price\":700000,"
                "\"status\":\"Open\"}"),
  };
  const ticketx::EventLog tail_log{
      MakeEvent(3, std::string{ticketx::event_type::TicketIssued},
                "{\"ticket_id\":30,\"owner_user_id\":200,\"event_id\":8,"
                "\"category\":\"standard\",\"status\":\"Owned\","
                "\"credential_version\":1}"),
      MakeEvent(4, std::string{ticketx::event_type::OrderPlaced},
                "{\"order_id\":20,\"user_id\":200,\"event_id\":8,"
                "\"category\":\"standard\",\"side\":\"Sell\",\"type\":\"Limit\","
                "\"limit_price\":500000,\"status\":\"Open\"}"),
      MakeEvent(5, std::string{ticketx::event_type::OrderMatched},
                "{\"buy_order_id\":30,\"sell_order_id\":20,\"buyer_user_id\":100,"
                "\"seller_user_id\":200,\"event_id\":8,\"category\":\"standard\","
                "\"price\":500000}"),
      MakeEvent(6, std::string{ticketx::event_type::WalletSettled},
                "{\"buyer_user_id\":100,\"seller_user_id\":200,\"event_id\":8,"
                "\"category\":\"standard\",\"price\":500000}"),
      MakeEvent(7, std::string{ticketx::event_type::TicketTransferred},
                "{\"buyer_user_id\":100,\"seller_user_id\":200,\"event_id\":8,"
                "\"category\":\"standard\"}"),
  };

  ticketx::EventLog full_log = snapshot_log;
  full_log.insert(full_log.end(), tail_log.begin(), tail_log.end());

  const ticketx::ReplayState snapshot_state = ticketx::replay_state(snapshot_log);
  const ticketx::ReplayState from_snapshot =
      ticketx::replay_state_from(snapshot_state, tail_log);
  const ticketx::ReplayState full_replay = ticketx::replay_state(full_log);

  EXPECT_EQ(from_snapshot.summary.event_count, full_replay.summary.event_count);
  EXPECT_EQ(from_snapshot.summary.last_sequence_id, full_replay.summary.last_sequence_id);
  EXPECT_TRUE(from_snapshot.summary.sequence_contiguous);
  ASSERT_TRUE(from_snapshot.wallets.contains(100));
  EXPECT_EQ(from_snapshot.wallets.at(100).available,
            full_replay.wallets.at(100).available);
  EXPECT_EQ(from_snapshot.wallets.at(100).locked, full_replay.wallets.at(100).locked);
  ASSERT_TRUE(from_snapshot.wallets.contains(200));
  EXPECT_EQ(from_snapshot.wallets.at(200).available,
            full_replay.wallets.at(200).available);
  ASSERT_TRUE(from_snapshot.open_orders.contains(10));
  EXPECT_FALSE(from_snapshot.open_orders.contains(20));
  ASSERT_TRUE(from_snapshot.tickets.contains(30));
  EXPECT_EQ(from_snapshot.tickets.at(30).owner_user_id.value, 100U);
  EXPECT_EQ(from_snapshot.tickets.at(30).credential_version, 2U);
}

TEST(SnapshotStoreTest, ReplayFromSnapshotMarksTailSequenceGap) {
  const ticketx::EventLog snapshot_log{
      MakeEvent(1, std::string{ticketx::event_type::WalletDeposited},
                "{\"user_id\":100,\"amount\":1000000}"),
      MakeEvent(2, std::string{ticketx::event_type::WalletDeposited},
                "{\"user_id\":200,\"amount\":1000000}"),
  };
  const ticketx::EventLog tail_with_gap{
      MakeEvent(4, std::string{ticketx::event_type::WalletDeposited},
                "{\"user_id\":300,\"amount\":1000000}"),
  };

  const ticketx::ReplayState recovered =
      ticketx::replay_state_from(ticketx::replay_state(snapshot_log), tail_with_gap);

  EXPECT_FALSE(recovered.summary.sequence_contiguous);
  EXPECT_EQ(recovered.summary.last_sequence_id, 4U);
}

TEST(SnapshotStoreTest, ReplayFromSnapshotMarksOverflowedSummaryInvalid) {
  ticketx::ReplayState initial_state;
  initial_state.summary.event_count = std::numeric_limits<std::size_t>::max();
  initial_state.summary.order_placed_count = std::numeric_limits<std::size_t>::max();
  initial_state.summary.last_sequence_id = 7;

  const ticketx::EventLog tail_log{
      MakeEvent(8, std::string{ticketx::event_type::WalletDeposited},
                "{\"user_id\":300,\"amount\":1000000}"),
  };

  const ticketx::ReplayState recovered = ticketx::replay_state_from(initial_state, tail_log);

  EXPECT_FALSE(recovered.summary.sequence_contiguous);
  EXPECT_FALSE(recovered.summary.trade_groups_complete);
}
