cmake_minimum_required(VERSION 3.24)
project(
    eastl_vs_std
    LANGUAGES CXX
)

include(FetchContent)

FetchContent_Declare(
    eastl
    GIT_REPOSITORY https://github.com/electronicarts/EASTL.git
    GIT_TAG master
)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_MakeAvailable(eastl)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    google_benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.9.5
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(google_benchmark)

FetchContent_Declare(
    oop_dod_backend
    GIT_REPOSITORY https://github.com/GameDevRocky/oop-dod-backend.git
    GIT_TAG main
)

FetchContent_MakeAvailable(oop_dod_backend)

add_executable(
    eastl_vs_std
    src/main.cpp
    src/eastl_allocator.cpp
)

target_link_libraries(
    eastl_vs_std
    PRIVATE
    EASTL
)

target_compile_features(eastl_vs_std PRIVATE cxx_std_20)


add_executable(
    vector_benchmarks
    benchmarks/vector_benchmarks.cpp
    src/eastl_allocator.cpp
)

target_compile_features(vector_benchmarks PRIVATE cxx_std_20)

target_link_libraries(
    vector_benchmarks
    PRIVATE
    EASTL
    benchmark::benchmark_main
)

add_executable(
    player_storage_benchmarks
    benchmarks/player_storage_benchmarks.cpp
)

target_compile_features(player_storage_benchmarks PRIVATE cxx_std_20)

target_link_libraries(
    player_storage_benchmarks
    PRIVATE
    dod::dod
    benchmark::benchmark_main
)

add_executable(
    dod_comprehensive_benchmarks
    benchmarks/dod_comprehensive_benchmarks.cpp
)

target_compile_features(dod_comprehensive_benchmarks PRIVATE cxx_std_20)

target_link_libraries(
    dod_comprehensive_benchmarks
    PRIVATE
    dod::dod
    benchmark::benchmark_main
)

add_executable(
    dod_parallel_stress
    benchmarks/dod_parallel_stress.cpp
)

target_compile_features(dod_parallel_stress PRIVATE cxx_std_20)
target_link_libraries(dod_parallel_stress PRIVATE dod::dod)

add_executable(
    dod_memory_report
    benchmarks/dod_memory_report.cpp
)

target_compile_features(dod_memory_report PRIVATE cxx_std_20)
target_link_libraries(dod_memory_report PRIVATE dod::dod)

if(WIN32)
    target_link_libraries(dod_memory_report PRIVATE psapi)
endif()

add_executable(
    dod_correctness_tests
    tests/dod_correctness_tests.cpp
)

target_compile_features(dod_correctness_tests PRIVATE cxx_std_20)
target_link_libraries(dod_correctness_tests PRIVATE dod::dod)


include(CTest)

if(BUILD_TESTING)
    add_test(NAME test_1 COMMAND eastl_vs_std)
    add_test(NAME dod_correctness COMMAND dod_correctness_tests)
endif()
