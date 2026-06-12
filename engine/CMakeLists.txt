add_library(ticketx_engine
  src/async_event_writer.cpp
  src/event_store.cpp
  src/snapshot_store.cpp
  src/ticketx_engine.cpp
  src/event_replay.cpp
  src/matching_engine.cpp
  src/order_book.cpp
  src/ticket_ledger.cpp
  src/wallet_ledger.cpp
)

target_include_directories(ticketx_engine
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ticketx_engine
  PRIVATE
    nlohmann_json::nlohmann_json
)

target_compile_features(ticketx_engine PUBLIC cxx_std_20)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(ticketx_engine PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(TICKETX_BUILD_TESTS)
  add_subdirectory(tests)
endif()

if(TICKETX_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()
