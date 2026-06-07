cmake_minimum_required(VERSION 3.28)

project(
  TicketX
  VERSION 0.1.0
  DESCRIPTION "Wallet-based ticket exchange with a C++ matching engine"
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TICKETX_BUILD_TESTS "Build TicketX unit tests" ON)
option(TICKETX_BUILD_BENCHMARKS "Build TicketX benchmarks" ON)
option(TICKETX_BUILD_BACKEND "Build TicketX backend placeholder" ON)

include(cmake/Dependencies.cmake)

if(TICKETX_BUILD_TESTS)
  include(CTest)
  enable_testing()
endif()

add_subdirectory(engine)

if(TICKETX_BUILD_BACKEND)
  add_subdirectory(backend)
endif()
