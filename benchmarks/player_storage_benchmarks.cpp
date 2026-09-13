#include <benchmark/benchmark.h>
#include <dod/dod.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr std::size_t player_count = 1'000'000;

struct Position {
    float x{};
    float y{};
    float z{};
};

class DodPlayer : public dod::Object<DodPlayer, player_count> {
public:
    DOD_PROPERTY(Position, position);
    DOD_PROPERTY(std::string, name);
    DOD_PROPERTY(std::uint32_t, age);
};

class OopPlayer {
public:
    Position position;
    std::string name;
    std::uint32_t age{};
};

std::vector<std::string> make_names()
{
    std::vector<std::string> names;
    names.reserve(player_count);

    for (std::size_t index = 0; index < player_count; ++index) {
        names.push_back("Player_" + std::to_string(index));
    }

    return names;
}

const std::vector<std::string>& player_names()
{
    // The input strings are prepared once and are never part of timed work.
    static const auto names = make_names();
    return names;
}

template <typename Player>
void initialize_player(
    Player& player,
    std::size_t index,
    const std::string& name
)
{
    const auto coordinate = static_cast<float>(index);
    player.position = Position{coordinate, coordinate * 2.0F, coordinate * 3.0F};
    player.name = name;
    player.age = static_cast<std::uint32_t>(18 + (index % 63));
}

template <typename Player>
std::unique_ptr<Player[]> make_heap_players()
{
    auto players = std::make_unique<Player[]>(player_count);
    const auto& names = player_names();

    for (std::size_t index = 0; index < player_count; ++index) {
        initialize_player(players[index], index, names[index]);
    }

    return players;
}

void initialize_dod_storage()
{
    // Allocate the persistent registry and property columns before timing
    // steady-state creation. The warmup entity is destroyed immediately.
    DodPlayer player;
    initialize_player(player, 0, "Warmup_Player");
}

void BM_DodPlayerCreation(benchmark::State& state)
{
    (void)player_names();
    initialize_dod_storage();

    for (auto _ : state) {
        auto players = make_heap_players<DodPlayer>();
        auto* data = players.get();
        benchmark::DoNotOptimize(data);
        benchmark::ClobberMemory();

        state.PauseTiming();
        players.reset();
        state.ResumeTiming();
    }

    state.SetItemsProcessed(
        static_cast<std::int64_t>(state.iterations()) * player_count
    );
}

void BM_OopPlayerCreation(benchmark::State& state)
{
    (void)player_names();

    for (auto _ : state) {
        auto players = make_heap_players<OopPlayer>();
        auto* data = players.get();
        benchmark::DoNotOptimize(data);
        benchmark::ClobberMemory();

        state.PauseTiming();
        players.reset();
        state.ResumeTiming();
    }

    state.SetItemsProcessed(
        static_cast<std::int64_t>(state.iterations()) * player_count
    );
}

void BM_DodPlayerDeletion(benchmark::State& state)
{
    initialize_dod_storage();

    for (auto _ : state) {
        state.PauseTiming();
        auto players = make_heap_players<DodPlayer>();
        auto* data = players.get();
        benchmark::DoNotOptimize(data);
        state.ResumeTiming();

        players.reset();
        benchmark::ClobberMemory();

        state.PauseTiming();
        if (DodPlayer::size() != 0) {
            state.ResumeTiming();
            state.SkipWithError("DOD registry was not empty after deletion");
            return;
        }
        state.ResumeTiming();
    }

    state.SetItemsProcessed(
        static_cast<std::int64_t>(state.iterations()) * player_count
    );
}

void BM_OopPlayerDeletion(benchmark::State& state)
{
    for (auto _ : state) {
        state.PauseTiming();
        auto players = make_heap_players<OopPlayer>();
        auto* data = players.get();
        benchmark::DoNotOptimize(data);
        state.ResumeTiming();

        players.reset();
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(
        static_cast<std::int64_t>(state.iterations()) * player_count
    );
}

void BM_DodPositionUpdate(benchmark::State& state)
{
    auto players = make_heap_players<DodPlayer>();

    for (auto _ : state) {
        auto positions = DodPlayer::view<&DodPlayer::position>();

        for (auto& position : positions) {
            position.x += 1.0F;
            position.y += 2.0F;
            position.z += 3.0F;
        }

        auto* data = positions.data();
        benchmark::DoNotOptimize(data);
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(
        static_cast<std::int64_t>(state.iterations()) * player_count
    );
}

void BM_OopPositionUpdate(benchmark::State& state)
{
    auto players = make_heap_players<OopPlayer>();

    for (auto _ : state) {
        for (std::size_t index = 0; index < player_count; ++index) {
            players[index].position.x += 1.0F;
            players[index].position.y += 2.0F;
            players[index].position.z += 3.0F;
        }

        auto* data = players.get();
        benchmark::DoNotOptimize(data);
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(
        static_cast<std::int64_t>(state.iterations()) * player_count
    );
}

void BM_DodAgeUpdate(benchmark::State& state)
{
    auto players = make_heap_players<DodPlayer>();

    for (auto _ : state) {
        auto ages = DodPlayer::view<&DodPlayer::age>();

        for (auto& age : ages) {
            ++age;
        }

        auto* data = ages.data();
        benchmark::DoNotOptimize(data);
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(
        static_cast<std::int64_t>(state.iterations()) * player_count
    );
}

void BM_OopAgeUpdate(benchmark::State& state)
{
    auto players = make_heap_players<OopPlayer>();

    for (auto _ : state) {
        for (std::size_t index = 0; index < player_count; ++index) {
            ++players[index].age;
        }

        auto* data = players.get();
        benchmark::DoNotOptimize(data);
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(
        static_cast<std::int64_t>(state.iterations()) * player_count
    );
}

void BM_DodNameUpdate(benchmark::State& state)
{
    auto players = make_heap_players<DodPlayer>();

    for (auto _ : state) {
        auto names = DodPlayer::view<&DodPlayer::name>();

        for (auto& name : names) {
            name.front() = name.front() == 'P' ? 'p' : 'P';
        }

        auto* data = names.data();
        benchmark::DoNotOptimize(data);
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(
        static_cast<std::int64_t>(state.iterations()) * player_count
    );
}

void BM_OopNameUpdate(benchmark::State& state)
{
    auto players = make_heap_players<OopPlayer>();

    for (auto _ : state) {
        for (std::size_t index = 0; index < player_count; ++index) {
            auto& name = players[index].name;
            name.front() = name.front() == 'P' ? 'p' : 'P';
        }

        auto* data = players.get();
        benchmark::DoNotOptimize(data);
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(
        static_cast<std::int64_t>(state.iterations()) * player_count
    );
}

constexpr auto benchmark_arg = static_cast<std::int64_t>(player_count);

// Creation pauses timing for deletion, and deletion pauses timing for setup.
// Three iterations per repetition keeps the million-object setup practical
// while still sampling each operation more than once.
BENCHMARK(BM_DodPlayerCreation)
    ->Arg(benchmark_arg)
    ->Iterations(3);
BENCHMARK(BM_OopPlayerCreation)
    ->Arg(benchmark_arg)
    ->Iterations(3);

BENCHMARK(BM_DodPlayerDeletion)
    ->Arg(benchmark_arg)
    ->Iterations(3);
BENCHMARK(BM_OopPlayerDeletion)
    ->Arg(benchmark_arg)
    ->Iterations(3);

BENCHMARK(BM_DodPositionUpdate)->Arg(benchmark_arg);
BENCHMARK(BM_OopPositionUpdate)->Arg(benchmark_arg);

BENCHMARK(BM_DodAgeUpdate)->Arg(benchmark_arg);
BENCHMARK(BM_OopAgeUpdate)->Arg(benchmark_arg);

BENCHMARK(BM_DodNameUpdate)->Arg(benchmark_arg);
BENCHMARK(BM_OopNameUpdate)->Arg(benchmark_arg);

} // namespace
