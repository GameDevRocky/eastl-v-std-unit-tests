#include <dod/dod.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <new>
#include <string>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#endif

namespace allocation_probe {
std::atomic<std::uint64_t> calls{0};
std::atomic<std::uint64_t> requested_bytes{0};
}

void* operator new(std::size_t size)
{
    size = std::max<std::size_t>(size, 1);
    allocation_probe::calls.fetch_add(1, std::memory_order_relaxed);
    allocation_probe::requested_bytes.fetch_add(size, std::memory_order_relaxed);
    if (void* memory = std::malloc(size)) return memory;
    throw std::bad_alloc();
}

void* operator new[](std::size_t size)
{
    size = std::max<std::size_t>(size, 1);
    allocation_probe::calls.fetch_add(1, std::memory_order_relaxed);
    allocation_probe::requested_bytes.fetch_add(size, std::memory_order_relaxed);
    if (void* memory = std::malloc(size)) return memory;
    throw std::bad_alloc();
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

namespace {

constexpr std::size_t entity_count = 1'000'000;

struct Vec3 {
    float x{};
    float y{};
    float z{};
};

struct MemoryOop {
    Vec3 position{};
    Vec3 velocity{};
    Vec3 acceleration{};
    std::uint32_t health{};
    std::uint32_t team{};
    float lifetime{};
    std::uint8_t active{};
};

class MemoryDod : public dod::Object<MemoryDod, entity_count> {
public:
    DOD_PROPERTY(Vec3, position);
    DOD_PROPERTY(Vec3, velocity);
    DOD_PROPERTY(Vec3, acceleration);
    DOD_PROPERTY(std::uint32_t, health);
    DOD_PROPERTY(std::uint32_t, team);
    DOD_PROPERTY(float, lifetime);
    DOD_PROPERTY(std::uint8_t, active);
};

struct MemorySoa {
    explicit MemorySoa(std::size_t count)
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

std::uint64_t working_set_bytes()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(
            GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
            sizeof(counters))) {
        return counters.WorkingSetSize;
    }
#endif
    return 0;
}

struct Snapshot {
    std::uint64_t allocations;
    std::uint64_t requested;
    std::uint64_t working_set;
};

Snapshot snapshot()
{
    return {
        allocation_probe::calls.load(std::memory_order_relaxed),
        allocation_probe::requested_bytes.load(std::memory_order_relaxed),
        working_set_bytes()
    };
}

double mib(std::uint64_t bytes)
{
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

template <typename Function>
void measure(const char* label, Function&& function)
{
    const Snapshot before = snapshot();
    const auto start = std::chrono::steady_clock::now();
    function();
    const auto elapsed = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start
    ).count();
    const Snapshot after = snapshot();

    std::cout << std::left << std::setw(31) << label
              << std::right << std::setw(11) << std::fixed << std::setprecision(3)
              << elapsed << " ms  "
              << std::setw(9) << (after.allocations - before.allocations)
              << " allocs  " << std::setw(10) << std::setprecision(2)
              << mib(after.requested - before.requested) << " MiB requested  "
              << std::setw(10) << mib(after.working_set) << " MiB working set\n";
}

} // namespace

int main()
{
    std::cout << "One-million-entity memory and cold/warm lifecycle report\n";
    std::cout << "sizeof(MemoryOop): " << sizeof(MemoryOop) << " bytes\n";
    std::cout << "sizeof(MemoryDod): " << sizeof(MemoryDod) << " bytes\n";
    std::cout << "DOD wrapper-only estimate: "
              << std::fixed << std::setprecision(2)
              << mib(sizeof(MemoryDod) * entity_count) << " MiB\n\n";

    std::unique_ptr<MemoryOop[]> oop;
    measure("OOP contiguous cold create", [&] {
        oop = std::make_unique<MemoryOop[]>(entity_count);
    });
    measure("OOP contiguous destroy", [&] { oop.reset(); });

    std::unique_ptr<MemoryDod[]> dod;
    measure("DOD cold create", [&] {
        dod = std::make_unique<MemoryDod[]>(entity_count);
    });
    measure("DOD destroy, columns retained", [&] { dod.reset(); });
    measure("DOD warm create", [&] {
        dod = std::make_unique<MemoryDod[]>(entity_count);
    });
    measure("DOD warm destroy", [&] { dod.reset(); });

    std::unique_ptr<MemorySoa> soa;
    measure("Raw SoA create", [&] {
        soa = std::make_unique<MemorySoa>(entity_count);
    });
    measure("Raw SoA destroy", [&] { soa.reset(); });

    std::vector<std::unique_ptr<MemoryOop>> pointer_oop;
    constexpr std::size_t pointer_count = 250'000;
    measure("OOP 250K individual create", [&] {
        pointer_oop.reserve(pointer_count);
        for (std::size_t i = 0; i < pointer_count; ++i) {
            pointer_oop.push_back(std::make_unique<MemoryOop>());
        }
    });
    measure("OOP 250K individual destroy", [&] {
        pointer_oop.clear();
        pointer_oop.shrink_to_fit();
    });

    std::cout << "\nDOD payload columns remain allocated by the process-wide registry after "
                 "all wrappers are destroyed.\n";
}
