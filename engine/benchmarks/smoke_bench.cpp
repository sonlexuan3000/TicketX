#include "ticketx/matching_engine.hpp"

#include <benchmark/benchmark.h>

static void EngineConstruction(benchmark::State& state) {
  for (auto _ : state) {
    ticketx::MatchingEngine engine;
    benchmark::DoNotOptimize(engine.event_log().size());
  }
}

BENCHMARK(EngineConstruction);

BENCHMARK_MAIN();
