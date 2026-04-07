# Quickstart: Harmonic Physics (122)

**Branch**: `122-harmonic-physics` | **Plugin**: Innexus

## What This Feature Does

Adds a physics-based harmonic processing system to Innexus that makes the harmonic model behave like a physical vibrating body rather than a set of independent sine waves. Three sub-systems:

1. **Warmth** (A1): Soft saturation that compresses loud partials and brings up quiet ones
2. **Coupling** (A2): Nearest-neighbor energy sharing between harmonics
3. **Dynamics** (A3): Per-partial inertia and decay for temporal smoothing

## Key Files

| File | Purpose |
|------|---------|
| `plugins/innexus/src/dsp/harmonic_physics.h` | NEW: All three processors |
| `plugins/innexus/src/plugin_ids.h` | MODIFY: Add kWarmthId(700), kCouplingId(701), kStabilityId(702), kEntropyId(703) |
| `plugins/innexus/src/processor/processor.h` | MODIFY: Add HarmonicPhysics member, 4 atomics, 4 smoothers |
| `plugins/innexus/src/processor/processor.cpp` | MODIFY: Wire processFrame at all 7 loadFrame sites |
| `plugins/innexus/src/controller/controller.cpp` | MODIFY: Register 4 new parameters |
| `plugins/innexus/tests/unit/processor/test_harmonic_physics.cpp` | NEW: Unit tests for all three processors |
| `plugins/innexus/tests/integration/test_harmonic_physics_integration.cpp` | NEW: Full-pipeline integration tests |
| `plugins/innexus/tests/CMakeLists.txt` | MODIFY: Register new test files |

## Build & Test

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Build
"$CMAKE" --build build/windows-x64-release --config Release --target innexus_tests

# Run all Innexus tests
build/windows-x64-release/bin/Release/innexus_tests.exe

# Run only harmonic physics tests
build/windows-x64-release/bin/Release/innexus_tests.exe "HarmonicPhysics*"
```

## Architecture Decision

The `HarmonicPhysics` class lives in `plugins/innexus/src/dsp/` (plugin-local, not shared KrateDSP) following the pattern of `HarmonicModulator` and `EvolutionEngine`. It processes `HarmonicFrame` in-place, modifying only partial amplitudes.

Processing chain: Coupling -> Warmth -> Dynamics -> loadFrame()

All parameters default to 0.0 for bit-exact bypass at plugin load.
