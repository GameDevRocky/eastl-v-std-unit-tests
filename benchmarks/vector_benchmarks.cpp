#include <EASTL/vector.h>
#include <benchmark/benchmark.h>
#include <cstdint>
#include <cstddef>
#include <vector>

static void BM_EASTLVectorPushBackReserved(benchmark::State& state)
{
    const auto count = static_cast<std::size_t>(state.range(0));

    for (auto _ : state)
    {
        eastl::vector<int> values;
        values.reserve(count);

        for (std::size_t i = 0; i < count; ++i)
        {
            values.push_back(static_cast<int>(i));
        }

        auto* data = values.data();
        benchmark::DoNotOptimize(data);
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(
        static_cast<std::int64_t>(state.iterations()) * state.range(0)
    );
}

static void BM_STDVectorPushBackReserved(benchmark::State& state)
{
    const auto count = static_cast<std::size_t>(state.range(0));

    for (auto _ : state)
    {
        std::vector<int> values;
        values.reserve(count);

        for (std::size_t i = 0; i < count; ++i)
        {
            values.push_back(static_cast<int>(i));
        }

        auto* data = values.data();
        benchmark::DoNotOptimize(data);
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(
        static_cast<std::int64_t>(state.iterations()) * state.range(0)
    );
}

BENCHMARK(BM_EASTLVectorPushBackReserved)
    ->Arg(1'000)
    ->Arg(100'000);

BENCHMARK(BM_STDVectorPushBackReserved)
    ->Arg(1'000)
    ->Arg(100'000);