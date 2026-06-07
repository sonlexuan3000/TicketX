#pragma once

#include "ticketx/order.hpp"

#include <cstddef>

namespace ticketx {

class OrderBook {
public:
  [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }
  [[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }

private:
  std::size_t size_{0};
};

} // namespace ticketx
