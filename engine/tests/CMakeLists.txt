add_executable(ticketx_engine_tests
  smoke_test.cpp
  async_event_writer_test.cpp
  event_replay_test.cpp
  event_store_test.cpp
  order_book_test.cpp
  matching_engine_test.cpp
  primary_sale_test.cpp
  ticketx_engine_test.cpp
  ticket_ledger_test.cpp
  wallet_ledger_test.cpp
)

target_link_libraries(ticketx_engine_tests
  PRIVATE
    ticketx_engine
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(ticketx_engine_tests)
