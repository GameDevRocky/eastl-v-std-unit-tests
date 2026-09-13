# DOD benchmark suite

Build the suite in Release mode. Do not benchmark under a debugger.

```powershell
cmake -S . -B build
cmake --build build --config Release
```

## Correctness

```powershell
.\build\Release\dod_correctness_tests.exe
```

This checks field views, stale handles, generation reuse, swap-and-pop column
alignment, move operations, capacity failures, nontrivial destruction, late
column construction, exception rollback, and deterministic random churn.

## Comprehensive benchmarks

```powershell
.\build\Release\dod_comprehensive_benchmarks.exe `
  --benchmark_min_time=2s `
  --benchmark_repetitions=7 `
  --benchmark_report_aggregates_only=true `
  --benchmark_out=build\dod_comprehensive_results.json `
  --benchmark_out_format=json
```

The executable contains these groups:

- `Cache`: scalar working sets from 1 Ki entities through 4 Mi entities.
- `Access`: DOD views, DOD property proxies, AoS, raw SoA, and pointer AoS.
- `Pattern`: random, strided, predictable-filter, and random-filter access.
- `MultiField`: systems touching one, two, four, or all seven fields.
- `Lifecycle`: warm creation, individual allocation, deletion order, partial
  random deletion, 10% churn, fill/empty, and long-string destruction.
- `Engine`: motion, visibility, AABB generation, animation, neighbor motion,
  render extraction, and projectile update/deferred-expiration collection.

The three numeric arguments on filtered benchmarks are entity count, active
percentage, and pattern (`0` means predictable; `1` means randomized). The
two deletion-order arguments are entity count and order (`0` FIFO, `1` LIFO,
`2` random).

Use `--benchmark_filter=Engine` or another group name to run a subset.

## Memory and allocation report

```powershell
.\build\Release\dod_memory_report.exe
```

This reports object sizes, cumulative allocation requests, current process
working set, cold and warm DOD creation, retained columns, raw SoA, contiguous
AoS, and individually allocated OOP objects.

## Parallel saturation

```powershell
.\build\Release\dod_parallel_stress.exe --seconds=10 --entities=1000000
```

This runs a numeric game-update kernel at 1, 2, 4, 8, and all available
hardware threads. Threads receive disjoint contiguous ranges. Creation and
one-percent churn remain on the main thread between runs because the DOD
registry intentionally provides no internal synchronization.

The saturation executable can maintain full CPU load for an extended period.
Monitor system temperatures and reduce `--seconds` if cooling is inadequate.
