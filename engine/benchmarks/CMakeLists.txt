add_executable(ticketx_engine_bench
  smoke_bench.cpp
)

target_link_libraries(ticketx_engine_bench
  PRIVATE
    ticketx_engine
    benchmark::benchmark
)
