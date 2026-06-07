add_executable(ticketx_server
  src/main.cpp
)

target_link_libraries(ticketx_server
  PRIVATE
    ticketx_engine
    nlohmann_json::nlohmann_json
)

target_compile_features(ticketx_server PRIVATE cxx_std_20)
