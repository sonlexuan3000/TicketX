#include "ticketx/async_event_writer.hpp"
#include "ticketx/event_store.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace {

std::filesystem::path TempAsyncEventLogPath(std::string name) {
  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("ticketx_async_" + std::move(name) + "_" + std::to_string(suffix) + ".jsonl");
}

ticketx::EventRecord WalletDepositRecord(std::uint64_t sequence_id, std::uint64_t user_id,
                                          std::int64_t amount) {
  return ticketx::EventRecord{
      .sequence_id = sequence_id,
      .type = std::string{ticketx::event_type::WalletDeposited},
      .payload_json = "{\"user_id\":" + std::to_string(user_id) + ",\"amount\":" +
                      std::to_string(amount) + "}",
  };
}

} // namespace

TEST(AsyncEventWriterTest, EnqueueAutoStartsWorkerAndFlushesOnStop) {
  const std::filesystem::path path = TempAsyncEventLogPath("auto_start");
  std::filesystem::remove(path);

  ticketx::AsyncEventWriter writer{path};

  EXPECT_TRUE(writer.enqueue(WalletDepositRecord(1, 100, 5000)));
  EXPECT_TRUE(writer.enqueue(WalletDepositRecord(2, 101, 7000)));
  writer.stop();

  const std::optional<ticketx::EventLog> loaded = ticketx::load_event_log(path);
  ASSERT_TRUE(loaded.has_value());
  ASSERT_EQ(loaded->size(), 2U);
  EXPECT_EQ(loaded->at(0).sequence_id, 1U);
  EXPECT_EQ(loaded->at(1).sequence_id, 2U);

  std::filesystem::remove(path);
}

TEST(AsyncEventWriterTest, StopDrainsQueuedEventsBeforeReturning) {
  const std::filesystem::path path = TempAsyncEventLogPath("drain");
  std::filesystem::remove(path);

  ticketx::AsyncEventWriter writer{path};
  for (std::uint64_t sequence_id = 1; sequence_id <= 50; ++sequence_id) {
    EXPECT_TRUE(writer.enqueue(WalletDepositRecord(
        sequence_id, sequence_id, static_cast<std::int64_t>(sequence_id))));
  }

  writer.stop();

  const std::optional<ticketx::EventLog> loaded = ticketx::load_event_log(path);
  ASSERT_TRUE(loaded.has_value());
  ASSERT_EQ(loaded->size(), 50U);
  EXPECT_EQ(loaded->front().sequence_id, 1U);
  EXPECT_EQ(loaded->back().sequence_id, 50U);

  std::filesystem::remove(path);
}

TEST(AsyncEventWriterTest, EnqueueAfterStopIsRejectedAndDoesNotRestartWorker) {
  const std::filesystem::path path = TempAsyncEventLogPath("after_stop");
  std::filesystem::remove(path);

  ticketx::AsyncEventWriter writer{path};

  EXPECT_TRUE(writer.enqueue(WalletDepositRecord(1, 100, 5000)));
  writer.stop();

  EXPECT_FALSE(writer.enqueue(WalletDepositRecord(2, 101, 7000)));

  const std::optional<ticketx::EventLog> loaded = ticketx::load_event_log(path);
  ASSERT_TRUE(loaded.has_value());
  ASSERT_EQ(loaded->size(), 1U);
  EXPECT_EQ(loaded->at(0).sequence_id, 1U);

  std::filesystem::remove(path);
}

TEST(AsyncEventWriterTest, ExplicitStartStillAllowsEnqueue) {
  const std::filesystem::path path = TempAsyncEventLogPath("explicit_start");
  std::filesystem::remove(path);

  ticketx::AsyncEventWriter writer{path};

  writer.start();
  EXPECT_TRUE(writer.enqueue(WalletDepositRecord(1, 200, 9000)));
  writer.stop();

  const std::optional<ticketx::EventLog> loaded = ticketx::load_event_log(path);
  ASSERT_TRUE(loaded.has_value());
  ASSERT_EQ(loaded->size(), 1U);
  EXPECT_EQ(loaded->at(0).payload_json, "{\"user_id\":200,\"amount\":9000}");

  std::filesystem::remove(path);
}

TEST(AsyncEventWriterTest, StopBeforeFirstEnqueueClosesWriter) {
  const std::filesystem::path path = TempAsyncEventLogPath("stop_before_enqueue");
  std::filesystem::remove(path);

  ticketx::AsyncEventWriter writer{path};

  writer.stop();

  EXPECT_FALSE(writer.enqueue(WalletDepositRecord(1, 300, 1000)));
  EXPECT_FALSE(ticketx::load_event_log(path).has_value());

  std::filesystem::remove(path);
}

TEST(AsyncEventWriterTest, DestructorDrainsQueuedEvents) {
  const std::filesystem::path path = TempAsyncEventLogPath("destructor_drain");
  std::filesystem::remove(path);

  {
    ticketx::AsyncEventWriter writer{path};
    EXPECT_TRUE(writer.enqueue(WalletDepositRecord(1, 400, 1000)));
    EXPECT_TRUE(writer.enqueue(WalletDepositRecord(2, 401, 2000)));
  }

  const std::optional<ticketx::EventLog> loaded = ticketx::load_event_log(path);
  ASSERT_TRUE(loaded.has_value());
  ASSERT_EQ(loaded->size(), 2U);
  EXPECT_EQ(loaded->at(0).sequence_id, 1U);
  EXPECT_EQ(loaded->at(1).sequence_id, 2U);

  std::filesystem::remove(path);
}
