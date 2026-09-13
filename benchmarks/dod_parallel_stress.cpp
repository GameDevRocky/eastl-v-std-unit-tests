#include <dod/dod.hpp>

#include <algorithm>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <optional>
#include <random>
#include <string_view>
#include <thread>
#include <vector>

namespace {

constexpr std::size_t maximum_entities = 1'000'000;

struct Vec3 {
    float x{};
    float y{};
    float z{};
};

class StressEntity : public dod::Object<StressEntity, maximum_entities> {
public:
    DOD_PROPERTY(Vec3, position);
    DOD_PROPERTY(Vec3, velocity);
    DOD_PROPERTY(Vec3, acceleration);
    DOD_PROPERTY(std::uint32_t, health);
    DOD_PROPERTY(std::uint8_t, active);
};

struct Options {
    std::size_t entities = maximum_entities;
    int seconds = 5;
};

Options parse_options(int argc, char** argv)
{
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument(argv[i]);
        constexpr std::string_view seconds_prefix = "--seconds=";
        constexpr std::string_view entities_prefix = "--entities=";
        if (argument.starts_with(seconds_prefix)) {
            options.seconds = std::max(1, std::atoi(argv[i] + seconds_prefix.size()));
        } else if (argument.starts_with(entities_prefix)) {
            options.entities = std::min(
                maximum_entities,
                std::max<std::size_t>(1, std::strtoull(
                    argv[i] + entities_prefix.size(), nullptr, 10
                ))
            );
        }
    }
    return options;
}

struct Result {
    unsigned threads{};
    double seconds{};
    std::uint64_t entity_updates{};
    double checksum{};
};

Result run_stress(unsigned thread_count, int seconds)
{
    auto positions = StressEntity::view<&StressEntity::position>();
    auto velocities = StressEntity::view<&StressEntity::velocity>();
    auto accelerations = StressEntity::view<&StressEntity::acceleration>();
    auto health = StressEntity::view<&StressEntity::health>();
    auto active = StressEntity::view<&StressEntity::active>();
    const std::size_t count = positions.size();

    std::barrier start_line(static_cast<std::ptrdiff_t>(thread_count + 1));
    std::atomic<std::uint64_t> total_updates{0};
    std::vector<std::thread> workers;
    workers.reserve(thread_count);
    const auto duration = std::chrono::seconds(seconds);
    std::chrono::steady_clock::time_point deadline;

    for (unsigned worker = 0; worker < thread_count; ++worker) {
        workers.emplace_back([&, worker] {
            const std::size_t begin = count * worker / thread_count;
            const std::size_t end = count * (worker + 1) / thread_count;
            std::uint64_t local_updates = 0;
            start_line.arrive_and_wait();
            do {
                for (std::size_t i = begin; i < end; ++i) {
                    if (!active[i]) continue;
                    velocities[i].x += accelerations[i].x * 0.016F;
                    velocities[i].y += accelerations[i].y * 0.016F;
                    positions[i].x += velocities[i].x * 0.016F;
                    positions[i].y += velocities[i].y * 0.016F;
                    const float distance = positions[i].x * positions[i].x +
                                           positions[i].y * positions[i].y;
                    health[i] -= static_cast<std::uint32_t>(distance > 10'000'000.0F);
                }
                local_updates += end - begin;
            } while (std::chrono::steady_clock::now() < deadline);
            total_updates.fetch_add(local_updates, std::memory_order_relaxed);
        });
    }

    const auto start = std::chrono::steady_clock::now();
    deadline = start + duration;
    start_line.arrive_and_wait();
    for (auto& worker : workers) worker.join();
    const double elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start
    ).count();

    double checksum = 0.0;
    for (std::size_t i = 0; i < count; i += 4096) {
        checksum += positions[i].x + positions[i].y + velocities[i].x + health[i];
    }
    return {thread_count, elapsed, total_updates.load(), checksum};
}

void apply_one_percent_churn(
    std::unique_ptr<std::optional<StressEntity>[]>& entities,
    std::size_t count,
    std::vector<std::size_t>& order,
    std::size_t& cursor
)
{
    const std::size_t churn = std::max<std::size_t>(1, count / 100);
    for (std::size_t i = 0; i < churn; ++i) {
        entities[order[(cursor + i) % count]].reset();
    }
    for (std::size_t i = 0; i < churn; ++i) {
        const auto index = order[(cursor + i) % count];
        entities[index].emplace();
        entities[index]->position = Vec3{static_cast<float>(index), 0.0F, 0.0F};
        entities[index]->velocity = Vec3{1.0F, -0.5F, 0.0F};
        entities[index]->acceleration = Vec3{0.01F, -0.02F, 0.0F};
        entities[index]->health = 100;
        entities[index]->active = 1;
    }
    cursor = (cursor + churn) % count;
}

} // namespace

int main(int argc, char** argv)
{
    const Options options = parse_options(argc, argv);
    const unsigned hardware_threads = std::max(1u, std::thread::hardware_concurrency());
    std::vector<unsigned> thread_counts{1};
    for (unsigned count : {2u, 4u, 8u, 16u}) {
        if (count <= hardware_threads && count < options.entities) thread_counts.push_back(count);
    }
    if (thread_counts.back() != hardware_threads && hardware_threads < options.entities) {
        thread_counts.push_back(hardware_threads);
    }

    std::cout << "DOD sustained numeric update stress\n"
              << "Entities: " << options.entities
              << ", hardware threads: " << hardware_threads
              << ", duration per thread count: " << options.seconds << " s\n"
              << "Structural mutation remains on the main thread between runs.\n\n";

    auto entities = std::make_unique<std::optional<StressEntity>[]>(options.entities);
    for (std::size_t i = 0; i < options.entities; ++i) {
        entities[i].emplace();
        entities[i]->position = Vec3{static_cast<float>(i % 10'000),
                                     static_cast<float>(i % 1'000), 0.0F};
        entities[i]->velocity = Vec3{1.0F, -0.5F, 0.0F};
        entities[i]->acceleration = Vec3{0.01F, -0.02F, 0.0F};
        entities[i]->health = 100;
        entities[i]->active = static_cast<std::uint8_t>((i % 10) != 0);
    }

    auto churn_order = std::vector<std::size_t>(options.entities);
    std::iota(churn_order.begin(), churn_order.end(), std::size_t{0});
    std::mt19937 random(0xC0FFEEu);
    std::shuffle(churn_order.begin(), churn_order.end(), random);
    std::size_t churn_cursor = 0;

    double one_thread_rate = 0.0;
    std::cout << std::left << std::setw(10) << "Threads"
              << std::setw(18) << "M entities/s"
              << std::setw(12) << "Speedup"
              << std::setw(14) << "Efficiency"
              << "Checksum\n";

    for (const unsigned threads : thread_counts) {
        const Result result = run_stress(threads, options.seconds);
        const double rate = static_cast<double>(result.entity_updates) /
                            result.seconds / 1'000'000.0;
        if (threads == 1) one_thread_rate = rate;
        const double speedup = rate / one_thread_rate;
        const double efficiency = speedup / threads * 100.0;
        std::cout << std::left << std::setw(10) << threads
                  << std::setw(18) << std::fixed << std::setprecision(2) << rate
                  << std::setw(12) << speedup
                  << std::setw(14) << efficiency
                  << std::setprecision(3) << result.checksum << '\n';

        apply_one_percent_churn(entities, options.entities, churn_order, churn_cursor);
        if (StressEntity::size() != options.entities) {
            std::cerr << "Registry size changed after churn; aborting.\n";
            return 1;
        }
    }

    std::cout << "\nCompleted without a registry-size or checksum failure.\n";
}
