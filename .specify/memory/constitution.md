<!-- SYNC: v1.14.0→1.15.0 | VIII: Test failure ownership rule -->

# VST Plugin Development Constitution

**Version**: 1.15.0 | **Ratified**: 2025-12-21 | **Last Amended**: 2026-02-10

---

## Core Principles

### I. VST3 Architecture Separation

**Non-Negotiable Rules:**
- Processor and Controller MUST be separate classes; set `kDistributable` flag when they can run in different processes
- Use `IMessage`, `IConnectionPoint`, `IAttributeList` for all processor-controller communication
- Processor MUST function correctly without controller instantiation
- State synchronization via `setComponentState()` - controller syncs to processor, never reverse
- NEVER send raw pointers via IMessage

### II. Real-Time Audio Thread Safety

**Non-Negotiable Rules:**
- NEVER allocate memory in audio callbacks (`malloc`, `new`, or allocating STL operations)
- NEVER use locks, mutexes, or blocking primitives in audio thread
- NEVER perform file I/O, network ops, or system calls in audio thread
- NEVER throw/catch exceptions in audio thread
- Pre-allocate ALL buffers in `initialize()` or `setupProcessing()` before `setActive(true)`
- Audio callbacks MUST complete within buffer duration (1-10ms typical)
- Use lock-free SPSC queues for inter-thread communication

### III. Modern C++ Standards

**Non-Negotiable Rules:**
- Target C++17 minimum; prefer C++20 where SDK-compatible
- Use RAII for ALL resource management
- Prefer `std::unique_ptr`; use `std::shared_ptr` only when genuinely needed
- NEVER use raw `new`/`delete` - wrap in smart pointers immediately
- Use `constexpr` and `const` aggressively
- Prefer `std::array` over C-style arrays; `std::span` for views (C++20)

### IV. SIMD & DSP Optimization

**Non-Negotiable Rules:**
- Align audio buffers to 16-byte (SSE) or 32-byte (AVX) boundaries
- Process audio in contiguous, sequential memory patterns
- Minimize branching in inner loops; prefer branchless algorithms
- Avoid virtual function calls in tight processing loops
- Profile before optimizing - measure actual bottlenecks
- **SIMD Viability Analysis**: During `/speckit.plan`, MUST evaluate whether the DSP algorithm is amenable to SIMD optimization. Document the verdict and reasoning in the plan's "SIMD Optimization Analysis" section. SIMD is NOT always beneficial — feedback loops, narrow parallelism, and branch-heavy code can make it counterproductive. When SIMD is not viable, document why and identify alternative optimization strategies (fast approximations, lookup tables, algorithmic simplifications).
- **Scalar-First Workflow**: SIMD MUST NEVER be implemented first. Phase 1: implement full algorithm with scalar code + complete test suite + CPU baseline. Phase 2 (if SIMD viable): add SIMD-optimized path behind the same API, keeping scalar as fallback. The Phase 1 tests serve as the correctness oracle for Phase 2.

### V. VSTGUI Development

**Non-Negotiable Rules:**
- Use `UIDescription` XML and WYSIWYG editor for layout
- Implement `VST3EditorDelegate` for custom view creation
- UI thread MUST NEVER directly access audio processing data
- Use `IParameterChanges` for UI→Processor updates
- All parameter values normalized (0.0-1.0) at VST boundary

### VI. Cross-Platform Compatibility (CRITICAL)

**Non-Negotiable Rules:**
- NEVER use Windows-native APIs (Win32, COM) or macOS-native APIs (Cocoa, AppKit) for UI
- ALWAYS use VSTGUI cross-platform abstractions (COptionMenu, CFileSelector, etc.)
- Platform-specific code ONLY for: debug logging (guarded), optimizations with fallbacks, documented bug workarounds
- ANY platform-specific solution requires explicit user approval
- CI/CD MUST build and test on all three platforms

### VII. Project Structure & Build System

**Non-Negotiable Rules:**
- Use Modern CMake (3.20+) with target-based configuration
- Directory structure MUST follow monorepo layout:
  ```
  dsp/                          # Shared KrateDSP library (Krate::DSP namespace)
  ├── include/krate/dsp/        # Public headers (use <krate/dsp/...>)
  │   ├── core/                 # Layer 0
  │   ├── primitives/           # Layer 1
  │   ├── processors/           # Layer 2
  │   ├── systems/              # Layer 3
  │   └── effects/              # Layer 4
  └── tests/                    # DSP unit tests

  plugins/<plugin>/             # Individual plugins (e.g., plugins/iterum/)
  ├── src/                      # Plugin source
  ├── tests/                    # Plugin tests
  └── resources/                # UI resources, installers
  ```
- DSP: angle bracket includes `#include <krate/dsp/...>`
- Plugin: relative includes `#include "processor/processor.h"`
- External deps via git submodules in `extern/` with pinned versions
- Must build on Windows (MSVC), macOS (Clang), Linux (GCC)

### VIII. Testing Discipline

**Non-Negotiable Rules:**
- DSP algorithms in `dsp/include/krate/dsp/` MUST be pure functions testable without VST infrastructure
- Unit tests MUST cover all DSP algorithms with known input/output pairs
- Integration tests MUST verify plugin loads correctly in test host
- Tests MUST run in CI/CD on every commit
- NEVER commit code that breaks existing tests
- **Warning & Test Ownership**: You are responsible for ALL compiler warnings, static analysis findings, and test failures encountered during a build, clang-tidy run, or test run — not just those in files belonging to the current spec. If you see a warning or failing test anywhere in the codebase, you MUST investigate and fix it (or add a NOLINT with documented justification if a warning is unfixable). "Pre-existing" and "flaky" are not excuses to ignore failures — a failing test means something is broken, and your job is to find out what. Every build and test run is an opportunity to leave the codebase cleaner than you found it.

---

## Technical Constraints

### SDK & Platform Requirements

| Component | Version |
|-----------|---------|
| VST3 SDK | 3.7.x+ (git submodule) |
| VSTGUI | 4.12+ |
| C++ | C++17 min, C++20 preferred |
| CMake | 3.20+ |

| Platform | Compiler | Status |
|----------|----------|--------|
| Windows 10/11 | MSVC 2019+ | Required |
| macOS 11+ | Clang/Xcode 13+ | Required |
| Linux | GCC 10+ / Clang 12+ | Optional |

### Performance Targets

| Metric | Target |
|--------|--------|
| Audio callback | < 50% buffer time |
| Memory alloc in audio | 0 |
| Plugin scan time | < 500ms |

### Forbidden Patterns

`new`/`delete` without smart pointer • `malloc`/`free` in audio • `std::mutex` in audio thread • `vector::push_back` in audio • `dynamic_cast` in audio path • `throw`/`catch` in audio • Global mutable state without sync • Busy-wait spinlocks

### Cross-Platform Compatibility

#### Floating-Point

| Issue | Solution |
|-------|----------|
| `-ffast-math` breaks `std::isnan()` | Use bit manipulation + compile with `-fno-fast-math -fno-finite-math-only` |
| MSVC/Clang precision differs at 7th decimal | Use ≤6 decimal places in tests; use `Approx().margin()` |
| Denormals cause 100x CPU slowdown | Enable FTZ/DAZ in audio thread |
| `std::pow/log10/exp` not constexpr in MSVC | Use custom Taylor series |

#### SIMD & Alignment

| Issue | Solution |
|-------|----------|
| SSE/AVX alignment | Use `alignas(32)` for buffers; `_mm_malloc`/`_mm_free` for dynamic |
| x86 vs ARM | Use SIMD abstraction or separate code paths |
| Apple Silicon | Build universal binaries (x86_64 + arm64) |

#### Threading

| Issue | Solution |
|-------|----------|
| `std::atomic<T>::is_lock_free()` varies | Only `std::atomic_flag` guaranteed lock-free; verify others |
| ARM weak memory model | Use `acquire`/`release` not `seq_cst` |
| Thread priority | Let host manage; don't set manually |

#### Build System

| Issue | Solution |
|-------|----------|
| macOS requires Xcode generator | Use `-G Xcode` for Objective-C++ support |
| VST3 ABI is compiler-dependent | Use recommended compiler per platform |
| Windows UTF-16 paths | Use UTF-8 internally; convert for Windows APIs |

---

## Development Workflow

### Code Review Requirements

1. Constitution compliance verified
2. Thread safety audited for audio code
3. No new allocations in audio paths
4. New DSP code includes unit tests
5. CI passes on all required platforms

### Profiling Protocol

1. Profile in Release with representative audio
2. Identify bottlenecks with CPU profiler
3. Document baseline before changes
4. Verify improvement with same methodology

---

## Advanced Principles

### IX. Layered DSP Architecture

**Non-Negotiable Rules:**
- 5-layer architecture: Layer 0 (core) → Layer 1 (primitives) → Layer 2 (processors) → Layer 3 (systems) → Layer 4 (effects)
- Each layer can ONLY depend on layers below; NEVER circular dependencies
- Each layer independently testable

See `dsp-architecture` skill for layer details, file locations, and examples.

### X. DSP Processing Constraints

**Non-Negotiable Rules:**
- Oversampling (min 2x) for saturation/distortion/waveshaping
- DC blocking after asymmetric saturation
- Interpolation: Allpass for fixed delays; Linear/Cubic for modulated
- Feedback >100% MUST include soft limiting

See `dsp-architecture` skill for interpolation selection, oversampling guidance, and implementation details.

### XI. Performance Budgets

**Non-Negotiable Rules:**
- Total plugin < 5% single core @ 44.1kHz stereo
- Max delay buffer: 10s @ 192kHz (1.92M samples)
- Zero allocations in audio thread

See `dsp-architecture` skill for per-layer CPU targets and memory guidelines.

### XII. Debugging Discipline (Anti-Pivot)

**Non-Negotiable Rules:**
- **Debug Before Pivot**: When code doesn't work:
  1. Read ALL debug logs
  2. Add logging to trace values
  3. Read framework source code
  4. Identify EXACT divergence point
  5. ONLY then consider alternatives

- **Framework Commitment**: The framework works in thousands of plugins. If it's not working, YOU are doing something wrong. Your job is to find WHAT, not abandon the framework.

- **Forbidden Patterns**: "Let me try something else" without investigating WHY • "Maybe framework bug" • Trying 3+ approaches without reading debug output

### XIII. Test-First Development

**Non-Negotiable Rules:**
- **Context Verification**: Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- **Test Before Implementation**: Write failing test FIRST, then implement
- **Bug-First Testing**: Reproduce bug in test → verify fails → fix → verify passes
- **Explicit Todo Items**:
  1. Write failing test
  2. Implement to make test pass
  3. Verify all tests pass
  4. Commit

### XIV. Living Architecture Documentation

**Non-Negotiable Rules:**
- Architecture documentation MUST exist at `specs/_architecture_/`
- Every spec implementation MUST update relevant section files as final task
- Organized by DSP layer (0-4), not chronologically
- Each component: purpose, API, location, "when to use"
- Index file at `specs/_architecture_/README.md` provides overview and links to sections

### XV. Pre-Implementation Research (ODR Prevention)

**Non-Negotiable Rules:**
- **Search Before Creating**: `grep -r "class ClassName" dsp/ plugins/`
- **Check `specs/_architecture_/`** for existing components
- Same namespace + same name = undefined behavior (ODR violation)

See `dsp-architecture` skill for ODR symptoms and prevention patterns.

### XVI. Honest Completion (Anti-Cheating)

**Non-Negotiable Rules:**
- **Definition of "Done"**: ALL acceptance criteria met, tests at spec thresholds, no placeholders/TODOs, performance targets measured
- **Forbidden Patterns**:
  - Relaxing test thresholds to pass
  - Placeholder values marked "needs proper design"
  - Removing scope without declaration
  - Saying "tests pass" when tests were weakened
  - Filling compliance table from memory or assumptions without re-reading the actual code and test output
  - Writing ✅ MET for any requirement without having just verified it against the implementation
  - Using vague evidence like "implemented" or "test passes" — evidence must include file paths, line numbers, test names, and actual measured values

- **Mandatory Verification Process**: Before claiming complete, perform the following for EVERY FR-xxx and SC-xxx individually:
  1. **Read the requirement** from the spec
  2. **Open and read the implementation code** that satisfies it — cite file path and line number
  3. **Run or read the test output** that proves it — cite the test name and actual result (not just "passes")
  4. **For SC-xxx with numeric thresholds**: Record the actual measured value and compare against the spec target
  5. **Only then** mark the row ✅ MET with this concrete evidence

  If ANY requirement is not met, the spec is NOT complete. Do not mark ✅ and move on — mark ❌ and document why.

- **Compliance Table Format** (evidence column must contain specifics, not summaries):
  | Requirement | Status | Evidence |
  |-------------|--------|----------|
  | FR-001 | ✅ MET | `wavetable_oscillator.h:42` — interpolation method selection; test `WavetableInterpolation` passes with max error 0.003 |
  | FR-002 | ❌ NOT MET | Mipmap generation not implemented — only single-level table exists |

### XVII. Framework Knowledge Documentation

**Non-Negotiable Rules:**
- **Read Before Working**: The `vst-guide` skill auto-loads for VSTGUI/VST3 work
- **Document Discoveries**: Log non-obvious framework behavior immediately after fix
- **Incident Log**: Date, symptom, root cause, solution, time wasted, lesson

### XVIII. Spec Numbering

**Non-Negotiable Rules:**
- **NEVER check branches** for spec numbering (branches deleted after merge)
- **ALWAYS check specs/ directory**: `ls specs/ | grep -E '^[0-9]+' | sed 's/-.*//' | sort -n | tail -1`
- New spec = highest + 1

---

## Governance

### Constitution Authority

This constitution is supreme for development decisions. In conflicts with other docs, constitution prevails.

### Amendment Process

1. Propose with rationale
2. Review codebase impact
3. Update version (MAJOR: removal/incompatible change, MINOR: new principle, PATCH: clarification)
4. Update dependent templates if affected

### Exception Process

If principle cannot be followed:
1. Document constraint
2. Propose minimal deviation
3. Include timeline if temporary
4. Record in Complexity Tracking
