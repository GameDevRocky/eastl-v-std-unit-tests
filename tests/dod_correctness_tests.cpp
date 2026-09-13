#include <dod/dod.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

#define REQUIRE(condition)                                                    \
    do {                                                                      \
        if (!(condition)) {                                                   \
            throw std::runtime_error(                                         \
                std::string("requirement failed at line ") +                 \
                std::to_string(__LINE__) + ": " #condition);                \
        }                                                                     \
    } while (false)

template <typename Exception, typename Function>
void require_throws(Function&& function)
{
    bool caught = false;
    try {
        std::forward<Function>(function)();
    } catch (const Exception&) {
        caught = true;
    }
    REQUIRE(caught);
}

struct PairEntity : dod::Object<PairEntity, 32> {
    DOD_PROPERTY(std::uint32_t, id);
    DOD_PROPERTY(std::uint32_t, checksum);
    DOD_PROPERTY(std::string, name);
};

void test_member_view_and_values()
{
    auto entities = std::make_unique<PairEntity[]>(3);
    for (std::uint32_t i = 0; i < 3; ++i) {
        entities[i].id = i + 1;
        entities[i].checksum = (i + 1) * 10;
        entities[i].name = "entity_" + std::to_string(i + 1);
    }

    auto ids = PairEntity::view<&PairEntity::id>();
    auto checksums = PairEntity::view<&PairEntity::checksum>();
    auto names = PairEntity::view<&PairEntity::name>();
    static_assert(std::same_as<decltype(ids), std::span<std::uint32_t>>);
    REQUIRE(ids.size() == 3);
    REQUIRE(checksums.size() == ids.size());
    REQUIRE(names.size() == ids.size());
    for (std::size_t i = 0; i < ids.size(); ++i) {
        REQUIRE(checksums[i] == ids[i] * 10);
        REQUIRE(names[i] == "entity_" + std::to_string(ids[i]));
    }
}

struct HandleEntity : dod::Object<HandleEntity, 8> {
    DOD_PROPERTY(int, value);
};

void test_stale_handles_and_slot_reuse()
{
    auto first = std::make_unique<HandleEntity>();
    auto second = std::make_unique<HandleEntity>();
    const auto stale = first->dod_handle();
    const auto live = second->dod_handle();
    first.reset();

    REQUIRE(!HandleEntity::is_alive(stale));
    REQUIRE(HandleEntity::is_alive(live));
    require_throws<std::logic_error>([&] {
        (void)HandleEntity::dod_dense_index(stale);
    });

    auto replacement = std::make_unique<HandleEntity>();
    const auto reused = replacement->dod_handle();
    REQUIRE(reused.slot == stale.slot);
    REQUIRE(reused.generation != stale.generation);
    REQUIRE(HandleEntity::is_alive(reused));
}

struct SwapEntity : dod::Object<SwapEntity, 16> {
    DOD_PROPERTY(std::uint32_t, key);
    DOD_PROPERTY(std::uint64_t, paired);
    DOD_PROPERTY(std::string, text);
};

void test_swap_and_pop_alignment()
{
    std::array<std::unique_ptr<SwapEntity>, 5> entities;
    for (std::uint32_t i = 0; i < entities.size(); ++i) {
        entities[i] = std::make_unique<SwapEntity>();
        entities[i]->key = i + 1;
        entities[i]->paired = static_cast<std::uint64_t>(i + 1) * 1'000;
        entities[i]->text = std::string(64, static_cast<char>('a' + i)) +
                            "_key_" + std::to_string(i + 1);
    }

    entities[1].reset();
    entities[3].reset();
    auto keys = SwapEntity::view<&SwapEntity::key>();
    auto paired = SwapEntity::view<&SwapEntity::paired>();
    auto text = SwapEntity::view<&SwapEntity::text>();
    REQUIRE(keys.size() == 3);
    for (std::size_t i = 0; i < keys.size(); ++i) {
        REQUIRE(paired[i] == static_cast<std::uint64_t>(keys[i]) * 1'000);
        REQUIRE(text[i] ==
                std::string(64, static_cast<char>('a' + keys[i] - 1)) +
                    "_key_" + std::to_string(keys[i]));
    }
}

struct MoveEntity : dod::Object<MoveEntity, 8> {
    DOD_PROPERTY(int, value);
};

void test_move_semantics()
{
    MoveEntity source;
    source.value = 41;
    const auto original = source.dod_handle();

    MoveEntity moved(std::move(source));
    REQUIRE(moved.dod_handle() == original);
    REQUIRE(moved.value.get() == 41);
    REQUIRE(!source.dod_handle().valid());

    MoveEntity target;
    target.value = 99;
    target = std::move(moved);
    REQUIRE(target.dod_handle() == original);
    REQUIRE(target.value.get() == 41);
    REQUIRE(!moved.dod_handle().valid());
    REQUIRE(MoveEntity::size() == 1);
}

struct CapacityEntity : dod::Object<CapacityEntity, 3> {
    DOD_PROPERTY(int, value);
};

void test_capacity_limit()
{
    auto a = std::make_unique<CapacityEntity>();
    auto b = std::make_unique<CapacityEntity>();
    auto c = std::make_unique<CapacityEntity>();
    require_throws<std::length_error>([] {
        auto overflow = std::make_unique<CapacityEntity>();
        (void)overflow;
    });
    REQUIRE(CapacityEntity::size() == 3);
}

struct Tracked {
    static inline int alive = 0;
    int value{};

    Tracked() { ++alive; }
    Tracked(Tracked&& other) noexcept : value(other.value) { ++alive; }
    Tracked& operator=(Tracked&&) noexcept = default;
    Tracked(const Tracked&) = delete;
    Tracked& operator=(const Tracked&) = delete;
    ~Tracked() noexcept { --alive; }
};

struct TrackedEntity : dod::Object<TrackedEntity, 8> {
    DOD_PROPERTY(Tracked, tracked);
};

void test_nontrivial_destructor_count()
{
    REQUIRE(Tracked::alive == 0);
    {
        std::array<std::unique_ptr<TrackedEntity>, 4> entities;
        for (auto& entity : entities) {
            entity = std::make_unique<TrackedEntity>();
        }
        REQUIRE(Tracked::alive == 4);
        entities[1].reset();
        REQUIRE(Tracked::alive == 3);
    }
    REQUIRE(Tracked::alive == 0);
}

struct LateColumnEntity : dod::Object<LateColumnEntity, 8> {
    DOD_PROPERTY(int, value);
};
struct LateTag {};

struct ThrowingDefault {
    static inline int alive = 0;
    static inline int remaining = -1;

    ThrowingDefault()
    {
        if (remaining == 0) {
            throw std::runtime_error("intentional constructor failure");
        }
        if (remaining > 0) {
            --remaining;
        }
        ++alive;
    }
    ThrowingDefault(ThrowingDefault&&) noexcept { ++alive; }
    ThrowingDefault& operator=(ThrowingDefault&&) noexcept = default;
    ThrowingDefault(const ThrowingDefault&) = delete;
    ThrowingDefault& operator=(const ThrowingDefault&) = delete;
    ~ThrowingDefault() noexcept { --alive; }
};
struct ThrowingTag {};

void test_late_columns_and_exception_rollback()
{
    auto entities = std::make_unique<LateColumnEntity[]>(3);
    auto& late = LateColumnEntity::dod_column<LateTag, std::uint64_t>();
    REQUIRE(late.size() == 3);
    for (std::size_t i = 0; i < late.size(); ++i) {
        REQUIRE(late[i] == 0);
    }

    ThrowingDefault::remaining = 1;
    require_throws<std::runtime_error>([] {
        (void)LateColumnEntity::dod_column<ThrowingTag, ThrowingDefault>();
    });
    REQUIRE(ThrowingDefault::alive == 0);
    REQUIRE(LateColumnEntity::size() == 3);

    ThrowingDefault::remaining = -1;
    auto& recovered =
        LateColumnEntity::dod_column<ThrowingTag, ThrowingDefault>();
    REQUIRE(recovered.size() == 3);
    REQUIRE(ThrowingDefault::alive == 3);

    entities.reset();
    REQUIRE(ThrowingDefault::alive == 0);
}

struct ChurnEntity : dod::Object<ChurnEntity, 256> {
    DOD_PROPERTY(std::uint32_t, key);
    DOD_PROPERTY(std::uint32_t, inverse);
};

void test_random_churn()
{
    auto slots = std::make_unique<std::optional<ChurnEntity>[]>(256);
    for (std::size_t i = 0; i < 192; ++i) {
        slots[i].emplace();
        slots[i]->key = static_cast<std::uint32_t>(i);
        slots[i]->inverse = ~static_cast<std::uint32_t>(i);
    }

    std::mt19937 random(0xC0FFEEu);
    std::uniform_int_distribution<std::size_t> slot_distribution(0, 255);
    for (std::uint32_t step = 0; step < 20'000; ++step) {
        const auto index = slot_distribution(random);
        if (slots[index]) {
            slots[index].reset();
        } else if (ChurnEntity::size() < 256) {
            slots[index].emplace();
            slots[index]->key = step;
            slots[index]->inverse = ~step;
        }

        if ((step % 257) == 0) {
            auto keys = ChurnEntity::view<&ChurnEntity::key>();
            auto inverses = ChurnEntity::view<&ChurnEntity::inverse>();
            REQUIRE(keys.size() == ChurnEntity::size());
            for (std::size_t i = 0; i < keys.size(); ++i) {
                REQUIRE(inverses[i] == ~keys[i]);
            }
        }
    }

    for (std::size_t i = 0; i < 256; ++i) {
        slots[i].reset();
    }
    REQUIRE(ChurnEntity::size() == 0);
}

using TestFunction = void (*)();

struct TestCase {
    const char* name;
    TestFunction function;
};

} // namespace

int main()
{
    const std::array tests{
        TestCase{"member view and values", test_member_view_and_values},
        TestCase{"stale handles and slot reuse", test_stale_handles_and_slot_reuse},
        TestCase{"swap-and-pop column alignment", test_swap_and_pop_alignment},
        TestCase{"move semantics", test_move_semantics},
        TestCase{"capacity limit", test_capacity_limit},
        TestCase{"nontrivial destructor count", test_nontrivial_destructor_count},
        TestCase{"late columns and exception rollback", test_late_columns_and_exception_rollback},
        TestCase{"random churn", test_random_churn},
    };

    int failures = 0;
    for (const auto& test : tests) {
        try {
            test.function();
            std::cout << "[PASS] " << test.name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[FAIL] " << test.name << ": " << error.what() << '\n';
        }
    }

    std::cout << tests.size() - failures << '/' << tests.size()
              << " correctness tests passed\n";
    return failures == 0 ? 0 : 1;
}
