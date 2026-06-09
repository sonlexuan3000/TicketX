#pragma once

#include "ticketx/event_log.hpp"

#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>

namespace ticketx {

class AsyncEventWriter {
public:
  explicit AsyncEventWriter(std::filesystem::path path);
  ~AsyncEventWriter();

  AsyncEventWriter(const AsyncEventWriter&) = delete;
  AsyncEventWriter& operator=(const AsyncEventWriter&) = delete;
  AsyncEventWriter(AsyncEventWriter&&) = delete;
  AsyncEventWriter& operator=(AsyncEventWriter&&) = delete;

  void start();
  bool enqueue(EventRecord record);
  void stop();

private:
  void start_worker_locked();
  void run();

  std::filesystem::path path_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<EventRecord> queue_;
  std::thread worker_;
  bool started_{false};
  bool stopping_{false};
  bool closed_{false};
};

} // namespace ticketx
