#include "ticketx/event_replay.hpp"
#include "ticketx/event_store.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

namespace {

std::filesystem::path TempEventLogPath(std::string name) {
  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("ticketx_" + std::move(name) + "_" + std::to_string(suffix) + ".jsonl");
}

} // namespace

TEST(EventStoreTest, AppendAndLoadRoundTripsEventRecords) {
  const std::filesystem::path path = TempEventLogPath("round_trip");
  std::filesystem::remove(path);

  const ticketx::EventRecord first{
      .sequence_id = 1,
      .type = std::string{ticketx::event_type::WalletDeposited},
      .payload_json = "{\"user_id\":100,\"amount\":1000000}",
  };
  const ticketx::EventRecord second{
      .sequence_id = 2,
      .type = std::string{ticketx::event_type::OrderPlaced},
      .payload_json =
          "{\"order_id\":10,\"user_id\":100,\"event_id\":7,\"category\":\"vip\","
          "\"side\":\"Buy\",\"type\":\"Limit\",\"limit_price\":700000,"
          "\"status\":\"Open\"}",
  };

  ASSERT_TRUE(ticketx::append_event_record(path, first));
  ASSERT_TRUE(ticketx::append_event_record(path, second));

  const std::optional<ticketx::EventLog> loaded = ticketx::load_event_log(path);

  ASSERT_TRUE(loaded.has_value());
  ASSERT_EQ(loaded->size(), 2U);
  EXPECT_EQ(loaded->at(0).sequence_id, first.sequence_id);
  EXPECT_EQ(loaded->at(0).type, first.type);
  EXPECT_EQ(loaded->at(0).payload_json, first.payload_json);
  EXPECT_EQ(loaded->at(1).sequence_id, second.sequence_id);
  EXPECT_EQ(loaded->at(1).type, second.type);
  EXPECT_EQ(loaded->at(1).payload_json, second.payload_json);

  std::filesystem::remove(path);
}

TEST(EventStoreTest, PayloadWithQuotesRoundTrips) {
  const std::filesystem::path path = TempEventLogPath("quotes");
  std::filesystem::remove(path);
  const ticketx::EventRecord record{
      .sequence_id = 1,
      .type = std::string{ticketx::event_type::EventCreated},
      .payload_json = "{\"name\":\"VIP \\\"Night\\\"\",\"category\":\"vip\\\\front\"}",
  };

  ASSERT_TRUE(ticketx::append_event_record(path, record));
  const std::optional<ticketx::EventLog> loaded = ticketx::load_event_log(path);

  ASSERT_TRUE(loaded.has_value());
  ASSERT_EQ(loaded->size(), 1U);
  EXPECT_EQ(loaded->at(0).payload_json, record.payload_json);

  std::filesystem::remove(path);
}

TEST(EventStoreTest, LoadEmptyFileReturnsEmptyLog) {
  const std::filesystem::path path = TempEventLogPath("empty");
  {
    std::ofstream output{path};
    ASSERT_TRUE(output.is_open());
  }

  const std::optional<ticketx::EventLog> loaded = ticketx::load_event_log(path);

  ASSERT_TRUE(loaded.has_value());
  EXPECT_TRUE(loaded->empty());

  std::filesystem::remove(path);
}

TEST(EventStoreTest, LoadMalformedJsonlReturnsNullopt) {
  const std::filesystem::path path = TempEventLogPath("malformed");
  {
    std::ofstream output{path};
    ASSERT_TRUE(output.is_open());
    output << "{\"sequence_id\":1,\"type\":\"WalletDeposited\",\"payload_json\":\"{}\"}\n";
    output << "{not json}\n";
  }

  EXPECT_FALSE(ticketx::load_event_log(path).has_value());

  std::filesystem::remove(path);
}

TEST(EventStoreTest, LoadedLogCanReplayState) {
  const std::filesystem::path path = TempEventLogPath("replay");
  std::filesystem::remove(path);
  ASSERT_TRUE(ticketx::append_event_record(
      path, ticketx::EventRecord{
                .sequence_id = 1,
                .type = std::string{ticketx::event_type::WalletDeposited},
                .payload_json = "{\"user_id\":100,\"amount\":1000000}",
            }));
  ASSERT_TRUE(ticketx::append_event_record(
      path, ticketx::EventRecord{
                .sequence_id = 2,
                .type = std::string{ticketx::event_type::OrderPlaced},
                .payload_json =
                    "{\"order_id\":10,\"user_id\":100,\"event_id\":7,"
                    "\"category\":\"vip\",\"side\":\"Buy\",\"type\":\"Limit\","
                    "\"limit_price\":700000,\"status\":\"Open\"}",
            }));

  const std::optional<ticketx::EventLog> loaded = ticketx::load_event_log(path);
  ASSERT_TRUE(loaded.has_value());

  const ticketx::ReplayState state = ticketx::replay_state(*loaded);

  ASSERT_TRUE(state.wallets.contains(100));
  EXPECT_EQ(state.wallets.at(100).available, 300'000);
  EXPECT_EQ(state.wallets.at(100).locked, 700'000);
  ASSERT_TRUE(state.open_orders.contains(10));

  std::filesystem::remove(path);
}

TEST(EventStoreTest, LoadReplayStateFromFileValidatesThenReplays) {
  const std::filesystem::path path = TempEventLogPath("validated_replay");
  std::filesystem::remove(path);
  ASSERT_TRUE(ticketx::append_event_record(
      path, ticketx::EventRecord{
                .sequence_id = 1,
                .type = std::string{ticketx::event_type::WalletDeposited},
                .payload_json = "{\"user_id\":100,\"amount\":1000000}",
            }));
  ASSERT_TRUE(ticketx::append_event_record(
      path, ticketx::EventRecord{
                .sequence_id = 2,
                .type = std::string{ticketx::event_type::OrderPlaced},
                .payload_json =
                    "{\"order_id\":10,\"user_id\":100,\"event_id\":7,"
                    "\"category\":\"vip\",\"side\":\"Buy\",\"type\":\"Limit\","
                    "\"limit_price\":700000,\"status\":\"Open\"}",
            }));

  const ticketx::ReplayLoadResult result =
      ticketx::load_replay_state_from_event_log_file(path);

  EXPECT_TRUE(result.report.ok);
  EXPECT_TRUE(result.report.errors.empty());
  EXPECT_EQ(result.report.event_count, 2U);
  ASSERT_TRUE(result.state.has_value());
  ASSERT_TRUE(result.state->wallets.contains(100));
  EXPECT_EQ(result.state->wallets.at(100).available, 300'000);
  EXPECT_EQ(result.state->wallets.at(100).locked, 700'000);
  ASSERT_TRUE(result.state->open_orders.contains(10));

  std::filesystem::remove(path);
}

TEST(EventStoreTest, LoadReplayStateFromFileRejectsMalformedJsonl) {
  const std::filesystem::path path = TempEventLogPath("validated_malformed");
  {
    std::ofstream output{path};
    ASSERT_TRUE(output.is_open());
    output << "{not json}\n";
  }

  const ticketx::ReplayLoadResult result =
      ticketx::load_replay_state_from_event_log_file(path);

  EXPECT_FALSE(result.report.ok);
  EXPECT_EQ(result.report.event_count, 0U);
  ASSERT_EQ(result.report.errors.size(), 1U);
  EXPECT_EQ(result.report.errors[0], "failed to load event log file");
  EXPECT_FALSE(result.state.has_value());

  std::filesystem::remove(path);
}

TEST(EventStoreTest, LoadReplayStateFromFileRejectsRecoveryInvalidLog) {
  const std::filesystem::path path = TempEventLogPath("validated_invalid_log");
  std::filesystem::remove(path);
  ASSERT_TRUE(ticketx::append_event_record(
      path, ticketx::EventRecord{
                .sequence_id = 1,
                .type = std::string{ticketx::event_type::WalletDeposited},
                .payload_json = "{\"user_id\":100}",
            }));

  const ticketx::ReplayLoadResult result =
      ticketx::load_replay_state_from_event_log_file(path);

  EXPECT_FALSE(result.report.ok);
  EXPECT_EQ(result.report.event_count, 1U);
  EXPECT_FALSE(result.report.errors.empty());
  EXPECT_FALSE(result.state.has_value());

  std::filesystem::remove(path);
}
