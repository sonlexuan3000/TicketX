add_executable(ticketx_engine_tests
  smoke_test.cpp
)

target_link_libraries(ticketx_engine_tests
  PRIVATE
    ticketx_engine
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(ticketx_engine_tests)
