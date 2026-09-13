#include <benchmark/benchmark.h>
#include <dod/dod.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numeric>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr std::size_t max_entities = 1'000'000;
constexpr std::size_t engine_entities = 1'000'000;
constexpr std::size_t lifecycle_entities = 250'000;

struct Vec3 {
    float x{};
    float y{};
    float z{};
};

struct NumericOop {
    Vec3 position{};
    Vec3 velocity{};
    Vec3 acceleration{};
    std::uint32_t health{};
    std::uint32_t team{};
    float lifetime{};
    std::uint8_t active{};
};

class NumericDod : public dod::Object<NumericDod, max_entities> {
public:
    DOD_PROPERTY(Vec3, position);
    DOD_PROPERTY(Vec3, velocity);
    DOD_PROPERTY(Vec3, acceleration);
    DOD_PROPERTY(std::uint32_t, health);
    DOD_PROPERTY(std::uint32_t, team);
    DOD_PROPERTY(float, lifetime);
    DOD_PROPERTY(std::uint8_t, active);
};

struct NumericSoa {
    explicit NumericSoa(std::size_t count)
        : position(count), velocity(count), acceleration(count), health(count),
          team(count), lifetime(count), active(count)
    {
    }

    std::vector<Vec3> position;
    std::vector<Vec3> velocity;
    std::vector<Vec3> acceleration;
    std::vector<std::uint32_t> health;
    std::vector<std::uint32_t> team;
    std::vector<float> lifetime;
    std::vector<std::uint8_t> active;
};

template <typename Entity>
void initialize_entity(Entity& entity, std::size_t index)
{
    const float value = static_cast<float>(index % 10'007);
    entity.position = Vec3{value, value * 0.5F, value * -0.25F};
    entity.velocity = Vec3{1.0F + value * 0.001F, -0.5F, 0.25F};
    entity.acceleration = Vec3{0.01F, -0.02F, 0.005F};
    entity.health = static_cast<std::uint32_t>(50 + (index % 151));
    entity.team = static_cast<std::uint32_t>(index % 8);
    entity.lifetime = 0.25F + static_cast<float>(index % 1000) * 0.01F;
    entity.active = static_cast<std::uint8_t>((index % 10) != 0);
}

std::unique_ptr<NumericDod[]> make_dod(std::size_t count)
{
    auto entities = std::make_unique<NumericDod[]>(count);
    for (std::size_t i = 0; i < count; ++i) {
        initialize_entity(entities[i], i);
    }
    return entities;
}

std::vector<NumericOop> make_aos(std::size_t count)
{
    std::vector<NumericOop> entities(count);
    for (std::size_t i = 0; i < count; ++i) {
        initialize_entity(entities[i], i);
    }
    return entities;
}

NumericSoa make_soa(std::size_t count)
{
    NumericSoa entities(count);
    for (std::size_t i = 0; i < count; ++i) {
        NumericOop value;
        initialize_entity(value, i);
        entities.position[i] = value.position;
        entities.velocity[i] = value.velocity;
        entities.acceleration[i] = value.acceleration;
        entities.health[i] = value.health;
        entities.team[i] = value.team;
        entities.lifetime[i] = value.lifetime;
        entities.active[i] = value.active;
    }
    return entities;
}

std::vector<std::unique_ptr<NumericOop>> make_pointer_aos(std::size_t count)
{
    std::vector<std::unique_ptr<NumericOop>> entities;
    entities.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        auto entity = std::make_unique<NumericOop>();
        initialize_entity(*entity, i);
        entities.push_back(std::move(entity));
    }
    return entities;
}

std::vector<std::size_t> shuffled_indices(std::size_t count)
{
    std::vector<std::size_t> indices(count);
    std::iota(indices.begin(), indices.end(), std::size_t{0});
    std::mt19937 random(0xC0FFEEu);
    std::shuffle(indices.begin(), indices.end(), random);
    return indices;
}

void set_item_and_byte_counters(
    benchmark::State& state,
    std::size_t items,
    std::size_t bytes_per_item
)
{
    const auto iterations = static_cast<std::int64_t>(state.iterations());
    state.SetItemsProcessed(iterations * static_cast<std::int64_t>(items));
    state.SetBytesProcessed(
        iterations * static_cast<std::int64_t>(items * bytes_per_item)
    );
}

// Cache sweep: one dense scalar column reaches beyond this machine's 16 MiB L3.
constexpr std::size_t scalar_capacity = 4'194'304;

class ScalarDod : public dod::Object<ScalarDod, scalar_capacity> {
public:
    DOD_PROPERTY(std::uint64_t, value);
};

void BM_Cache_DodView(benchmark::State& state)
{
    const auto count = static_cast<std::size_t>(state.range(0));
    auto entities = std::make_unique<ScalarDod[]>(count);
    auto values = ScalarDod::view<&ScalarDod::value>();
    for (std::size_t i = 0; i < count; ++i) {
        values[i] = i;
    }

    for (auto _ : state) {
        for (auto& value : values) {
            value = value * 1'664'525u + 1'013'904'223u;
        }
        benchmark::DoNotOptimize(values.data());
        benchmark::ClobberMemory();
    }
    set_item_and_byte_counters(state, count, sizeof(std::uint64_t) * 2);
}

void BM_Cache_RawSoa(benchmark::State& state)
{
    const auto count = static_cast<std::size_t>(state.range(0));
    std::vector<std::uint64_t> values(count);
    std::iota(values.begin(), values.end(), std::uint64_t{0});

    for (auto _ : state) {
        for (auto& value : values) {
            value = value * 1'664'525u + 1'013'904'223u;
        }
        benchmark::DoNotOptimize(values.data());
        benchmark::ClobberMemory();
    }
    set_item_and_byte_counters(state, count, sizeof(std::uint64_t) * 2);
}

void BM_Access_DodView(benchmark::State& state)
{
    const auto count = static_cast<std::size_t>(state.range(0));
    auto entities = make_dod(count);
    auto health = NumericDod::view<&NumericDod::health>();
    for (auto _ : state) {
        for (auto& value : health) {
            value += 3;
        }
        benchmark::DoNotOptimize(health.data());
        benchmark::ClobberMemory();
    }
    set_item_and_byte_counters(state, count, sizeof(std::uint32_t) * 2);
}

void BM_Access_DodProxy(benchmark::State& state)
{
    const auto count = static_cast<std::size_t>(state.range(0));
    auto entities = make_dod(count);
    for (auto _ : state) {
        for (std::size_t i = 0; i < count; ++i) {
            entities[i].health += 3;
        }
        benchmark::DoNotOptimize(entities.get());
        benchmark::ClobberMemory();
    }
    set_item_and_byte_counters(state, count, sizeof(std::uint32_t) * 2);
}

void BM_Access_Aos(benchmark::State& state)
{
    const auto count = static_cast<std::size_t>(state.range(0));
    auto entities = make_aos(count);
    for (auto _ : state) {
        for (auto& entity : entities) {
            entity.health += 3;
        }
        benchmark::DoNotOptimize(entities.data());
        benchmark::ClobberMemory();
    }
    set_item_and_byte_counters(state, count, sizeof(std::uint32_t) * 2);
}

void BM_Access_RawSoa(benchmark::State& state)
{
    const auto count = static_cast<std::size_t>(state.range(0));
    auto entities = make_soa(count);
    for (auto _ : state) {
        for (auto& value : entities.health) {
            value += 3;
        }
        benchmark::DoNotOptimize(entities.health.data());
        benchmark::ClobberMemory();
    }
    set_item_and_byte_counters(state, count, sizeof(std::uint32_t) * 2);
}

void BM_Access_DodReadOnly(benchmark::State& state)
{
    const auto count = static_cast<std::size_t>(state.range(0));
    auto entities = make_dod(count);
    auto health = NumericDod::view<&NumericDod::health>();
    for (auto _ : state) {
        std::uint64_t sum = 0;
        for (const auto value : health) sum += value;
        benchmark::DoNotOptimize(sum);
    }
    set_item_and_byte_counters(state, count, sizeof(std::uint32_t));
}

void BM_Access_RawSoaReadOnly(benchmark::State& state)
{
    const auto count = static_cast<std::size_t>(state.range(0));
    auto entities = make_soa(count);
    for (auto _ : state) {
        std::uint64_t sum = 0;
        for (const auto value : entities.health) sum += value;
        benchmark::DoNotOptimize(sum);
    }
    set_item_and_byte_counters(state, count, sizeof(std::uint32_t));
}

void BM_Access_DodWriteOnly(benchmark::State& state)
{
    const auto count = static_cast<std::size_t>(state.range(0));
    auto entities = make_dod(count);
    auto health = NumericDod::view<&NumericDod::health>();
    for (auto _ : state) {
        std::uint32_t value = 1;
        for (auto& destination : health) destination = value++;
        benchmark::DoNotOptimize(health.data());
        benchmark::ClobberMemory();
    }
    set_item_and_byte_counters(state, count, sizeof(std::uint32_t));
}

void BM_Access_RawSoaWriteOnly(benchmark::State& state)
{
    const auto count = static_cast<std::size_t>(state.range(0));
    auto entities = make_soa(count);
    for (auto _ : state) {
        std::uint32_t value = 1;
        for (auto& destination : entities.health) destination = value++;
        benchmark::DoNotOptimize(entities.health.data());
        benchmark::ClobberMemory();
    }
    set_item_and_byte_counters(state, count, sizeof(std::uint32_t));
}

void BM_Access_DodColumnCopy(benchmark::State& state)
{
    const auto count = static_cast<std::size_t>(state.range(0));
    auto entities = make_dod(count);
    auto source = NumericDod::view<&NumericDod::health>();
    auto destination = NumericDod::view<&NumericDod::team>();
    for (auto _ : state) {
        std::copy(source.begin(), source.end(), destination.begin());
        benchmark::DoNotOptimize(destination.data());
        benchmark::ClobberMemory();
    }
    set_item_and_byte_counters(state, count, sizeof(std::uint32_t) * 2);
}

void BM_Access_RawSoaColumnCopy(benchmark::State& state)
{
    const auto count = static_cast<std::size_t>(state.range(0));
    auto entities = make_soa(count);
    for (auto _ : state) {
        std::copy(
            entities.health.begin(), entities.health.end(), entities.team.begin()
        );
        benchmark::DoNotOptimize(entities.team.data());
        benchmark::ClobberMemory();
    }
    set_item_and_byte_counters(state, count, sizeof(std::uint32_t) * 2);
}

void BM_Access_PointerAos(benchmark::State& state)
{
    const auto count = static_cast<std::size_t>(state.range(0));
    auto entities = make_pointer_aos(count);
    for (auto _ : state) {
        for (auto& entity : entities) {
            entity->health += 3;
        }
        benchmark::DoNotOptimize(entities.data());
        benchmark::ClobberMemory();
    }
    set_item_and_byte_counters(state, count, sizeof(std::uint32_t) * 2);
}

void BM_Pattern_DodRandom(benchmark::State& state)
{
    const auto count = static_cast<std::size_t>(state.range(0));
    auto entities = make_dod(count);
    const auto order = shuffled_indices(count);
    auto health = NumericDod::view<&NumericDod::health>();
    for (auto _ : state) {
        for (const auto index : order) {
            health[index] += 1;
        }
        benchmark::DoNotOptimize(health.data());
        benchmark::ClobberMemory();
    }
    set_item_and_byte_counters(state, count, sizeof(std::uint32_t) * 2);
}

void BM_Pattern_DodProxyRandom(benchmark::State& state)
{
    const auto count = static_cast<std::size_t>(state.range(0));
    auto entities = make_dod(count);
    const auto order = shuffled_indices(count);
    for (auto _ : state) {
        for (const auto index : order) {
            entities[index].health += 1;
        }
        benchmark::DoNotOptimize(entities.get());
        benchmark::ClobberMemory();
    }
    set_item_and_byte_counters(state, count, sizeof(std::uint32_t) * 2);
}

void BM_Pattern_AosRandom(benchmark::State& state)
{
    const auto count = static_cast<std::size_t>(state.range(0));
    auto entities = make_aos(count);
    const auto order = shuffled_indices(count);
    for (auto _ : state) {
        for (const auto index : order) {
            entities[index].health += 1;
        }
        benchmark::DoNotOptimize(entities.data());
        benchmark::ClobberMemory();
    }
    set_item_and_byte_counters(state, count, sizeof(std::uint32_t) * 2);
}

void BM_Pattern_DodStrided(benchmark::State& state)
{
    const auto count = static_cast<std::size_t>(state.range(0));
    const auto stride = static_cast<std::size_t>(state.range(1));
    auto entities = make_dod(count);
    auto health = NumericDod::view<&NumericDod::health>();
    for (auto _ : state) {
        for (std::size_t i = 0; i < count; i += stride) {
            health[i] += 1;
        }
        benchmark::DoNotOptimize(health.data());
        benchmark::ClobberMemory();
    }
    set_item_and_byte_counters(
        state, (count + stride - 1) / stride, sizeof(std::uint32_t) * 2
    );
}

void BM_Pattern_AosStrided(benchmark::State& state)
{
    const auto count = static_cast<std::size_t>(state.range(0));
    const auto stride = static_cast<std::size_t>(state.range(1));
    auto entities = make_aos(count);
    for (auto _ : state) {
        for (std::size_t i = 0; i < count; i += stride) {
            entities[i].health += 1;
        }
        benchmark::DoNotOptimize(entities.data());
        benchmark::ClobberMemory();
    }
    set_item_and_byte_counters(
        state, (count + stride - 1) / stride, sizeof(std::uint32_t) * 2
    );
}

void BM_Pattern_DodFiltered(benchmark::State& state)
{
    const auto count = static_cast<std::size_t>(state.range(0));
    const int active_percent = static_cast<int>(state.range(1));
    const bool random_pattern = state.range(2) != 0;
    auto entities = make_dod(count);
    auto active = NumericDod::view<&NumericDod::active>();
    auto health = NumericDod::view<&NumericDod::health>();
    if (random_pattern) {
        std::mt19937 random(12345u);
        std::bernoulli_distribution enabled(active_percent / 100.0);
        for (auto& value : active) value = static_cast<std::uint8_t>(enabled(random));
    } else {
        const auto active_count = count * static_cast<std::size_t>(active_percent) / 100;
        for (std::size_t i = 0; i < count; ++i) active[i] = i < active_count;
    }

    for (auto _ : state) {
        std::uint64_t sum = 0;
        for (std::size_t i = 0; i < count; ++i) {
            if (active[i]) sum += health[i];
        }
        benchmark::DoNotOptimize(sum);
    }
    set_item_and_byte_counters(
        state, count, sizeof(std::uint8_t) + sizeof(std::uint32_t)
    );
}

void BM_Pattern_AosFiltered(benchmark::State& state)
{
    const auto count = static_cast<std::size_t>(state.range(0));
    const int active_percent = static_cast<int>(state.range(1));
    const bool random_pattern = state.range(2) != 0;
    auto entities = make_aos(count);
    if (random_pattern) {
        std::mt19937 random(12345u);
        std::bernoulli_distribution enabled(active_percent / 100.0);
        for (auto& entity : entities) entity.active = static_cast<std::uint8_t>(enabled(random));
    } else {
        const auto active_count = count * static_cast<std::size_t>(active_percent) / 100;
        for (std::size_t i = 0; i < count; ++i) entities[i].active = i < active_count;
    }

    for (auto _ : state) {
        std::uint64_t sum = 0;
        for (const auto& entity : entities) {
            if (entity.active) sum += entity.health;
        }
        benchmark::DoNotOptimize(sum);
    }
    set_item_and_byte_counters(
        state, count, sizeof(std::uint8_t) + sizeof(std::uint32_t)
    );
}

void BM_MultiField_Dod(benchmark::State& state)
{
    const auto count = static_cast<std::size_t>(state.range(0));
    const int fields = static_cast<int>(state.range(1));
    auto entities = make_dod(count);
    auto position = NumericDod::view<&NumericDod::position>();
    auto velocity = NumericDod::view<&NumericDod::velocity>();
    auto acceleration = NumericDod::view<&NumericDod::acceleration>();
    auto health = NumericDod::view<&NumericDod::health>();
    auto team = NumericDod::view<&NumericDod::team>();
    auto lifetime = NumericDod::view<&NumericDod::lifetime>();
    auto active = NumericDod::view<&NumericDod::active>();

    for (auto _ : state) {
        for (std::size_t i = 0; i < count; ++i) {
            position[i].x += 1.0F;
            if (fields >= 2) velocity[i].y += position[i].x * 0.001F;
            if (fields >= 4) {
                acceleration[i].z += velocity[i].y * 0.0001F;
                health[i] += team[i] & 1u;
            }
            if (fields >= 7) {
                lifetime[i] -= 0.001F;
                active[i] = static_cast<std::uint8_t>(health[i] != 0);
                team[i] = (team[i] + 1u) & 7u;
            }
        }
        benchmark::DoNotOptimize(position.data());
        benchmark::ClobberMemory();
    }
    const std::size_t logical_bytes = fields == 1 ? 8 : fields == 2 ? 20 :
                                      fields == 4 ? 44 : 65;
    set_item_and_byte_counters(state, count, logical_bytes);
}

void BM_MultiField_Aos(benchmark::State& state)
{
    const auto count = static_cast<std::size_t>(state.range(0));
    const int fields = static_cast<int>(state.range(1));
    auto entities = make_aos(count);
    for (auto _ : state) {
        for (auto& entity : entities) {
            entity.position.x += 1.0F;
            if (fields >= 2) entity.velocity.y += entity.position.x * 0.001F;
            if (fields >= 4) {
                entity.acceleration.z += entity.velocity.y * 0.0001F;
                entity.health += entity.team & 1u;
            }
            if (fields >= 7) {
                entity.lifetime -= 0.001F;
                entity.active = static_cast<std::uint8_t>(entity.health != 0);
                entity.team = (entity.team + 1u) & 7u;
            }
        }
        benchmark::DoNotOptimize(entities.data());
        benchmark::ClobberMemory();
    }
    const std::size_t logical_bytes = fields == 1 ? 8 : fields == 2 ? 20 :
                                      fields == 4 ? 44 : 65;
    set_item_and_byte_counters(state, count, logical_bytes);
}

void BM_MultiField_RawSoa(benchmark::State& state)
{
    const auto count = static_cast<std::size_t>(state.range(0));
    const int fields = static_cast<int>(state.range(1));
    auto entities = make_soa(count);
    for (auto _ : state) {
        for (std::size_t i = 0; i < count; ++i) {
            entities.position[i].x += 1.0F;
            if (fields >= 2) entities.velocity[i].y += entities.position[i].x * 0.001F;
            if (fields >= 4) {
                entities.acceleration[i].z += entities.velocity[i].y * 0.0001F;
                entities.health[i] += entities.team[i] & 1u;
            }
            if (fields >= 7) {
                entities.lifetime[i] -= 0.001F;
                entities.active[i] = static_cast<std::uint8_t>(entities.health[i] != 0);
                entities.team[i] = (entities.team[i] + 1u) & 7u;
            }
        }
        benchmark::DoNotOptimize(entities.position.data());
        benchmark::ClobberMemory();
    }
    const std::size_t logical_bytes = fields == 1 ? 8 : fields == 2 ? 20 :
                                      fields == 4 ? 44 : 65;
    set_item_and_byte_counters(state, count, logical_bytes);
}

class LifecycleDod : public dod::Object<LifecycleDod, max_entities> {
public:
    DOD_PROPERTY(Vec3, position);
    DOD_PROPERTY(std::uint32_t, value);
};

struct LifecycleOop {
    Vec3 position{};
    std::uint32_t value{};
};

void warm_lifecycle_dod()
{
    LifecycleDod warmup;
    warmup.value = 1;
}

void BM_Lifecycle_DodWarmCreate(benchmark::State& state)
{
    const auto count = static_cast<std::size_t>(state.range(0));
    warm_lifecycle_dod();
    for (auto _ : state) {
        auto entities = std::make_unique<LifecycleDod[]>(count);
        benchmark::DoNotOptimize(entities.get());
        benchmark::ClobberMemory();
        state.PauseTiming();
        entities.reset();
        state.ResumeTiming();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * count));
}

void BM_Lifecycle_OopContiguousCreate(benchmark::State& state)
{
    const auto count = static_cast<std::size_t>(state.range(0));
    for (auto _ : state) {
        auto entities = std::make_unique<LifecycleOop[]>(count);
        benchmark::DoNotOptimize(entities.get());
        benchmark::ClobberMemory();
        state.PauseTiming();
        entities.reset();
        state.ResumeTiming();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * count));
}

void BM_Lifecycle_OopIndividualCreate(benchmark::State& state)
{
    const auto count = static_cast<std::size_t>(state.range(0));
    for (auto _ : state) {
        std::vector<std::unique_ptr<LifecycleOop>> entities;
        entities.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            entities.push_back(std::make_unique<LifecycleOop>());
        }
        benchmark::DoNotOptimize(entities.data());
        benchmark::ClobberMemory();
        state.PauseTiming();
        entities.clear();
        state.ResumeTiming();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * count));
}

template <typename Entity>
void BM_Lifecycle_DeleteOrder(benchmark::State& state)
{
    const auto count = static_cast<std::size_t>(state.range(0));
    const int order_kind = static_cast<int>(state.range(1)); // 0 FIFO, 1 LIFO, 2 random
    auto order = shuffled_indices(count);
    if (order_kind == 0) std::iota(order.begin(), order.end(), std::size_t{0});
    if (order_kind == 1) {
        std::iota(order.rbegin(), order.rend(), std::size_t{0});
    }

    for (auto _ : state) {
        state.PauseTiming();
        auto entities = std::make_unique<std::optional<Entity>[]>(count);
        for (std::size_t i = 0; i < count; ++i) entities[i].emplace();
        state.ResumeTiming();
        for (const auto index : order) entities[index].reset();
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * count));
}

template <typename Entity>
void BM_Lifecycle_TenPercentChurn(benchmark::State& state)
{
    const auto count = static_cast<std::size_t>(state.range(0));
    const auto churn_count = count / 10;
    auto entities = std::make_unique<std::optional<Entity>[]>(count);
    for (std::size_t i = 0; i < count; ++i) entities[i].emplace();
    const auto order = shuffled_indices(count);
    std::size_t cursor = 0;

    for (auto _ : state) {
        for (std::size_t i = 0; i < churn_count; ++i) {
            entities[order[(cursor + i) % count]].reset();
        }
        for (std::size_t i = 0; i < churn_count; ++i) {
            entities[order[(cursor + i) % count]].emplace();
        }
        cursor = (cursor + churn_count) % count;
        benchmark::DoNotOptimize(entities.get());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(
        static_cast<std::int64_t>(state.iterations() * churn_count * 2)
    );
}

template <typename Entity>
void BM_Lifecycle_RandomDeleteFraction(benchmark::State& state)
{
    const auto count = static_cast<std::size_t>(state.range(0));
    const auto percent = static_cast<std::size_t>(state.range(1));
    const auto remove_count = count * percent / 100;
    const auto order = shuffled_indices(count);

    for (auto _ : state) {
        state.PauseTiming();
        auto entities = std::make_unique<std::optional<Entity>[]>(count);
        for (std::size_t i = 0; i < count; ++i) entities[i].emplace();
        state.ResumeTiming();

        for (std::size_t i = 0; i < remove_count; ++i) {
            entities[order[i]].reset();
        }
        benchmark::ClobberMemory();

        state.PauseTiming();
        entities.reset();
        state.ResumeTiming();
    }
    state.SetItemsProcessed(
        static_cast<std::int64_t>(state.iterations() * remove_count)
    );
}

template <typename Entity>
void BM_Lifecycle_FillAndEmpty(benchmark::State& state)
{
    const auto count = static_cast<std::size_t>(state.range(0));
    for (auto _ : state) {
        auto entities = std::make_unique<Entity[]>(count);
        benchmark::DoNotOptimize(entities.get());
        entities.reset();
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(
        static_cast<std::int64_t>(state.iterations() * count * 2)
    );
}

class StringDod : public dod::Object<StringDod, lifecycle_entities> {
public:
    DOD_PROPERTY(std::string, text);
};

struct StringOop {
    std::string text;
};

template <typename Entity>
void BM_Lifecycle_LongStringRandomDelete(benchmark::State& state)
{
    const auto count = static_cast<std::size_t>(state.range(0));
    const auto order = shuffled_indices(count);
    const std::string long_text(128, 'x');
    for (auto _ : state) {
        state.PauseTiming();
        auto entities = std::make_unique<std::optional<Entity>[]>(count);
        for (std::size_t i = 0; i < count; ++i) {
            entities[i].emplace();
            entities[i]->text = long_text;
        }
        state.ResumeTiming();
        for (const auto index : order) entities[index].reset();
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * count));
}

void BM_Engine_Motion_Dod(benchmark::State& state)
{
    auto entities = make_dod(engine_entities);
    auto position = NumericDod::view<&NumericDod::position>();
    auto velocity = NumericDod::view<&NumericDod::velocity>();
    auto acceleration = NumericDod::view<&NumericDod::acceleration>();
    constexpr float dt = 1.0F / 60.0F;
    for (auto _ : state) {
        for (std::size_t i = 0; i < engine_entities; ++i) {
            velocity[i].x += acceleration[i].x * dt;
            velocity[i].y += acceleration[i].y * dt;
            position[i].x += velocity[i].x * dt;
            position[i].y += velocity[i].y * dt;
        }
        benchmark::DoNotOptimize(position.data());
        benchmark::ClobberMemory();
    }
    set_item_and_byte_counters(state, engine_entities, sizeof(Vec3) * 5);
}

void BM_Engine_Motion_Aos(benchmark::State& state)
{
    auto entities = make_aos(engine_entities);
    constexpr float dt = 1.0F / 60.0F;
    for (auto _ : state) {
        for (auto& entity : entities) {
            entity.velocity.x += entity.acceleration.x * dt;
            entity.velocity.y += entity.acceleration.y * dt;
            entity.position.x += entity.velocity.x * dt;
            entity.position.y += entity.velocity.y * dt;
        }
        benchmark::DoNotOptimize(entities.data());
        benchmark::ClobberMemory();
    }
    set_item_and_byte_counters(state, engine_entities, sizeof(Vec3) * 5);
}

void BM_Engine_Motion_RawSoa(benchmark::State& state)
{
    auto entities = make_soa(engine_entities);
    constexpr float dt = 1.0F / 60.0F;
    for (auto _ : state) {
        for (std::size_t i = 0; i < engine_entities; ++i) {
            entities.velocity[i].x += entities.acceleration[i].x * dt;
            entities.velocity[i].y += entities.acceleration[i].y * dt;
            entities.position[i].x += entities.velocity[i].x * dt;
            entities.position[i].y += entities.velocity[i].y * dt;
        }
        benchmark::DoNotOptimize(entities.position.data());
        benchmark::ClobberMemory();
    }
    set_item_and_byte_counters(state, engine_entities, sizeof(Vec3) * 5);
}

void BM_Engine_Visibility_Dod(benchmark::State& state)
{
    auto entities = make_dod(engine_entities);
    auto position = NumericDod::view<&NumericDod::position>();
    auto active = NumericDod::view<&NumericDod::active>();
    for (auto _ : state) {
        std::uint64_t visible = 0;
        for (std::size_t i = 0; i < engine_entities; ++i) {
            const float distance_squared = position[i].x * position[i].x +
                                           position[i].y * position[i].y;
            active[i] = static_cast<std::uint8_t>(distance_squared < 4'000'000.0F);
            visible += active[i];
        }
        benchmark::DoNotOptimize(visible);
        benchmark::ClobberMemory();
    }
    set_item_and_byte_counters(state, engine_entities, sizeof(Vec3) + 1);
}

void BM_Engine_Visibility_Aos(benchmark::State& state)
{
    auto entities = make_aos(engine_entities);
    for (auto _ : state) {
        std::uint64_t visible = 0;
        for (auto& entity : entities) {
            const float distance_squared = entity.position.x * entity.position.x +
                                           entity.position.y * entity.position.y;
            entity.active = static_cast<std::uint8_t>(distance_squared < 4'000'000.0F);
            visible += entity.active;
        }
        benchmark::DoNotOptimize(visible);
        benchmark::ClobberMemory();
    }
    set_item_and_byte_counters(state, engine_entities, sizeof(Vec3) + 1);
}

struct Aabb {
    float min_x;
    float min_y;
    float max_x;
    float max_y;
};

void BM_Engine_Aabb_Dod(benchmark::State& state)
{
    auto entities = make_dod(engine_entities);
    auto position = NumericDod::view<&NumericDod::position>();
    auto extent = NumericDod::view<&NumericDod::velocity>();
    std::vector<Aabb> output(engine_entities);
    for (auto _ : state) {
        for (std::size_t i = 0; i < engine_entities; ++i) {
            output[i] = {position[i].x - std::abs(extent[i].x),
                         position[i].y - std::abs(extent[i].y),
                         position[i].x + std::abs(extent[i].x),
                         position[i].y + std::abs(extent[i].y)};
        }
        benchmark::DoNotOptimize(output.data());
        benchmark::ClobberMemory();
    }
    set_item_and_byte_counters(state, engine_entities, sizeof(Vec3) * 2 + sizeof(Aabb));
}

void BM_Engine_Aabb_Aos(benchmark::State& state)
{
    auto entities = make_aos(engine_entities);
    std::vector<Aabb> output(engine_entities);
    for (auto _ : state) {
        for (std::size_t i = 0; i < engine_entities; ++i) {
            const auto& entity = entities[i];
            output[i] = {entity.position.x - std::abs(entity.velocity.x),
                         entity.position.y - std::abs(entity.velocity.y),
                         entity.position.x + std::abs(entity.velocity.x),
                         entity.position.y + std::abs(entity.velocity.y)};
        }
        benchmark::DoNotOptimize(output.data());
        benchmark::ClobberMemory();
    }
    set_item_and_byte_counters(state, engine_entities, sizeof(Vec3) * 2 + sizeof(Aabb));
}

void BM_Engine_Animation_Dod(benchmark::State& state)
{
    auto entities = make_dod(engine_entities);
    auto frame = NumericDod::view<&NumericDod::health>();
    auto frame_count = NumericDod::view<&NumericDod::team>();
    for (auto& count : frame_count) count += 8;
    for (auto _ : state) {
        for (std::size_t i = 0; i < engine_entities; ++i) {
            frame[i] = (frame[i] + 1) % frame_count[i];
        }
        benchmark::DoNotOptimize(frame.data());
        benchmark::ClobberMemory();
    }
    set_item_and_byte_counters(state, engine_entities, sizeof(std::uint32_t) * 3);
}

void BM_Engine_Animation_Aos(benchmark::State& state)
{
    auto entities = make_aos(engine_entities);
    for (auto& entity : entities) entity.team += 8;
    for (auto _ : state) {
        for (auto& entity : entities) {
            entity.health = (entity.health + 1) % entity.team;
        }
        benchmark::DoNotOptimize(entities.data());
        benchmark::ClobberMemory();
    }
    set_item_and_byte_counters(state, engine_entities, sizeof(std::uint32_t) * 3);
}

void BM_Engine_NeighborMotion_Dod(benchmark::State& state)
{
    auto entities = make_dod(engine_entities);
    auto position = NumericDod::view<&NumericDod::position>();
    auto velocity = NumericDod::view<&NumericDod::velocity>();
    for (auto _ : state) {
        for (std::size_t i = 1; i + 1 < engine_entities; ++i) {
            velocity[i].x += (position[i - 1].x + position[i + 1].x -
                              2.0F * position[i].x) * 0.0001F;
        }
        benchmark::DoNotOptimize(velocity.data());
        benchmark::ClobberMemory();
    }
    set_item_and_byte_counters(
        state, engine_entities - 2, sizeof(Vec3) * 3 + sizeof(float) * 2
    );
}

void BM_Engine_NeighborMotion_Aos(benchmark::State& state)
{
    auto entities = make_aos(engine_entities);
    for (auto _ : state) {
        for (std::size_t i = 1; i + 1 < engine_entities; ++i) {
            entities[i].velocity.x +=
                (entities[i - 1].position.x + entities[i + 1].position.x -
                 2.0F * entities[i].position.x) * 0.0001F;
        }
        benchmark::DoNotOptimize(entities.data());
        benchmark::ClobberMemory();
    }
    set_item_and_byte_counters(
        state, engine_entities - 2, sizeof(Vec3) * 3 + sizeof(float) * 2
    );
}

struct RenderCommand {
    float x;
    float y;
    std::uint32_t material;
};

void BM_Engine_RenderExtraction_Dod(benchmark::State& state)
{
    auto entities = make_dod(engine_entities);
    auto position = NumericDod::view<&NumericDod::position>();
    auto active = NumericDod::view<&NumericDod::active>();
    auto team = NumericDod::view<&NumericDod::team>();
    std::vector<RenderCommand> commands(engine_entities);
    for (auto _ : state) {
        std::size_t output = 0;
        for (std::size_t i = 0; i < engine_entities; ++i) {
            if (active[i]) commands[output++] = {position[i].x, position[i].y, team[i]};
        }
        benchmark::DoNotOptimize(commands.data());
        benchmark::DoNotOptimize(output);
        benchmark::ClobberMemory();
    }
    set_item_and_byte_counters(
        state, engine_entities, sizeof(Vec3) + 1 + sizeof(std::uint32_t) +
                                sizeof(RenderCommand)
    );
}

void BM_Engine_RenderExtraction_Aos(benchmark::State& state)
{
    auto entities = make_aos(engine_entities);
    std::vector<RenderCommand> commands(engine_entities);
    for (auto _ : state) {
        std::size_t output = 0;
        for (const auto& entity : entities) {
            if (entity.active) {
                commands[output++] = {entity.position.x, entity.position.y, entity.team};
            }
        }
        benchmark::DoNotOptimize(commands.data());
        benchmark::DoNotOptimize(output);
        benchmark::ClobberMemory();
    }
    set_item_and_byte_counters(
        state, engine_entities, sizeof(Vec3) + 1 + sizeof(std::uint32_t) +
                                sizeof(RenderCommand)
    );
}

void BM_Engine_ProjectileUpdate_Dod(benchmark::State& state)
{
    auto entities = make_dod(engine_entities);
    auto position = NumericDod::view<&NumericDod::position>();
    auto velocity = NumericDod::view<&NumericDod::velocity>();
    auto lifetime = NumericDod::view<&NumericDod::lifetime>();
    std::vector<std::uint32_t> expired(engine_entities);
    for (auto _ : state) {
        std::size_t expired_count = 0;
        for (std::size_t i = 0; i < engine_entities; ++i) {
            position[i].x += velocity[i].x * 0.016F;
            position[i].y += velocity[i].y * 0.016F;
            lifetime[i] -= 0.016F;
            if (lifetime[i] <= 0.0F) expired[expired_count++] = static_cast<std::uint32_t>(i);
        }
        benchmark::DoNotOptimize(expired.data());
        benchmark::DoNotOptimize(expired_count);
        benchmark::ClobberMemory();
    }
    set_item_and_byte_counters(
        state, engine_entities, sizeof(Vec3) * 3 + sizeof(float) * 2
    );
}

void BM_Engine_ProjectileUpdate_Aos(benchmark::State& state)
{
    auto entities = make_aos(engine_entities);
    std::vector<std::uint32_t> expired(engine_entities);
    for (auto _ : state) {
        std::size_t expired_count = 0;
        for (std::size_t i = 0; i < engine_entities; ++i) {
            auto& entity = entities[i];
            entity.position.x += entity.velocity.x * 0.016F;
            entity.position.y += entity.velocity.y * 0.016F;
            entity.lifetime -= 0.016F;
            if (entity.lifetime <= 0.0F) expired[expired_count++] = static_cast<std::uint32_t>(i);
        }
        benchmark::DoNotOptimize(expired.data());
        benchmark::DoNotOptimize(expired_count);
        benchmark::ClobberMemory();
    }
    set_item_and_byte_counters(
        state, engine_entities, sizeof(Vec3) * 3 + sizeof(float) * 2
    );
}

void cache_arguments(benchmark::Benchmark* benchmark)
{
    for (const auto count : {1'024, 8'192, 65'536, 524'288, 4'194'304}) {
        benchmark->Arg(count);
    }
}

BENCHMARK(BM_Cache_DodView)->Apply(cache_arguments);
BENCHMARK(BM_Cache_RawSoa)->Apply(cache_arguments);

BENCHMARK(BM_Access_DodView)->Arg(engine_entities);
BENCHMARK(BM_Access_DodProxy)->Arg(engine_entities);
BENCHMARK(BM_Access_Aos)->Arg(engine_entities);
BENCHMARK(BM_Access_RawSoa)->Arg(engine_entities);
BENCHMARK(BM_Access_DodReadOnly)->Arg(engine_entities);
BENCHMARK(BM_Access_RawSoaReadOnly)->Arg(engine_entities);
BENCHMARK(BM_Access_DodWriteOnly)->Arg(engine_entities);
BENCHMARK(BM_Access_RawSoaWriteOnly)->Arg(engine_entities);
BENCHMARK(BM_Access_DodColumnCopy)->Arg(engine_entities);
BENCHMARK(BM_Access_RawSoaColumnCopy)->Arg(engine_entities);
BENCHMARK(BM_Access_PointerAos)->Arg(lifecycle_entities);

BENCHMARK(BM_Pattern_DodRandom)->Arg(engine_entities);
BENCHMARK(BM_Pattern_DodProxyRandom)->Arg(engine_entities);
BENCHMARK(BM_Pattern_AosRandom)->Arg(engine_entities);
BENCHMARK(BM_Pattern_DodStrided)
    ->Args({engine_entities, 1})->Args({engine_entities, 2})
    ->Args({engine_entities, 4})->Args({engine_entities, 8})
    ->Args({engine_entities, 16});
BENCHMARK(BM_Pattern_AosStrided)
    ->Args({engine_entities, 1})->Args({engine_entities, 2})
    ->Args({engine_entities, 4})->Args({engine_entities, 8})
    ->Args({engine_entities, 16});
BENCHMARK(BM_Pattern_DodFiltered)
    ->Args({engine_entities, 0, 0})->Args({engine_entities, 10, 0})
    ->Args({engine_entities, 50, 0})->Args({engine_entities, 90, 0})
    ->Args({engine_entities, 100, 0})->Args({engine_entities, 0, 1})
    ->Args({engine_entities, 10, 1})->Args({engine_entities, 50, 1})
    ->Args({engine_entities, 90, 1})->Args({engine_entities, 100, 1});
BENCHMARK(BM_Pattern_AosFiltered)
    ->Args({engine_entities, 0, 0})->Args({engine_entities, 10, 0})
    ->Args({engine_entities, 50, 0})->Args({engine_entities, 90, 0})
    ->Args({engine_entities, 100, 0})->Args({engine_entities, 0, 1})
    ->Args({engine_entities, 10, 1})->Args({engine_entities, 50, 1})
    ->Args({engine_entities, 90, 1})->Args({engine_entities, 100, 1});

BENCHMARK(BM_MultiField_Dod)
    ->Args({engine_entities, 1})->Args({engine_entities, 2})
    ->Args({engine_entities, 4})->Args({engine_entities, 7});
BENCHMARK(BM_MultiField_Aos)
    ->Args({engine_entities, 1})->Args({engine_entities, 2})
    ->Args({engine_entities, 4})->Args({engine_entities, 7});
BENCHMARK(BM_MultiField_RawSoa)
    ->Args({engine_entities, 1})->Args({engine_entities, 2})
    ->Args({engine_entities, 4})->Args({engine_entities, 7});

BENCHMARK(BM_Lifecycle_DodWarmCreate)->Arg(lifecycle_entities)->Iterations(3);
BENCHMARK(BM_Lifecycle_OopContiguousCreate)->Arg(lifecycle_entities)->Iterations(3);
BENCHMARK(BM_Lifecycle_OopIndividualCreate)->Arg(lifecycle_entities)->Iterations(3);
BENCHMARK_TEMPLATE(BM_Lifecycle_DeleteOrder, LifecycleDod)
    ->Args({lifecycle_entities, 0})->Args({lifecycle_entities, 1})
    ->Args({lifecycle_entities, 2})->Iterations(3);
BENCHMARK_TEMPLATE(BM_Lifecycle_DeleteOrder, LifecycleOop)
    ->Args({lifecycle_entities, 0})->Args({lifecycle_entities, 1})
    ->Args({lifecycle_entities, 2})->Iterations(3);
BENCHMARK_TEMPLATE(BM_Lifecycle_TenPercentChurn, LifecycleDod)
    ->Arg(lifecycle_entities);
BENCHMARK_TEMPLATE(BM_Lifecycle_TenPercentChurn, LifecycleOop)
    ->Arg(lifecycle_entities);
BENCHMARK_TEMPLATE(BM_Lifecycle_RandomDeleteFraction, LifecycleDod)
    ->Args({lifecycle_entities, 1})->Args({lifecycle_entities, 10})
    ->Args({lifecycle_entities, 50})->Args({lifecycle_entities, 100})
    ->Iterations(3);
BENCHMARK_TEMPLATE(BM_Lifecycle_RandomDeleteFraction, LifecycleOop)
    ->Args({lifecycle_entities, 1})->Args({lifecycle_entities, 10})
    ->Args({lifecycle_entities, 50})->Args({lifecycle_entities, 100})
    ->Iterations(3);
BENCHMARK_TEMPLATE(BM_Lifecycle_FillAndEmpty, LifecycleDod)
    ->Arg(lifecycle_entities)->Iterations(3);
BENCHMARK_TEMPLATE(BM_Lifecycle_FillAndEmpty, LifecycleOop)
    ->Arg(lifecycle_entities)->Iterations(3);
BENCHMARK_TEMPLATE(BM_Lifecycle_LongStringRandomDelete, StringDod)
    ->Arg(100'000)->Iterations(3);
BENCHMARK_TEMPLATE(BM_Lifecycle_LongStringRandomDelete, StringOop)
    ->Arg(100'000)->Iterations(3);

BENCHMARK(BM_Engine_Motion_Dod);
BENCHMARK(BM_Engine_Motion_Aos);
BENCHMARK(BM_Engine_Motion_RawSoa);
BENCHMARK(BM_Engine_Visibility_Dod);
BENCHMARK(BM_Engine_Visibility_Aos);
BENCHMARK(BM_Engine_Aabb_Dod);
BENCHMARK(BM_Engine_Aabb_Aos);
BENCHMARK(BM_Engine_Animation_Dod);
BENCHMARK(BM_Engine_Animation_Aos);
BENCHMARK(BM_Engine_NeighborMotion_Dod);
BENCHMARK(BM_Engine_NeighborMotion_Aos);
BENCHMARK(BM_Engine_RenderExtraction_Dod);
BENCHMARK(BM_Engine_RenderExtraction_Aos);
BENCHMARK(BM_Engine_ProjectileUpdate_Dod);
BENCHMARK(BM_Engine_ProjectileUpdate_Aos);

} // namespace
