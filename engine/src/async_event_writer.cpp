#include "ticketx/async_event_writer.hpp"
#include "ticketx/event_store.hpp"

#include <utility>

namespace ticketx {

AsyncEventWriter::AsyncEventWriter(std::filesystem::path path) : path_(std::move(path)) {}

AsyncEventWriter::~AsyncEventWriter() { stop(); }

void AsyncEventWriter::start() {
  std::lock_guard lock{mutex_};
  if (started_ || closed_) {
    return;
  }
  start_worker_locked();
}

bool AsyncEventWriter::enqueue(EventRecord record) {
  {
    std::lock_guard lock{mutex_};
    if (closed_ || stopping_) {
      return false;
    }
    if (!started_) {
      start_worker_locked();
    }
    queue_.push_back(std::move(record));
  }
  cv_.notify_one();
  return true;
}

void AsyncEventWriter::stop() {
  {
    std::lock_guard lock{mutex_};
    if (closed_) {
      return;
    }
    closed_ = true;
    if (!started_) {
      return;
    }
    stopping_ = true;
  }
  cv_.notify_one();
  if (worker_.joinable()) {
    worker_.join();
  }
  {
    std::lock_guard lock{mutex_};
    started_ = false;
  }

  // run() drains queue_ before worker_ exits.
}

void AsyncEventWriter::start_worker_locked() {
  stopping_ = false;
  worker_ = std::thread(&AsyncEventWriter::run, this);
  started_ = true;
}

void AsyncEventWriter::run() {
  while (true) {
    EventRecord record;
    {
      std::unique_lock lock{mutex_};
      cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
      if (stopping_ && queue_.empty()) {
        break;
      }
      record = std::move(queue_.front());
      queue_.pop_front();
    }
    (void)append_event_record(path_, record);
  }
}

} // namespace ticketx
