# Implementation Plan: Vorago Phase 8 — Ecosystem Engine

**Spec:** `specs/vorago-phase8-ecosystem/spec.md` (1525 lines, read in full this session, **after** the
Review notes and the 2026-09-15 Clarifications Q1–Q8 / OQ-1–OQ-5).
**Roadmap:** `specs/Vorago-roadmap.md` Part A → Phase 8 (lines 368–396); reuse row `Ecosystem agents`
(line 123); the ODR note (127–129); the cross-cutting constraints (528–552) including the Dormancy
rule (537–545) and the shared-component rule (550–552); the dependency graph (508–521); roadmap Open
Question 2 (557–558).
**Prototype:** `specs/vorago-phase8-ecosystem/prototype/{ecosystem-sim.js, run.js, FINDINGS.md}` —
all three re-read in full this session. Where this plan states a formula, it is transcribed from
`ecosystem-sim.js` with its line number, not paraphrased.
**Deliverable:** one new Layer 3 header `dsp/include/krate/dsp/systems/ecosystem_engine.h`
(header-only, no `.cpp`), **four** new test TUs, **one** test-local helper header, **two** edits in
`dsp/tests/CMakeLists.txt`, **one** edit in `dsp/lint_all_headers.cpp`. **No existing header is
modified** (FR-090).
**Test target:** `dsp_systems_tests` (all four new TUs). SC-016 regression targets: `dsp_core_tests`,
`dsp_primitives_tests`, `dsp_processors_tests`, `dsp_systems_tests`, `dsp_effects_tests`.
**Plugin work:** none. Vorago's plugin starts at Phase 11.

**The phase in one sentence:** a fixed table of ≤ 48 agents holding `double` energy on a 2-D toroidal
habitat, stepped once every `stepIntervalChunks × 64` samples through thirteen normative stages that
move energy between agents, a 96-cell resource strip field and one pool **without ever creating or
destroying a joule**, publishing one clamped `float` in `[0, 1]` per agent — with **no audio path,
no oscillator, no filter and no Layer 1/2/3 include anywhere**.

---

## S0. Verification ledger — every signature below was read this session

Nothing in this plan is quoted from the spec without re-opening the file. Where the spec and the code
disagree, **the code wins**, and the disagreement is recorded in **S14 (Spec corrections)**.

| Claim this plan is built on | Verified at |
|---|---|
| `class Xorshift32`; `explicit constexpr Xorshift32(uint32_t seedValue = 1) noexcept` | `core/random.h:41`, `:45` |
| `[[nodiscard]] constexpr uint32_t next() noexcept` returns `[1, 2^32-1]` (xorshift 13/17/5) | `core/random.h:50-55` |
| `nextFloat()` bipolar `= float(next()) * kToFloat * 2.0f - 1.0f`; `nextUnipolar()` `= float(next()) * kToFloat` — **both `float`**, `kToFloat = 2.3283064370807974e-10f` | `core/random.h:59-69`, `:89` |
| `constexpr void seed(uint32_t)` substitutes `kDefaultSeed = 2463534242u` for 0 | `core/random.h:72-74`, `:84` |
| `[[nodiscard]] constexpr std::uint32_t deriveStreamSeed(std::uint32_t base, std::size_t salt) noexcept` — lowbias32 finaliser over `base ^ ((salt+1)*0x9E3779B9)`, substitutes `0x2545F491` for a zero hash | `core/random.h:102-113` |
| `detail::isNaN(float)` / `detail::isInf(float)` / `detail::isFinite(float)` — bit-pattern behind `opaqueFloatBits`, `-ffast-math`-proof | `core/db_utils.h:99`, `:260`, `:118` |
| **`detail::isFinite(double)` exists** — bit-pattern behind `opaqueDoubleBits`, mask `0x7FF0000000000000ULL` | `core/db_utils.h:125-129`, barrier at `:107` |
| **`fastExp` / `fastSin` / `fastCos` DO NOT EXIST** — "removed because MSVC's std:: versions were slower. Use std::sin/cos/exp" | `core/fast_math.h:13-15` |
| House shape: `kControlChunkSamples = 64` + live `static_assert`; `kMinUsableSampleRate = 8000.0`; nested `PrepareConfig` (designated-initialisers only); `prepare(double, const PrepareConfig&) noexcept`; `processChunk(…, std::size_t numSamples)` with "`numSamples == 0` applies the current state WITHOUT advancing"; `getAllocatedBytes()` returning `0u`; probe forward-declared in `namespace detail` and befriended | `systems/bloom_engine.h:221`, `:229`, `:338`, `:383`, `:494`, `:817`, `:162`, `:1661` |
| Bloom's **absolute-residue** control clock carried across calls, and the named failure mode (`numSamples / kControlChunkSamples` per call) | `bloom_engine.h:893-916`, `:899` |
| Bloom's setter contract: non-finite float **rejected**, previous value stands | `bloom_engine.h:624-630`, `:648-655` |
| Bloom's **sanitise-then-floor** sample-rate form: `sampleRate_ = std::max(kMinUsableSampleRate, sanitise(sampleRate, 48000.0));`, with the two `sanitise(v, neutral)` overloads (`float`, `double`) defined below the public section | usage `bloom_engine.h:385`; definitions `bloom_engine.h:879-882` |
| Bloom's `setWake` epsilon snap: `w <= kWakeSilenceEpsilon ? 0.0f : w`; `kWakeSilenceEpsilon = 1.0e-6f` | `bloom_engine.h:650-655`, `:292` |
| Bloom snaps the ramp at prepare so there is no 50 ms window at t = 0 (`depthRamp_.snapTo(depth_)`) | `bloom_engine.h:399-400` |
| `kGainRampMs = 50.0f` — the Dormancy rule's re-entry fade, identical in five shipped components | `noise_organism.h:178`, `resonance_drift_network.h:143`, `feedback_ecology.h:417`, `subharmonic_engine.h:177`, `bloom_engine.h:287` |
| `kWakeSilenceEpsilon = 1.0e-6f` in all three sleeping components | `resonance_drift_network.h:266`, `feedback_ecology.h:428`, `bloom_engine.h:292` |
| `ResonanceDriftNetwork::kMinUsableSampleRate = 8000.0` (+ ordering `static_assert`) | `resonance_drift_network.h:281-285` |
| `ResonanceDriftNetwork::setPeakWake(std::size_t, float)` — index guard, `detail::isFinite` guard, `std::clamp(amount, 0.0f, 1.0f)`; doxygen names "a Phase-8 caller driving wake from agent energy" | `resonance_drift_network.h:765-780` |
| `ResonanceDriftNetwork::setPeakDormant(std::size_t, bool)` — "behaviourally indistinguishable" from `setPeakWake(i, 0.0f)` via `gateSteady()` | `resonance_drift_network.h:784-793` |
| `getClampEngagementCount()` `std::uint32_t`; `getAllocatedBytes()` returning `0u` with the "Phase-10 host totals its children uniformly" rationale | `resonance_drift_network.h:904`, `:912` |
| Salt-table house shape: a block of `static constexpr std::size_t kSalt…` constants with non-overlap `static_assert`s, marked **APPEND ONLY** | `resonance_drift_network.h:929-947` |
| `FeedbackEcology::refreshGates()` — the `target != lastGateTarget` guard is **load-bearing, not an optimisation**: "A Phase-8 agent writing one loop's wake per block would otherwise stretch every OTHER loop's ramp without bound" | `feedback_ecology.h:2289-2296` |
| `PrepareConfig` designated-initialiser rationale (a positional brace init hides a narrowing conversion Clang errors on and MSVC does not) | `resonance_drift_network.h:297-306` |
| `render_fingerprint.h`: `kSampleTolerance = 5.0e-4f` `:58`, `kMetricTolerance = 2.5e-4` `:61`, `struct RenderFingerprint` `:63`, `compareFingerprints(…)` `:122` | `tests/test_helpers/render_fingerprint.h` |
| `allocation_detector.h`: `AllocationDetector` `:48`, `AllocationScope` `:111` (RAII start/stop, `getAllocationCount()`) | `tests/test_helpers/allocation_detector.h` |
| `statistical_utils.h`: `computeMean(const float*, size_t)` `:41`, `computeVariance` `:59` (Bessel), `computeStdDev` `:76`, `computeMedian(float*, size_t)` **sorts in place** `:90`; namespace `StatisticalUtils`; **all `float`** | `tests/test_helpers/statistical_utils.h` |
| **Helper root is `tests/test_helpers/`, NOT `dsp/tests/test_helpers/`** | `tests/test_helpers/CMakeLists.txt` |
| Test-local helper headers next to their TUs are house-legal and need **no** CMake edit (only `.cpp` are enumerated) | `dsp/tests/unit/processors/arpeggiator_core_test_helpers.h`, `dsp/tests/unit/systems/harmonic_cloud_pre_amendment_fingerprints.h` |
| Perf idiom: ns per 512-sample block at 48 kHz (one block period = **10 666 667 ns**), best-of-25 × 500 blocks after 400 warm-up, `[.perf]`, **RUN IT ALONE**, and the **stop-and-surface rule** verbatim ("NO IMPLEMENTING AGENT MAY lower […], raise […], relax a threshold, or shrink a workload to make a figure fit. Reduce cost, never move the line.") | `dsp/tests/unit/systems/resonance_drift_network_perf_test.cpp:52-88` |
| `dsp_systems_tests` enumerated list opens `:324`; the Phase-7 TUs sit `:473-476` (`:471-472` are still comment lines); the list closes `:477`; "This list is ENUMERATED, not globbed - an unregistered TU silently drops out of the build and its cases never run" | `dsp/tests/CMakeLists.txt` |
| `-fno-fast-math` block opens `if(CMAKE_CXX_COMPILER_ID MATCHES "Clang\|GNU")` `:569`; the Phase-7 entry `:920`; `PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"` `:921` | `dsp/tests/CMakeLists.txt` |
| `dsp/lint_all_headers.cpp` enumerates every public header; the Vorago Phase-7 include sits `:191`, the Layer-4 block opens `:193` | `dsp/lint_all_headers.cpp` |
| `tools/lint-layers.js` enforces only "layer N includes ≤ N" — it **cannot** see FR-001's stricter "Layer 0 + stdlib only" rule | `tools/lint-layers.js:5-8`, `:60` |
| `tools/lint-odr.js` qualifies nested types by their enclosing class | `tools/lint-odr.js:18-21` |

### S0.1 ODR sweep, re-run this session from the repo root

```
$ grep -rn "Ecosystem" dsp/ plugins/ tools/ --include=*.h --include=*.cpp --include=*.js
   -> 0 hits
$ ls dsp/include/krate/dsp/systems/ecosystem_engine.h
   -> No such file or directory
```

The spec's sweep is confirmed exactly. `EcosystemEngine`, `EcosystemEngine::Kind`,
`EcosystemEngine::PrepareConfig`, `EcosystemEngine::Agent`,
`detail::EcosystemEngineNonFiniteProbe` and `detail::EcosystemEngineInspectProbe` (S1.3, plan
addition A-1) claim **one** namespace-scope name in `Krate::DSP` — `EcosystemEngine` — plus two
forward declarations inside `Krate::DSP::detail`. The roadmap's near-name hazard list
(`ResonatorBank`, `FeedbackNetwork`, `NoiseGenerator`, `GranularEngine`, `PatternScheduler`) is
untouched: none is referenced, included or shadowed.

### S0.2 Prototype transcription ledger

Every rule below was re-read in `ecosystem-sim.js` this session at the line cited. This plan's
formulas are transcriptions, not re-derivations.

| Rule | Prototype line(s) | Plan section |
|---|---|---|
| RNG port (`nextUnipolar`, `nextBipolar`, `range`, `deriveStreamSeed`) | `:33-58` | S1.6 |
| `KINDS` order | `:63` | S1.3 |
| `defaultConfig()` | `:70-152` | S1.2, Appendix-A table |
| `defaultAffinity()` (same kind −1.0, other +0.45) | `:154-167` | S1.2 |
| Seeded streams, salts 0–6 | `:171-177`, `:219` | S1.6 |
| Initial state + three-way energy partition + seeded resource fill | `:179-232` | S2.1 |
| `wrapDelta` | `:240-246` | S4.0 |
| Pair pass: kernel, `w < 1e-6` cutoff, exchange record, affinity, crowding, sync | `:266-321` | S4.1 |
| Exchange pass 2 (guarded `scale`) | `:325-347` | S4.2 |
| Appetite gate | `:352-360` | S4.3 |
| Proportional regrowth + single withdrawal budget `avail` | `:362-400` | S4.4 |
| Demand-scaled grazing + foraging gradient | `:402-451` | S4.5 |
| Global feed + nonlinear leak | `:453-470` | S4.6 |
| Integrate, zero clamp, carrying-capacity spill | `:472-489` | S4.7 |
| Movement, slew limit, torus wrap | `:490-500` | S4.8 |
| Bounded OU frequency drift | `:502-510` | S4.9 |
| Phase integrate | `:511` | S4.9 |
| Pool update, **no clamp**, negative-pool latch | `:513-521` | S4.10 |
| `entropy()`, `totalEnergy()`, `anyNonFinite()` | `:532-558` | S8 |
| Liveness thresholds (`kLateWindowSeconds=600`, `kFrozenActivity=0.02`, `kAliveActivity=0.10`, `kAliveMaxFrozenFraction=0.25`, `kCycleAutocorr=0.8`) | `:566-583` | S10.2 |
| Recurrence cycle scan (decay to 0.2, then peak ≤ 0.8) | `:724-757` | S10.2 |
| Hostile box | `run.js:302-332` | S10.3 |
| Sane box | `run.js:338-377` | S10.3 |
| `assertCoverage` + exemptions | `run.js:381-415` | S10.3 |

---

## S1. Component: `EcosystemEngine`

### S1.1 Header, layer, includes (FR-001)

`dsp/include/krate/dsp/systems/ecosystem_engine.h`, `namespace Krate::DSP`, Layer 3, header-only,
`#pragma once`.

```cpp
#include <krate/dsp/core/random.h>     // Xorshift32, deriveStreamSeed
#include <krate/dsp/core/db_utils.h>   // detail::isFinite(float), detail::isFinite(double)

#include <algorithm>   // std::clamp, std::min, std::max, std::sort (NOT used in the header)
#include <array>
#include <cmath>       // std::exp, std::sin, std::sqrt, std::floor, std::pow, std::ceil
#include <cstddef>
#include <cstdint>
```

That is the complete include set. **No Layer 1, 2 or 3 header. No Vorago header. No consumer
header.** In particular `smoother.h` (Layer 1, `LinearRamp`) is *not* included — the wake ramp is
four lines of arithmetic on the control-step grid (S6.2) and pulling a Layer-1 header in for it
would break FR-001 for no gain.

**`lint-layers.js` cannot see this rule** (S0: it only forbids *upward* includes, and Layer 3 →
Layer 1/2 is legal to it). FR-001's stricter promise is therefore enforced by a **test-side literal
check** in `ecosystem_engine_test.cpp` (S10.4, SC-015 arm) that reads the header's own include block
and asserts every `<krate/dsp/...>` line names `core/`. This is the same shape as the enumerated-TU
guard: the thing that can silently rot gets a test, not a comment.

### S1.2 Constants — all `static constexpr`, class scope, `kPascalCase` (FR-004)

```cpp
// ---- capacities (FR-004) -------------------------------------------------
static constexpr std::size_t kMaxAgents        = 48;   // roadmap line 381 upper bound, OQ-3
static constexpr std::size_t kMinAgents        = 1;
static constexpr std::size_t kMaxResourceCells = 96;   // run.js:317 upper bound
static constexpr std::size_t kNumKinds         = 5;    // ecosystem-sim.js:63
static constexpr std::size_t kMaxPairs         = kMaxAgents * (kMaxAgents - 1) / 2;  // 1128
static_assert(kMaxPairs == 1128, "pair scratch sizing");
static_assert(kMaxAgents <= 255, "pair index arrays are std::uint8_t");

// ---- the shared control grid --------------------------------------------
static constexpr std::size_t kControlChunkSamples = 64;   // bloom_engine.h:221
static_assert(kControlChunkSamples == 64, "shared 64-sample control grid");
static constexpr std::size_t kMinStepIntervalChunks = 1;   // FR-082
static constexpr std::size_t kMaxStepIntervalChunks = 64;  // FR-082
static constexpr std::size_t kDefaultStepIntervalChunks = 8;  // 512 samples = the tuned dt (OQ-2)

static constexpr double kMinUsableSampleRate = 8000.0;   // resonance_drift_network.h:281
static constexpr double kDefaultSampleRate   = 48000.0;

// ---- energy budget domain (FR-005) --------------------------------------
static constexpr double kMinEnergyBudget = 1.0e-3;   // FR-061's divisor floor - LOAD-BEARING
static constexpr double kMaxEnergyBudget = 1.0e3;

// ---- publication (FR-061, FR-063, FR-070) -------------------------------
static constexpr float  kWakeSilenceEpsilon = 1.0e-6f;  // feedback_ecology.h:428
static constexpr float  kGainRampMs         = 50.0f;    // noise_organism.h:178
static constexpr double kOutputAnchor       = 0.5;      // FR-061: mean share publishes 0.5

// ---- affinity matrix range (FR-031) -------------------------------------
// FR-031 makes the matrix runtime-settable per entry AND clamped [-2, +2].
// setAffinity() is the ONLY setter whose argument is not an Appendix-A scalar,
// so its bounds live here rather than in S1.5's range comment column.
static constexpr float kMinAffinity = -2.0f;
static constexpr float kMaxAffinity = +2.0f;

// ---- numerics ------------------------------------------------------------
static constexpr double kNeighbourWeightCutoff = 1.0e-6;  // FR-012, ecosystem-sim.js:274
static constexpr double kDenormalCellGuard     = 1.0e-30; // FR-043
static constexpr double kUnitVectorEpsilon     = 1.0e-9;  // ecosystem-sim.js:294

// ---- SC-012's structural anchor (plan addition A-2) ---------------------
/// The number of knobs in the spec's Appendix A (affinity counted as one).
/// SC-012's fuzz-coverage table is asserted against this. RAISE IT IN THE SAME
/// COMMIT THAT ADDS A SETTER, or EcosystemEngine_FuzzCoverageIsComplete fails.
static constexpr std::size_t kConfigKnobCount = 28;
```

Every Appendix-A default is a **member initialiser** on the corresponding private field (S1.5), not
a `kDefault…` constant, except where a range bound is needed by a `std::clamp` — those get a
`kMin…` / `kMax…` pair. The full range table is S1.5's comment block and is transcribed verbatim
from Appendix A.

### S1.3 Nested types

```cpp
/// FR-011. APPEND ONLY - this becomes a persisted plugin parameter at Phase 12,
/// and the affinity matrix is INDEXED BY IT, so the order is normative
/// (ecosystem-sim.js:63).
enum class Kind : std::uint8_t { Partial = 0, Resonator = 1, Noise = 2, Feedback = 3, Ghost = 4 };
static_assert(static_cast<std::size_t>(Kind::Ghost) + 1u == kNumKinds, "Kind roster");

/// FR-005. Callers MUST use designated initialisers - PrepareConfig{.agentCount = 24} -
/// so no narrowing conversion hides in a positional brace init (Clang errors where
/// MSVC does not; resonance_drift_network.h:297-306).
struct PrepareConfig {
    std::size_t agentCount          = 32;    // clamped [kMinAgents, kMaxAgents]
    std::size_t resourceCells       = 64;    // clamped [1, kMaxResourceCells]
    double      energyBudget        = 1.0;   // clamped [kMinEnergyBudget, kMaxEnergyBudget]
    double      initialPoolFraction = 0.5;   // clamped [0.1, 0.9]
    std::size_t stepIntervalChunks  = kDefaultStepIntervalChunks;  // clamped [1, 64]
};
```

`PrepareConfig` carries **exactly** FR-006's prepare-time set and nothing else. Every rule knob is a
runtime setter (FR-064) with a member-initialiser default. This is the shape that makes FR-006 a
compile-time fact rather than a documented promise.

Two probe structs, both forward-declared in `namespace Krate::DSP::detail` above the class and both
befriended at the bottom of the private section (the `bloom_engine.h:162` / `:1661` pattern):

```cpp
namespace detail {
/// Defined ONLY in dsp/tests/unit/systems/ecosystem_engine_nonfinite_test.cpp.
struct EcosystemEngineNonFiniteProbe;
/// Defined ONLY in dsp/tests/unit/systems/ecosystem_engine_test.cpp.
struct EcosystemEngineInspectProbe;
}  // namespace detail
```

**Plan addition A-1: two probes, not one.** SC-020 (exchange scale well-formed) and SC-021 (denormal
cell snap) are probe-level but must run in the **shipping** `/fp:fast` + `-ffast-math` mode, not
under `-fno-fast-math`. A single probe struct can have only one definition in the program, which
would drag both criteria into the non-finite TU and prove them in a mode the header never ships in.
Two forward declarations and two `friend` lines cost nothing and keep each criterion in the TU whose
compile flags match what it is asserting about.

`Agent` is **not** a nested struct in this design — see S1.5 (structure-of-arrays). The spec's
new-components table lists `EcosystemEngine::Agent`; S14 records the deviation.

### S1.4 Public API — the complete shape the implementer types

```cpp
class EcosystemEngine {
public:
    // constants (S1.2), Kind, PrepareConfig  ................................

    EcosystemEngine() noexcept = default;
    EcosystemEngine(const EcosystemEngine&) = default;
    EcosystemEngine& operator=(const EcosystemEngine&) = default;
    EcosystemEngine(EcosystemEngine&&) noexcept = default;
    EcosystemEngine& operator=(EcosystemEngine&&) noexcept = default;

    // ---- lifecycle (FR-005, FR-080) --------------------------------------
    void prepare(double sampleRate, const PrepareConfig& config) noexcept;
    void reset() noexcept;
    void setSeed(std::uint32_t seed) noexcept;

    // ---- the clock (FR-081) ----------------------------------------------
    void processChunk(std::size_t numSamples) noexcept;

    // ---- rule knobs (FR-064): every setter has a getter -------------------
    void  setKernelSigma(float v) noexcept;      [[nodiscard]] float getKernelSigma() const noexcept;
    void  setExchangeRate(float v) noexcept;     [[nodiscard]] float getExchangeRate() const noexcept;
    void  setPredation(float v) noexcept;        [[nodiscard]] float getPredation() const noexcept;
    void  setPreyFloorShares(float v) noexcept;  [[nodiscard]] float getPreyFloorShares() const noexcept;
    void  setCapacityShares(float v) noexcept;   [[nodiscard]] float getCapacityShares() const noexcept;
    void  setLeakRate(float v) noexcept;         [[nodiscard]] float getLeakRate() const noexcept;
    void  setLeakExponent(float v) noexcept;     [[nodiscard]] float getLeakExponent() const noexcept;
    void  setMoveRate(float v) noexcept;         [[nodiscard]] float getMoveRate() const noexcept;
    void  setMaxSpeed(float v) noexcept;         [[nodiscard]] float getMaxSpeed() const noexcept;
    void  setForageRate(float v) noexcept;       [[nodiscard]] float getForageRate() const noexcept;
    void  setCrowding(float v) noexcept;         [[nodiscard]] float getCrowding() const noexcept;
    void  setCrowdingRadius(float v) noexcept;   [[nodiscard]] float getCrowdingRadius() const noexcept;
    void  setSyncRate(float v) noexcept;         [[nodiscard]] float getSyncRate() const noexcept;
    void  setCellCapacityShares(float v) noexcept;
                                                 [[nodiscard]] float getCellCapacityShares() const noexcept;
    void  setRegenRate(float v) noexcept;        [[nodiscard]] float getRegenRate() const noexcept;
    void  setGrazeRate(float v) noexcept;        [[nodiscard]] float getGrazeRate() const noexcept;
    void  setFeedRate(float v) noexcept;         [[nodiscard]] float getFeedRate() const noexcept;
    void  setSatiationShares(float v) noexcept;  [[nodiscard]] float getSatiationShares() const noexcept;
    void  setAppetiteDepth(float v) noexcept;    [[nodiscard]] float getAppetiteDepth() const noexcept;
    void  setFreqRangeHz(float lo, float hi) noexcept;   ///< ONE setter, two knobs; ordered
    [[nodiscard]] float getFreqLoHz() const noexcept;
    [[nodiscard]] float getFreqHiHz() const noexcept;
    void  setFreqDrift(float v) noexcept;        [[nodiscard]] float getFreqDrift() const noexcept;
    void  setAffinity(Kind from, Kind to, float v) noexcept;
    [[nodiscard]] float getAffinity(Kind from, Kind to) const noexcept;

    // ---- sleep / wake / events (FR-070, FR-071) ---------------------------
    void setAgentWake(std::size_t i, float amount) noexcept;
    void setAgentDormant(std::size_t i, bool dormant) noexcept;
    void perturbAgent(std::size_t i, float amount) noexcept;
    [[nodiscard]] float getAgentWake(std::size_t i) const noexcept;
    [[nodiscard]] bool  isAgentDormant(std::size_t i) const noexcept;

    // ---- the output surface (FR-060, FR-061) -----------------------------
    [[nodiscard]] float       getAgentOutput(std::size_t i) const noexcept;   ///< [0,1]
    [[nodiscard]] double      getAgentEnergy(std::size_t i) const noexcept;
    [[nodiscard]] Kind        getAgentKind(std::size_t i) const noexcept;
    [[nodiscard]] double      getAgentPositionX(std::size_t i) const noexcept;
    [[nodiscard]] double      getAgentPositionY(std::size_t i) const noexcept;
    [[nodiscard]] double      getAgentPhase(std::size_t i) const noexcept;
    [[nodiscard]] std::size_t getAgentCount() const noexcept;
    [[nodiscard]] std::size_t getAgentCountOfKind(Kind kind) const noexcept;

    // ---- prepare-time read-back (FR-060, Clarification Q8) ----------------
    [[nodiscard]] double      getEnergyBudget() const noexcept;
    [[nodiscard]] std::size_t getResourceCells() const noexcept;
    [[nodiscard]] std::size_t getStepIntervalChunks() const noexcept;
    [[nodiscard]] double      getStepDurationSeconds() const noexcept;
    [[nodiscard]] double      getInitialPoolFraction() const noexcept;
    [[nodiscard]] double      getSampleRate() const noexcept;
    [[nodiscard]] bool        isPrepared() const noexcept;
    [[nodiscard]] std::uint32_t getSeed() const noexcept;

    // ---- diagnostics (FR-065, FR-066) ------------------------------------
    [[nodiscard]] double        getPoolEnergy() const noexcept;
    [[nodiscard]] double        getCellEnergy(std::size_t k) const noexcept;
    [[nodiscard]] double        getTotalEnergy() const noexcept;
    [[nodiscard]] std::size_t   getPairInteractionCount() const noexcept;
    [[nodiscard]] std::uint64_t getConservationViolationCount() const noexcept;
    [[nodiscard]] std::uint64_t getNonFiniteContainmentCount() const noexcept;
    [[nodiscard]] std::uint64_t getOutputClampEngagementCount() const noexcept;
    [[nodiscard]] float         getAgentClampedStepFraction(std::size_t i) const noexcept;
    [[nodiscard]] std::uint64_t getControlStepCount() const noexcept;   ///< plan addition A-3
    [[nodiscard]] double        getEnergyEntropy() const noexcept;      ///< FR-066, NEVER gated
    [[nodiscard]] std::size_t   getAllocatedBytes() const noexcept;     ///< always 0
};
```

Notes on shape:

* **Share-unit setters are named `…Shares`** (`setPreyFloorShares`, `setCapacityShares`,
  `setCellCapacityShares`, `setSatiationShares`). FR-008 makes the unit part of the contract, and a
  bare `setCapacity(float)` would read as absolute energy to every caller. Plan addition A-4;
  recorded in S14 because the spec names the knobs `preyFloor` / `capacity` / `cellCapacity` /
  `satiation`.
* **`setFreqRangeHz(lo, hi)`** is one setter for two knobs because the pair must stay ordered
  (`freqLo < freqHi`) or FR-013's hard clamp inverts. A caller that sets them independently can
  transiently invert the range; the paired setter makes that unreachable. It clamps each to its
  Appendix-A range, then enforces `hi >= lo + 1e-4`. Two getters, as FR-064 requires.
* **`getControlStepCount()` (plan addition A-3)** is the denominator
  `getAgentClampedStepFraction()` divides by and the quantity SC-010 (b) asserts to within one step
  ("the number of simulation steps is within one step of `duration · sampleRate /
  (stepIntervalChunks · 64)`"). Without it SC-010 (b) has nothing to read.
* **`setAffinity(Kind from, Kind to, float v)`** carries FR-031's clamp, which is not optional and
  is not implied by any Appendix-A row: `if (from or to is out of range) return;` (silent no-op,
  FR-064 — reachable only by a cast, since `Kind` is a scoped enum);
  `if (!detail::isFinite(v)) return;` (FR-064's rejection); then
  `affinity_[size_t(from)][size_t(to)] = std::clamp(v, kMinAffinity, kMaxAffinity);`. The matrix is
  **not** symmetrised — FR-031 is per entry, and the pair pass reads `affinity_[kind_i][kind_j]`
  for the `i`-side force only (S4.2), so `setAffinity(a, b, …)` and `setAffinity(b, a, …)` are
  genuinely two knobs. `getAffinity` reports the clamped stored value, gated by
  `EcosystemEngine_SettersClampToRange` (S10.4).
* `getAgentEnergy`, `getPoolEnergy`, `getCellEnergy`, `getTotalEnergy` return `double` — FR-084's
  economy is `double` and SC-001 (c)'s 1e-9 relative gate is unmeasurable through a `float` getter.
* Out-of-range indices return the documented neutral (`0.0f` / `0.0` / `0` / `Kind::Partial`) and
  read nothing (FR-060, FR-064). `getAffinity` with an out-of-range `Kind` (only reachable by a cast)
  returns `0.0f`. **Knob getters have no neutral** — they always report the current configuration.
  S8 splits the three cases and is the authority.
* **Doxygen on the read surface (FR-060, required text).** The block comment above the output
  surface must name the roadmap's intended consumers *as use, not as coupling*, in one sentence —
  the FR-060 closing clause. The header text to copy: *"Roadmap lines 386–388 describe the intended
  use of this surface — a partial agent feeding a harmonic cloud partial's amplitude, a resonator
  agent a drift-network peak's wake, a feedback agent an ecology loop's coupling. Those are uses a
  Phase-10 host composes; this component includes nothing of them, names no consumer type, and is
  complete without any of them."*

### S1.5 Private state layout — structure-of-arrays, exact members

```cpp
private:
    // ---- configuration, fixed at prepare() (FR-006) -----------------------
    bool        prepared_        = false;
    double      sampleRate_      = kDefaultSampleRate;
    std::size_t agentCount_      = 32;
    std::size_t resourceCells_   = 64;
    std::size_t stepChunks_      = kDefaultStepIntervalChunks;
    double      energyBudget_    = 1.0;
    double      initialPoolFrac_ = 0.5;
    std::uint32_t seed_          = 0xC0FFEEu;   // the prototype's reference seed

    double      dt_        = 0.0;   // stepChunks_*64 / sampleRate_   (FR-082)
    double      sqrtDt_    = 0.0;   // precomputed for FR-013's OU term
    std::size_t rampSteps_ = 1;     // max(1, ceil(0.050 / dt_))      (FR-070)

    // ---- rule knobs, runtime (FR-064). Defaults = Appendix A. ------------
    // value                          default   Appendix-A range
    float kernelSigma_      = 0.03f;   // [0.01, 0.35]
    float exchangeRate_     = 0.35f;   // [0, 3.0]
    float predation_        = 0.55f;   // [0, 1]
    float preyFloorShares_  = 0.5f;    // [0, 1.6]     shares
    float capacityShares_   = 32.0f;   // [0.32, 32]   shares
    float leakRate_         = 0.06f;   // [0, 1.0]
    float leakExponent_     = 1.0f;    // [1.0, 2.5]; Phase-10 macros <= 1.3 (FR-052)
    float moveRate_         = 0.20f;   // [0, 0.5]
    float maxSpeed_         = 0.03f;   // [0.001, 0.05]
    float forageRate_       = 0.010f;  // [0, 0.05]
    float crowding_         = 0.05f;   // [0, 0.2]
    float crowdingRadius_   = 0.02f;   // [0.005, 0.05]
    float syncRate_         = 0.0f;    // [0, 0.5]     OFF by default (FR-035)
    float cellCapShares_    = 3.2f;    // [0.32, 12.8] cell-shares
    float regenRate_        = 0.05f;   // [0, 1.0]
    float grazeRate_        = 0.75f;   // [0, 3.0]
    float feedRate_         = 0.0f;    // [0, 1.0]     OFF by default (FR-057)
    float satiationShares_  = 0.0f;    // [0, 16]      0 = off
    float appetiteDepth_    = 0.8f;    // [0, 1]
    float freqLoHz_         = 0.0015f; // [0.0005, 0.005]
    float freqHiHz_         = 0.0180f; // [0.006, 0.05]
    float freqDrift_        = 0.00004f;// [0, 0.0002]
    std::array<std::array<float, kNumKinds>, kNumKinds> affinity_{};
                                       // [kMinAffinity, kMaxAffinity] = [-2, +2] (FR-031);
                                       // member-initialised -1.0 diagonal / +0.45 off-diagonal

    // ---- FR-008 absolute conversions, re-derived on prepare() AND on each
    //      share-unit setter (S5) -----------------------------------------
    double preyFloorAbs_ = 0.0;   // preyFloorShares_ * energyBudget_/agentCount_
    double capacityAbs_  = 0.0;   // capacityShares_  * energyBudget_/agentCount_
    double satiationAbs_ = 0.0;   // satiationShares_ * energyBudget_/agentCount_   (0 = off)
    double cellCapAbs_   = 0.0;   // cellCapShares_   * energyBudget_/resourceCells_
    double meanShare_    = 0.0;   // energyBudget_ / agentCount_   (FR-061, FR-083)

    // ---- kernel derivatives, recomputed when kernelSigma_ changes ---------
    double twoSigmaSq_   = 0.0;   // 2*sigma^2
    double invTwoSigmaSq_= 0.0;
    double sigmaSq_      = 0.0;   // sigma^2, the FR-033 gradient divisor
    double cutDistSq_    = 0.0;   // S4.0's exp-free pre-test bound

    // ---- agent state (SoA), all kMaxAgents-sized -------------------------
    std::array<Kind,   kMaxAgents> kind_{};
    std::array<double, kMaxAgents> energy_{};
    std::array<double, kMaxAgents> x_{}, y_{}, phase_{}, freq_{}, freq0_{};
    // FR-083's repair targets: the values prepare() produced for this index.
    std::array<double, kMaxAgents> x0_{}, y0_{}, phase0_{};
    std::array<float,  kMaxAgents> wake_{};      // [0,1]
    std::array<bool,   kMaxAgents> dormant_{};
    std::array<float,  kMaxAgents> gate_{};      // the FR-070 ramp's current value
    std::array<float,  kMaxAgents> output_{};    // FR-062's held publication
    std::array<std::uint64_t, kMaxAgents> clampedSteps_{};   // FR-061 upper rail, per agent
    std::array<std::size_t, kNumKinds> kindCount_{};         // FR-060

    // ---- resource field + pool -------------------------------------------
    std::array<double, kMaxResourceCells> res_{}, cellPos_{};
    double pool_ = 0.0;

    // ---- per-step scratch (members, NOT locals: FR-003) ------------------
    std::array<double, kMaxAgents> dE_{}, fx_{}, fy_{}, dPhase_{};
    std::array<double, kMaxAgents> outflow_{}, scale_{}, appetite_{}, graze_{}, forage_{};
    std::array<double, kMaxAgents> cellDemand_{};              // per-cell, reset by touch list
    std::array<bool,   kMaxAgents> divided_{};                 // SC-020 per-agent (A-10, S7.4)
    std::array<std::uint8_t, kMaxAgents> cellTouched_{};       // indices with demand this cell
    std::array<std::uint8_t, kMaxPairs> pairI_{}, pairJ_{};
    std::array<double,        kMaxPairs> pairFlow_{};
    std::size_t pairCount_ = 0;

    // ---- clock -----------------------------------------------------------
    std::size_t   samplePhase_ = 0;   // 0..63          (FR-081 absolute residue)
    std::size_t   chunkPhase_  = 0;   // 0..stepChunks_-1
    std::uint64_t stepCount_   = 0;

    // ---- counters (FR-065). Cleared by prepare/reset/setSeed. -------------
    std::uint64_t conservationViolations_ = 0;
    std::uint64_t nonFiniteContainments_  = 0;
    std::uint64_t outputClampEngagements_ = 0;
    // probe-only, no public getter (S7.4, S10.4)
    std::uint64_t exchangeDivisions_      = 0;
    double        lastDenormalSnap_       = 0.0;

    // ---- RNG: ONE persistent stream (FR-080 salt 5) ----------------------
    Xorshift32 driftRng_{1u};

    friend struct detail::EcosystemEngineNonFiniteProbe;
    friend struct detail::EcosystemEngineInspectProbe;
```

**Why SoA and not `std::array<Agent, 48>`** (deviating from the spec's new-components table, S14
D-A): the pair loop reads `x_`, `y_`, `energy_`, `kind_`, `phase_` of two agents at a stride of 1;
the cell loop reads `x_`, `energy_`, `appetite_` of every agent 96 times per step. SoA keeps each of
those scans in one cache line per 8 agents instead of touching a 100-byte struct. The cell loop is
the dominant cost (S12), so this is not premature — it is the layout the budget depends on. `Agent`
as a named type would then exist only to be decomposed, so it is not introduced at all; the
component adds exactly one namespace-scope name, which is what the ODR sweep promised.

**Footprint: S9's itemised table is the single authority, and it totals ≈ 21.5 KB.** The member list
above declares **19** agent-indexed `double` arrays — 9 persistent state (`energy_ x_ y_ phase_
freq_ freq0_ x0_ y0_ phase0_`) plus 10 per-step scratch (`dE_ fx_ fy_ dPhase_ outflow_ scale_
appetite_ graze_ forage_ cellDemand_`) — and S9 counts them as 9 + 10. No second total is stated
here on purpose: R-9's "too large for a casual stack local" guidance and SC-007's
construct-outside-the-`AllocationScope` instruction are both written against S9's number, and two
footprint figures in one document is how a later reader "reconciles" them by shrinking the object.
No heap term anywhere; `getAllocatedBytes()` returns `0u` unconditionally
(`resonance_drift_network.h:912` idiom).

### S1.6 RNG architecture — the salt table and the `double` unipolar helper

```cpp
// FR-080 salt table. APPEND ONLY. Renumbering silently changes every trajectory.
// Salts 0-6 are the prototype's seven streams (ecosystem-sim.js:171-177, :219).
static constexpr std::size_t kSaltPositions   = 0;
static constexpr std::size_t kSaltEnergies    = 1;
static constexpr std::size_t kSaltFrequencies = 2;
static constexpr std::size_t kSaltPhases      = 3;
static constexpr std::size_t kSaltKinds       = 4;   // deal-remainder AND shuffle (S2.1)
static constexpr std::size_t kSaltDrift       = 5;   // the ONE persistent stream
static constexpr std::size_t kSaltResourceFill= 6;
static constexpr std::size_t kSaltNextFree    = 7;
static_assert(kSaltPositions   < kSaltEnergies   && kSaltEnergies    < kSaltFrequencies &&
              kSaltFrequencies < kSaltPhases     && kSaltPhases      < kSaltKinds       &&
              kSaltKinds       < kSaltDrift      && kSaltDrift       < kSaltResourceFill &&
              kSaltResourceFill< kSaltNextFree,
              "salt table must be strictly increasing - two lanes sharing a salt share a stream");
```

Six of the seven streams are **local `Xorshift32` objects constructed inside `initialiseState()`**
and destroyed at its end; only the drift stream persists as a member, because it is drawn every
step. That matches the prototype exactly (`:171-177` are `const` locals; `:177` assigns
`this.rDrift`).

**The `double` unipolar helper (plan addition A-5, and it is load-bearing).** `Xorshift32::
nextUnipolar()` returns `static_cast<float>(next()) * kToFloat` with a **`float`** `kToFloat`
(`random.h:67`, `:88`), while the prototype computes `next() * kToFloat` in JavaScript **doubles**
(`ecosystem-sim.js:44`). Using the shipped `float` accessor for positions and energies would
introduce a ~1e-7 relative difference at t = 0 and make every prototype figure incomparable for a
reason that has nothing to do with the rules. The component therefore carries two private statics:

```cpp
static constexpr double kToDouble = 1.0 / 4294967295.0;   // == JS kToFloat, exactly
[[nodiscard]] static double nextUnipolarD(Xorshift32& r) noexcept {
    return static_cast<double>(r.next()) * kToDouble;      // (0, 1]
}
[[nodiscard]] static double nextBipolarD(Xorshift32& r) noexcept {
    return static_cast<double>(r.next()) * kToDouble * 2.0 - 1.0;   // (-1, 1]
}
[[nodiscard]] static double rangeD(Xorshift32& r, double lo, double hi) noexcept {
    return lo + (hi - lo) * nextUnipolarD(r);              // ecosystem-sim.js:47
}
```

`random.h` is **not** modified (FR-090): these are local helpers over the shipped `next()`. Note the
range is **(0, 1]**, not `[0, 1)`, because `next()` returns `[1, 2^32-1]` (`random.h:51-55`) — the
same as the prototype, so the initial draws match.

---

## S2. `prepare()`, `reset()`, `setSeed()`

### S2.1 `prepare(double sampleRate, const PrepareConfig& config)` — numbered order

1. `sampleRate_ = std::max(kMinUsableSampleRate, sanitise(sampleRate, kDefaultSampleRate))`, where
   `sanitise(v, neutral) = detail::isFinite(v) ? v : neutral` — the sanitise-then-floor form used
   verbatim at `bloom_engine.h:385`, with `sanitise()`'s two overloads at `bloom_engine.h:879-882`.
   A non-finite rate is substituted, then floored — FR-005's "clamped, and the getter reports the
   clamp". **Both halves are gated**: SC-009 (c) drives non-finite *and* sub-floor rates (0.0,
   −48000.0, 1.0, 7999.0) and asserts `getSampleRate() == kMinUsableSampleRate` for the latter,
   because `dt_`, `sqrtDt_` and `rampSteps_` all derive from this one number.
2. `agentCount_   = std::clamp(config.agentCount, kMinAgents, kMaxAgents)`.
   `resourceCells_ = std::clamp(config.resourceCells, std::size_t{1}, kMaxResourceCells)`.
   `stepChunks_    = std::clamp(config.stepIntervalChunks, kMinStepIntervalChunks,
                                kMaxStepIntervalChunks)`.
   `energyBudget_  = std::clamp(sanitise(config.energyBudget, 1.0), kMinEnergyBudget,
                                kMaxEnergyBudget)`.
   `initialPoolFrac_ = std::clamp(sanitise(config.initialPoolFraction, 0.5), 0.1, 0.9)`.
   The `energyBudget_` floor is the one clamp that is not cosmetic: it is FR-061's divisor and
   SC-001 (c)'s reference. `agentCount == 0` and `SIZE_MAX` both land inside `[1, 48]` here, which is
   what SC-009 (c) drives.
3. `dt_ = double(stepChunks_ * kControlChunkSamples) / sampleRate_`; `sqrtDt_ = std::sqrt(dt_)`;
   `rampSteps_ = std::max<std::size_t>(1, std::size_t(std::ceil(0.050 / dt_)))`.
4. `meanShare_ = energyBudget_ / double(agentCount_)`; run the FR-008 conversion (S5).
5. Recompute the kernel derivatives (S4.0).
6. Clear every counter (`conservationViolations_`, `nonFiniteContainments_`,
   `outputClampEngagements_`, `clampedSteps_[]`, `exchangeDivisions_`, `stepCount_`) —
   Clarification Q8.
7. `samplePhase_ = 0; chunkPhase_ = 0;`
8. `affinity_[i][j] = (i == j) ? -1.0f : 0.45f` **only if this is the first prepare** — no. The
   affinity matrix is a *runtime knob* (FR-064) and must survive `prepare()` the way every other
   rule knob does. It is initialised by its member initialiser at construction (a `constexpr`
   helper that fills the same −1.0 / +0.45 pattern, `ecosystem-sim.js:154-167`) and `prepare()`
   leaves it alone. Same for every other rule knob: **`prepare()` re-derives state, never
   configuration** (the `bloom_engine.h:386-388` rule, "`seed_` and every configuration scalar
   survive").
9. `initialiseState()` (S2.2).
10. `prepared_ = true;` then **publish once** (S4.11) with the gates *snapped* to their steady
    targets rather than ramped — the `bloom_engine.h:399-400` rule, so a caller that reads
    `getAgentOutput` before the first `processChunk` sees the initial state and not a 50 ms window
    at t = 0.

`prepare()` is allocation-free in fact, not merely by promise: every member is a fixed `std::array`.

### S2.2 `initialiseState()` — FR-041's three-way partition, verbatim

Transcribed from `ecosystem-sim.js:179-232`. Streams are constructed in salt order; the draw order
within the agent loop is normative because it fixes each stream's position.

```
rPos(seed,0)  rEnergy(seed,1)  rFreq(seed,2)  rPhase(seed,3)  rKind(seed,4)
driftRng_.seed(deriveStreamSeed(seed,5))            // persistent
rRes(seed,6)

// (a) KIND: stratified (FR-011, Clarification Q6) - NOT the prototype's i.i.d. draw
base = agentCount_ / kNumKinds;  rem = agentCount_ % kNumKinds
fill kind_[0..] with `base` of each kind in Kind order
for (r = 0; r < rem; ++r)  kind_[base*kNumKinds + r] = Kind(floor(nextUnipolarD(rKind)*kNumKinds))
                                                        clamped to kNumKinds-1
Fisher-Yates shuffle over [0, agentCount_) using rKind:
    for (i = agentCount_-1; i > 0; --i) { j = size_t(nextUnipolarD(rKind) * double(i+1));
                                          if (j > i) j = i; swap(kind_[i], kind_[j]); }
recount kindCount_[]

// (b) per-agent draws, in this order per agent i (matching :192-206)
x_[i]     = nextUnipolarD(rPos)          x0_[i] = x_[i]
y_[i]     = nextUnipolarD(rPos)          y0_[i] = y_[i]        // 2-D always (FR-012)
energy_[i]= nextUnipolarD(rEnergy)                             // raw, normalised below
phase_[i] = nextUnipolarD(rPhase)        phase0_[i] = phase_[i]
freq_[i]  = rangeD(rFreq, freqLoHz_, freqHiHz_)   freq0_[i] = freq_[i]

// (c) normalise agent energies to (1 - initialPoolFrac_) * energyBudget_   (:207-211)
target = energyBudget_ * (1 - initialPoolFrac_)
eSum   = sum(energy_);  s = eSum > 0 ? target/eSum : 0;  energy_[i] *= s

// (d) SEEDED resource fill - cells are NEVER left empty (:219-232)
for k in [0, resourceCells_):
    cellPos_[k] = (k + 0.5) / resourceCells_          // FR-040's strip geometry, x only
    res_[k]     = nextUnipolarD(rRes) * cellCapAbs_
resSum    = sum(res_)
remaining = energyBudget_ - target
resTarget = min(resSum, remaining * 0.5)
rs        = resSum > 0 ? resTarget/resSum : 0;  res_[k] *= rs
pool_     = remaining - resTarget

// (e) gates and publication
gate_[i]  = dormant_[i] ? 0.0f : wake_[i]        // SNAP, no ramp at t=0
publish()                                         // S4.11
```

Two properties this order buys, both asserted: the sum
`pool_ + Σ energy_ + Σ res_ == energyBudget_` holds to `double` rounding at step 0 (SC-001 (c)
measures from here), and the shuffle in (a) decorrelates kind from agent index so the affinity
matrix does not see a block structure.

**Stratification vs the prototype.** `ecosystem-sim.js:192` draws kind i.i.d.; FR-011 deals then
shuffles. The shuffle consumes `rKind` *after* the remainder draws, so both consumers share one
stream in a fixed order (no new salt). Consequence, already flagged by the spec: SC-002's defaults
figures were measured against the unconstrained histogram and **must be re-measured** (S10.4).

### S2.3 `reset()`

`reset()` re-runs S2.2 with the current `seed_` and the current configuration, clears every counter
and the clock, and publishes. It does **not** re-read a `PrepareConfig` and does not touch any rule
knob. FR-005: "`reset()` returns to exactly the state `prepare()` produced for the current seed" —
SC-006 (c) asserts bit-identity of every agent double against a `prepare()`-fresh instance.

### S2.4 `setSeed(std::uint32_t)`

`seed_ = seed;` then exactly `reset()`. The Edge Cases section requires this be documented as
"re-deriving the initial state (a re-`prepare()` in effect), not re-seeding lanes in place" — the
header says so in one sentence. `setSeed(0)` is legal and exercises both zero substitutions
(`Xorshift32::seed()` at `random.h:72-74`, `deriveStreamSeed`'s `0x2545F491` at `random.h:112`).

---

## S3. `processChunk()` and the control clock (FR-081, FR-082)

```cpp
void processChunk(std::size_t numSamples) noexcept {
    if (!prepared_ || numSamples == 0) return;   // FR-007, FR-081: 0 draws no RNG, advances nothing
    std::size_t remaining = numSamples;
    while (remaining > 0) {
        const std::size_t take = std::min(remaining, kControlChunkSamples - samplePhase_);
        samplePhase_ += take;
        remaining    -= take;
        if (samplePhase_ == kControlChunkSamples) {
            samplePhase_ = 0;
            if (++chunkPhase_ >= stepChunks_) { chunkPhase_ = 0; simulationStep(); }
        }
    }
}
```

Both residues (`samplePhase_` within 64, `chunkPhase_` within `stepChunks_`) live **across calls**.
The number of steps after N total advanced samples is therefore
`floor((N + phase0) / (stepChunks_ * 64))` — a function of N alone, never of how N was partitioned.
The header restates the named failure mode verbatim from `bloom_engine.h:899`
(`numSamples / kControlChunkSamples` per call, residue discarded at the call boundary), because that
is the natural mistake and SC-008 is what catches it.

A very large `numSamples` (the Edge Cases' 1 000 000) costs `numSamples/64` trivial loop iterations
with no recursion and no stack growth; `samplePhase_`/`chunkPhase_` are `std::size_t` residues and
cannot overflow. `stepCount_` is `std::uint64_t`: at 93.75 Hz it wraps after 6 × 10^12 years.

---

## S4. `simulationStep()` — FR-087's thirteen stages, in order

The order is **normative** (FR-087, Clarification Q2). Every stage below names the prototype lines it
transcribes. A reordering is a spec amendment, not an implementation detail.

### S4.0 Step preamble: kernel derivatives and the exp-free pre-test

Recomputed whenever `kernelSigma_` changes (in `setKernelSigma` and in `prepare`), not per step:

```
sigmaSq_       = sigma^2
twoSigmaSq_    = 2*sigma^2
invTwoSigmaSq_ = 1/twoSigmaSq_
cutDistSq_     = twoSigmaSq_ * 13.815510557964274 * (1 + 1e-9)
```

`13.815510557964274 == -ln(1e-6)`. The habitat separation `d` is computed with the prototype's
`wrapDelta` (`:240-246`), on each axis independently:

```cpp
[[nodiscard]] static double wrapDelta(double d) noexcept {
    if (d >  0.5) return d - 1.0;
    if (d < -0.5) return d + 1.0;
    return d;
}
```

**The two-stage cutoff (plan addition A-6; the single largest cost lever, S12).** FR-012's cutoff is
normative as `w < 1e-6 → skip entirely`. `std::exp` is the dominant per-step cost (S0: the repo has
no `fastExp` — `fast_math.h:13-15` records that MSVC's `std::exp` beat the removed one), and at the
defaults ~93 % of pairs and ~68 % of cell visits are skipped. So every kernel site is written:

```cpp
const double d2 = dx*dx + dy*dy;
if (d2 > cutDistSq_) continue;                        // exp-free pre-test
const double w = std::exp(-d2 * invTwoSigmaSq_);
if (w < kNeighbourWeightCutoff) continue;             // FR-012, kept VERBATIM
```

The pre-test is monotone-equivalent to the normative test and the `(1 + 1e-9)` inflation makes it
strictly conservative: a pair it drops has `exp(-d²/2σ²) < 1e-6` by more than any plausible `exp`
rounding error, so no pair with `w >= 1e-6` is ever lost. The normative comparison is still executed
on every survivor, so the *set* of interacting pairs is exactly the prototype's.

### S4.1 Stage 1 — non-finite guard and repair (FR-083)

See S7. Runs **before** anything reads state.

### S4.2 Stage 2 — one pair pass, recording only (`ecosystem-sim.js:266-321`)

`pairCount_ = 0`; `dE_`, `fx_`, `fy_`, `dPhase_`, `outflow_` zeroed over `[0, agentCount_)`.

```
for i in [0, n):  for j in (i, n):
    dx = wrapDelta(x_[j] - x_[i]);  dy = wrapDelta(y_[j] - y_[i])
    d2 = dx*dx + dy*dy;  two-stage cutoff (S4.0)
    ++pairInteractions (the getPairInteractionCount() figure)

    // RULE 1 - exchange, RECORDED not applied (:285)
    // FR-021: (1 - 2*predation) is EXACTLY ZERO at predation == 0.5, so the whole
    // exchange rule is silently OFF there. Keep this note on the line.
    flow = exchangeRate_ * w * (energy_[j] - energy_[i]) * (1 - 2*predation_)
    pairI_[p]=i; pairJ_[p]=j; pairFlow_[p]=flow; ++pairCount_
    if (flow > 0) outflow_[j] += flow; else outflow_[i] -= flow;

    // RULE 2 - affinity, scaled by the NEIGHBOUR's energy (:292-300)
    a    = affinity_[kind_[i]][kind_[j]]
    dist = sqrt(d2) + 1e-9;  ux = dx/dist;  uy = dy/dist
    fx_[i] += a*w*energy_[j]*ux;   fy_[i] += a*w*energy_[j]*uy
    fx_[j] -= a*w*energy_[i]*ux;   fy_[j] -= a*w*energy_[i]*uy

    // CROWDING - kind- and energy-independent (:307-315)
    if (crowding_ > 0 && dist < crowdingRadius_):
        push = crowding_ * (1 - dist/crowdingRadius_)
        fx_[i] -= push*ux; fy_[i] -= push*uy; fx_[j] += push*ux; fy_[j] += push*uy

    // RULE 3 - Kuramoto, OFF by default (:317-322)
    if (syncRate_ > 0):                                  // plan addition A-7
        s = sin(2*pi*(phase_[j] - phase_[i]))
        dPhase_[i] += syncRate_*w*s;  dPhase_[j] -= syncRate_*w*s
```

**FR-021's required header note — the second of its two defences, and the one this plan previously
omitted.** SC-012's coverage assertion is the first; the header text is the second, and without it
the trap that already cost the prototype an ablation run reported as *"bit-identical to baseline, to
every printed digit"* (`FINDINGS.md:119-122`, spec FR-021) is reproduced with half its guard. The
sentence to copy into the header, immediately above the `flow` line:

> **`predation == 0.5f` disables this rule exactly.** The factor `(1 − 2·predation)` is zero there,
> so every recorded flow is `0.0` and the exchange rule is silently off while every other stage runs
> normally. This is not hypothetical: an ablation run in the prototype reported "no exchange" as
> **bit-identical to baseline, to every printed digit**, because the default at the time *was* 0.5
> and the rule had been off in the very configuration being validated (`FINDINGS.md:119-122`).

Made observable, not merely documented: `EcosystemEngine_ExchangeDisabledAtPredationHalf` (S10.4)
runs the defaults at `predation = 0.5` and asserts through `EcosystemEngineInspectProbe` that every
recorded `pairFlow(p)` is **exactly** `0.0` while `pairCount() > 0` — i.e. pairs still survive the
`w < 1e-6` cutoff, so the zero is the factor and not an empty pair list.

**FR-035's required header sentence.** The spec states it as header content, so it is prescribed
here rather than left to be re-derived: *"synchronize is a macro, not a default rule."* It sits on
the `syncRate_` declaration (S1.5) and above the Kuramoto block, with the measurement behind it —
removing sync at σ = 0.03 nearly doubled activity in 1-D (+86 %) and halved correlation, and
re-adding it at the final 2-D defaults costs 18 % activity (`FINDINGS.md:228-233`). It is retained
at a default of `0.0f` so a Phase-10 *coherence* macro can dial it in deliberately.

**Plan addition A-7:** the prototype evaluates `Math.sin` unconditionally and multiplies by
`syncRate = 0`. Guarding the `sin` on `syncRate_ > 0` is exactly equivalent (the product is 0 either
way, and the guard ladder of S7 makes a NaN `phase_` unreachable at this point) and removes 34 `sin`
calls per step at the defaults — one of the few free wins available. It does **not** help SC-011's
worst case, where sync is on by construction.

`kMaxPairs = 1128 == 48*47/2`, so `pairCount_` can never exceed the arrays; the `static_assert` in
S1.2 is what keeps that true if `kMaxAgents` ever moves.

### S4.3 Stage 3 — exchange pass two (FR-023, `ecosystem-sim.js:325-347`)

```
divided_ zeroed over [0, agentCount_)        // SC-020's per-agent record (A-10)
for i in [0, n):
    want  = outflow_[i] * dt_
    spare = energy_[i] - preyFloorAbs_
    scale_[i] = (want > 0.0 && want > spare) ? (spare > 0.0 ? (++exchangeDivisions_,
                                                               divided_[i] = true, spare/want)
                                                            : 0.0)
                                             : 1.0
for p in [0, pairCount_):
    i = pairI_[p]; j = pairJ_[p]
    s = min(scale_[i], scale_[j])
    f = pairFlow_[p] * s
    dE_[i] += f;  dE_[j] -= f
```

**Write the ternary exactly as above.** The spec (FR-023) states both guards and forbids the
`min(1, spare/want)` paraphrase by name, and both failure modes are reachable at the defaults, not
exotic:

* `want == 0` is the *common* case at `kernelSigma = 0.03` (≈ 34 surviving pairs out of 496,
  `FINDINGS.md:254`) and the *only* case at `kMinAgents = 1`. The outer `want > 0` test is what
  stops `0/0` (NaN) and `spare/0` (±Inf).
* `spare < 0` is routine in the hostile box (`preyFloor` up to 1.8 shares against a mean share of
  1.0), and `min(1, negative)` would produce a **negative** scale — every flow in that pair applied
  with reversed sign and arbitrary magnitude, still antisymmetric and therefore **invisible to
  SC-001**. SC-020 is the criterion that sees it, and `divided_[i]` (set only on the division
  branch, beside the `exchangeDivisions_` increment) is what lets the probe assert **per agent**
  that "no division is performed when `want_i == 0`" — the cumulative counter alone cannot, because
  other agents divide on the same step (S7.4, A-10).

Scaling both sides of a pair by the same `min(scale_i, scale_j)` is what preserves antisymmetry: the
pairwise sum is exactly zero in `double`, so **exchange never touches the pool or the field**
(FR-024).

### S4.4 Stage 4 — appetite gate (FR-050, `:352-360`)

```
appetiteSum = 0
for i: g = 1 + appetiteDepth_ * sin(2*pi*phase_[i]);  appetite_[i] = g > 0 ? g : 0;  appetiteSum += …
```
Read from the phase at the **start** of the step (phase integrates at stage 11). This is the
load-bearing rule: removing it costs −96 % activity and freezes 26 of 32 agents
(`FINDINGS.md:264`).

### S4.5 Stage 5 — proportional regrowth, pool → cells (FR-053, FR-054, `:362-400`)

```
poolDelta = 0
// THE single withdrawal balance (Guard 1, FR-054). The floor is OUTSIDE the
// branch on purpose - see "Why the floor is structural" below. A negative pool
// (FR-056 permits one and never clamps it) must present as ZERO available, not
// as a negative budget that the stage-7 feed can then hand to one agent.
avail     = (pool_ > 0.0) ? pool_ : 0.0
if (avail > 0):
    want = 0
    for k: room = cellCapAbs_ - res_[k]; if (room > 0) want += regenRate_ * room * dt_
    share = (want > avail) ? avail/want : 1.0
    for k: room = cellCapAbs_ - res_[k]; if (room <= 0) continue
           give = regenRate_ * room * dt_ * share
           if (give <= 0) continue
           res_[k] += give;  poolDelta -= give;  avail -= give
    if (avail < 0) avail = 0
```

Two forbidden-by-name shapes, both recorded in the header: **index-order regrowth** (cells 0–19 take
every joule at steady state and 70 % of the habitat becomes a permanent desert,
`FINDINGS.md:192-200`) and **two independent caps against the start-of-step pool** (591 of 1000
fuzzed configurations drove the pool negative, `FINDINGS.md:30-34`). `avail` is the one running
balance for regrowth *and* the FR-057 global feed, and nothing else may withdraw.

**Why the floor is structural, not a line inside the branch.** FR-054 requires the running balance
to be "never allowed below zero", and that promise has **two** withdrawal sites: this stage and the
FR-057 global feed at stage 7. Initialising `avail = pool_` and clamping only *after* the regrowth
loop leaves the clamp unreachable exactly when it matters — when `pool_ < 0.0` the `if (avail > 0)`
branch is skipped entirely and a negative `avail` walks into stage 7. What happens there is not a
rounding artefact but a full transfer, **and it fires at the default `feedRate_ = 0`**: stage 7's
`if (globalFeed > avail) globalFeed = avail` is *intended* as `min(globalFeed, avail)`, but with
`avail < 0` it instead **forces** `globalFeed = avail` — at `feedRate_ = 0` the product is
`0.0 * negative = -0.0`, and `-0.0 > avail` is true. Agent 0 (the first loop iteration) is then
charged `influx = graze_[0] + avail`, i.e. the entire pool deficit in a single step; `avail -=
globalFeed` zeroes the balance so no other agent is touched; and stage 8's zero clamp returns the
overdraw to the pool. Net effect: the deficit silently migrates onto one agent and back, wiping that
agent's energy to 0 with **no counter recording it**, and FR-056's contract that a negative pool is
a *readable, latched* signal is undermined — the pool is quietly healed toward zero by destroying
agent 0's state.

Hoisting the floor makes FR-054's non-negativity structural for **both** withdrawal sites; the
post-regrowth `if (avail < 0) avail = 0` stays as the rounding backstop. Gated by SC-009 (d)
(S10.4), which probe-injects a negative `pool_`, steps once at the defaults, and asserts that no
agent absorbed the deficit.

Regrowth runs **before** grazing so a grazed cell cannot be refilled in the step it was grazed
(FR-087 stage 5, explicit).

### S4.6 Stage 6 — demand-scaled grazing and the foraging gradient (FR-051, FR-033, `:402-451`)

`graze_` and `forage_` zeroed. For each cell `k` with `res_[k] > 0`:

```
touchCount = 0;  demandSum = 0
for i in [0, n):
    d  = wrapDelta(cellPos_[k] - x_[i])        // FR-040: x separation ONLY (strip field)
    d2 = d*d;  two-stage cutoff (S4.0)
    hunger = (satiationAbs_ > 0) ? max(0, 1 - energy_[i]/satiationAbs_) : 1.0
    cellDemand_[i] = w * appetite_[i] * hunger
    cellTouched_[touchCount++] = uint8(i)
    demandSum += cellDemand_[i]
    if (forageRate_ > 0) forage_[i] += (res_[k]/cellCapAbs_) * w * (d/sigmaSq_)
if (demandSum <= 0) continue
perUnitDemand = grazeRate_ * res_[k] * dt_
ask           = perUnitDemand * demandSum
cap           = (ask > res_[k]) ? res_[k]/ask : 1.0
taken = 0
for t in [0, touchCount): i = cellTouched_[t]
    g = perUnitDemand * cellDemand_[i] * cap;  graze_[i] += g;  taken += g
res_[k] -= taken
```

**The touch list replaces the prototype's per-cell `new Float64Array(n)`** (`:409`). It is not an
optimisation for its own sake: a reused member scratch array would carry stale demand from the
previous cell into the second inner loop, and zeroing all 48 entries per cell costs 4608 stores per
step for nothing. Iterating the touch list reads only what this cell wrote, so no clearing is needed
and the arithmetic is identical.

The forbidden shape, named in the header: **a fixed amount per cell split by demand share.** It made
a lone grazer eat the same whatever its appetite said, so the phase gate only decided who *won* a
contested cell, and every configuration settled at a starvation fixed point
(`FINDINGS.md:202-207`).

**Stage 6b — the denormal cell snap (FR-043).** Immediately after the cell loop, before the pool
update:

```
lastDenormalSnap_ = 0
for k: if (res_[k] != 0.0 && (res_[k] < kDenormalCellGuard && res_[k] > -kDenormalCellGuard)):
           poolDelta += res_[k];  lastDenormalSnap_ += res_[k];  res_[k] = 0.0
```

The magnitude test is two-sided on purpose: `res_[k] -= taken` with `taken <= res_[k]` can still
land a few ULP below zero, and returning that (negative) residue to the pool is what keeps the
invariant exact rather than papering it over. FR-087's stage list does not name FR-043; **this plan
places it here** (S14 D-B) because immediately after grazing is the only point at which a cell can
have just been driven sub-guard, and it must precede the next step's regrowth read of `room`.
`lastDenormalSnap_` is the quantity SC-021's probe compares against the pool increase.

### S4.7 Stage 7 — global feed and nonlinear leak (FR-057, FR-052, `:453-470`)

```
for i in [0, n):
    globalFeed = (appetiteSum > 0) ? feedRate_ * avail * (appetite_[i]/appetiteSum) * dt_ : 0
    if (globalFeed > avail) globalFeed = avail    // min(): correct ONLY because S4.5
                                                  // floors avail at 0 before the branch
    avail -= globalFeed
    influx = graze_[i] + globalFeed
    leak   = (leakExponent_ == 1.0f) ? leakRate_ * energy_[i] * dt_
                                     : leakRate_ * std::pow(energy_[i], double(leakExponent_)) * dt_
    dE_[i] = dE_[i] * dt_ + influx - leak
    poolDelta += leak - globalFeed
```

Three things that look like details and are not:

* `dE_[i] * dt_` — the exchange flows accumulated in stage 3 are **per-second rates**, multiplied by
  `dt` exactly here. Stage 3's `want = outflow_[i] * dt_` uses the same convention, which is what
  makes the solvency scale correct.
* Only the **global feed** is debited from the pool; the grazed part already left the cells. Debiting
  it twice destroys energy silently (`ecosystem-sim.js:467-469`).
* The `leakExponent == 1.0f` fast path is exact (`pow(x, 1) == x`) and is the default. `std::pow` at
  the default would cost 32–48 calls per step for nothing.
* `avail` arrives here **already floored at zero** (S4.5). The `if (globalFeed > avail)` line is a
  `min`, and it is a `min` only under that precondition; with a negative `avail` it is an
  assignment, and the whole deficit lands on agent 0. The header carries the one-line reason on the
  line, not only in S4.5.

**FR-052's required header sentence — the macro band.** The spec makes this header content, so it is
prescribed rather than re-derived: *"`leakExponent` above ~1.3 kills the ecosystem — at per-agent
energies of ~0.03 a superlinear leak all but vanishes, agents fill to capacity and sit (63 % alive
in the lowest tercile vs 13 % in the highest, `run.js:349-353`). Phase 10's macros must stay at or
below 1.3; the full `[1.0, 2.5]` range exists so the fuzz box can prove the component stays
**bounded** there, not because the upper half is musically usable."* It sits on the `leakExponent_`
declaration (whose range comment carries `; macros <= 1.3`, S1.5) and above this stage.

### S4.8 Stage 8 — integrate with both clamps (FR-055, `:472-489`)

```
for i: energy_[i] += dE_[i]
       if (energy_[i] < 0)            { poolDelta += energy_[i];                energy_[i] = 0 }
       else if (energy_[i] > capacityAbs_) { poolDelta += energy_[i]-capacityAbs_; energy_[i] = capacityAbs_ }
```
Both clamps **return their delta to the pool**. Neither may silently create or destroy energy; this
is half of the conservation argument and SC-001 (c) is what watches it.

### S4.9 Stages 9–11 — movement, frequency drift, phase (`:490-511`)

```
maxStep = maxSpeed_ * dt_
for i:
    vx = (moveRate_*fx_[i] + forageRate_*forage_[i]) * dt_     // foraging enters x only (FR-040)
    vy =  moveRate_*fy_[i] * dt_
    speed = sqrt(vx*vx + vy*vy)
    if (speed > maxStep) { kk = maxStep/speed; vx *= kk; vy *= kk }
    x_[i] = wrap01(x_[i] + vx);  y_[i] = wrap01(y_[i] + vy)

    // FR-013 bounded OU drift - DRAWN UNCONDITIONALLY (plan addition A-8)
    nz = nextBipolarD(driftRng_) * double(freqDrift_) * sqrtDt_
    if (freqDrift_ > 0):
        freq_[i] += nz - 0.5*(freq_[i] - freq0_[i]) * dt_ * 0.01
        freq_[i] = clamp(freq_[i], freqLoHz_, freqHiHz_)

    // FR-014
    phase_[i] = wrap01(phase_[i] + (freq_[i] + dPhase_[i]) * dt_)
```

`wrap01(t) = t - std::floor(t)`, which is exactly the prototype's `(t + 1) % 1` over the reachable
range and is robust for any finite input (the prototype's form breaks below −1). `t == 1.0` maps to
`0.0`. The result is always in `[0, 1)`.

**Plan addition A-8: the drift draw is unconditional.** The prototype draws only when
`freqDrift > 0` (`:504`), which makes the RNG stream's *position* a function of a runtime knob — so
toggling `setFreqDrift(0)` and back silently re-aligns every subsequent draw and breaks the
determinism story SC-006 rests on. Drawing always and applying conditionally is the
`bloom_engine.h:914-916` rule ("the clock draw is UNCONDITIONAL … taken BEFORE the probability is
even computed"). At the defaults (`freqDrift = 4e-5 > 0`) the behaviour is identical, so no
prototype figure moves.

`sqrtDt_` is precomputed at `prepare()`; `freqDrift_` is per-second in the same sense as the
prototype's `Math.sqrt(dt)` scaling, i.e. the OU term is a Wiener increment.

### S4.10 Stage 12 — pool update, and the deliberate absence of a clamp (FR-056, `:513-521`)

```
pool_ += poolDelta
if (pool_ < 0.0) ++conservationViolations_;
```

**The pool is never clamped.** Clamping a negative pool to zero would *create* energy and break the
invariant the whole boundedness argument rests on; the prototype says so outright (`:514-521`, "A
negative pool is a real design error and the metrics must see it"). The counter increments **once
per step whose end-of-step pool is negative** — not once per transition, and not once per negative
read. Rationale for that choice, stated in the header so a later reader does not "fix" it: the
counter's contract is "monotonic, non-saturating" and SC-001 (b) asserts `== 0`, so any
per-step-or-finer rule satisfies the criterion; per-step is the one a soak test can also use as a
*rate*. It counts **exactly one thing** — non-finite containment has its own counter (FR-056,
FR-083), because one counter with two meanings cannot tell SC-001 (b) which failure mode fired.

### S4.11 Stage 13 — publish (FR-060, FR-061, FR-062, FR-063, FR-070)

```
++stepCount_
anyRail = false
stepGain = 1.0f / float(rampSteps_)
for i in [0, n):
    target = dormant_[i] ? 0.0f : wake_[i]                  // gateSteady(): one number
    if (gate_[i] < target) gate_[i] = min(target, gate_[i] + stepGain)
    else if (gate_[i] > target) gate_[i] = max(target, gate_[i] - stepGain)

    raw = kOutputAnchor * energy_[i] * double(agentCount_) / energyBudget_ * double(gate_[i])
    if (raw > 1.0) { ++clampedSteps_[i]; anyRail = true; raw = 1.0 }
    else if (raw < 0.0) raw = 0.0                            // unreachable: energy_ >= 0
    float out = float(raw)
    if (out <= kWakeSilenceEpsilon) out = 0.0f               // FR-063, snapped AT THE SOURCE
    output_[i] = out
if (anyRail) ++outputClampEngagements_
```

Points that are contract, not style:

* **The gate is inside the normalisation**, exactly as FR-061 writes it. `clamp(raw·gate)` and
  `clamp(raw)·gate` differ, and only the former makes a dormant agent's output exactly 0.
* **Both rail counters count the UPPER rail only** (Clarification Q4). A published `0.0f` from
  dormancy, wake gating or the FR-063 snap is a *designed* state and is never counted.
  `getAgentClampedStepFraction(i) = stepCount_ ? float(double(clampedSteps_[i]) / double(stepCount_))
  : 0.0f`.
* **Outputs are recomputed only here and held between steps** (FR-062). This is required by a
  consumer, not chosen for tidiness: `FeedbackEcology::refreshGates()`'s `target != lastGateTarget`
  guard is load-bearing, and "a Phase-8 agent writing one loop's wake per block would otherwise
  stretch every OTHER loop's ramp without bound" (`feedback_ecology.h:2292-2296`). The header quotes
  that sentence.
* **The ramp is on the control-step grid, not per sample** (FR-070). `rampSteps_ = max(1,
  ceil(0.050 / dt_))`; at the FR-082 default that is 5 steps = 53.33 ms, and above
  `stepIntervalChunks ≈ 47` at 48 kHz it floors at 1 and the output reaches its target in a single
  step. The header states the quantisation and why a "50 ms ± 1 ms" contract is structurally
  unmeasurable here (this component has no per-sample path at all, FR-002).

---

## S5. FR-008's share-unit conversion — converted once, in one function

```cpp
void refreshDerivedScales() noexcept {
    meanShare_    = energyBudget_ / double(agentCount_);
    const double cellShare = energyBudget_ / double(resourceCells_);
    preyFloorAbs_ = double(preyFloorShares_) * meanShare_;
    capacityAbs_  = double(capacityShares_)  * meanShare_;
    satiationAbs_ = double(satiationShares_) * meanShare_;   // 0 stays 0 = off
    cellCapAbs_   = double(cellCapShares_)   * cellShare;
}
```

Called from exactly two places: `prepare()` (step 4 of S2.1) and each of the four share-unit setters
(`setPreyFloorShares`, `setCapacityShares`, `setSatiationShares`, `setCellCapacityShares`). It is
**not** called per step — FR-008 says "converted to an absolute energy quantity exactly once, at
`prepare()`", and a setter re-converts immediately using the *current* prepare-time
`energyBudget_`/`agentCount_`/`resourceCells_`. Since all three are prepare-time (FR-006), the
conversion cannot drift underneath a running simulation.

This is what makes SC-019 invariant on **both** its axes: `preyFloor`, `capacity` and `satiation`
scale with `energyBudget/agentCount` and `cellCapacity` with `energyBudget/resourceCells`, so the
nine-cell grid (budget 0.1×/1×/10× × agentCount 24/32/48) is the *same dynamical system* rescaled,
and FR-061's `0.5 · e · n / B` anchor lands at 0.5 in every cell by construction rather than by
coincidence.

The getters report **share units**, matching what was set (FR-008's closing sentence). The absolute
values are visible only through `getAgentEnergy`/`getCellEnergy`/`getPoolEnergy`.

---

## S6. Dormancy, wake, and the event hook

### S6.1 `setAgentWake` / `setAgentDormant` (FR-070)

```cpp
void setAgentWake(std::size_t i, float amount) noexcept {
    if (i >= agentCount_) return;                  // index -> silent no-op (FR-064)
    if (!detail::isFinite(amount)) return;         // non-finite -> reject (FR-064)
    const float w = std::clamp(amount, 0.0f, 1.0f);
    wake_[i] = (w <= kWakeSilenceEpsilon) ? 0.0f : w;   // bloom_engine.h:650-655
}
void setAgentDormant(std::size_t i, bool dormant) noexcept {
    if (i >= agentCount_) return;
    dormant_[i] = dormant;
}
```

Neither setter advances the simulation or touches `gate_` — the ramp target is recomputed at the
next publication (S4.11), which is the whole point of FR-062. `setAgentWake(i, 0)` and
`setAgentDormant(i, true)` therefore fold into one steady gate target and are **behaviourally
identical**, exactly as `ResonanceDriftNetwork::gateSteady()` (`:787-790`) and
`FeedbackEcology::gateSteady()` (`:2268-2273`) make them. SC-014 (a) asserts identical output
trajectories.

The epsilon snap in the setter and the epsilon snap on the published output (S4.11) are **both**
present and are not redundant: the setter's makes `setAgentWake(i, 1e-8)` exactly dormant; the
output's makes a near-zero *energy* publish exactly zero. FR-063 mandates the latter, quoting the
consumer's rationale — a release tail that stops at 1e-8 "must not leave a loop burning forever"
(`feedback_ecology.h:2260-2262`).

### S6.2 Dormancy gates the OUTPUT, not the simulation (FR-072, D-6, OQ-5)

A dormant agent keeps grazing, exchanging and leaking; only `gate_[i]` is driven to 0. The header
carries the mechanism-level argument the cross-cutting rule demands (roadmap lines 543–545): the
agent **is** the generator and the modulation lane — there is no chain behind it — and its energy is
a share of a *conserved* budget, so excluding it from the economy would strand or destroy that
share and break FR-023/FR-054 and every boundedness criterion. Nothing "burns" that the rule was
written to stop: the per-agent cost *is* the simulation, which must run regardless.

SC-014 (d) is the criterion that makes this claim falsifiable rather than decorative: with half the
population dormant for 1800 s, the dormant agents' own activity must be **≥ 0.30** and within
**0.5×–2×** of the awake population's. "Their energies still move" would be satisfied by one ULP of
a neighbour's rounding.

### S6.3 `perturbAgent` (FR-071)

```cpp
void perturbAgent(std::size_t i, float amount) noexcept {
    if (i >= agentCount_) return;
    if (!detail::isFinite(amount)) return;
    const double a = std::clamp(double(amount), -1.0, 1.0);
    const double delta = (a >= 0.0)
        ? std::min(a * (capacityAbs_ - energy_[i]), std::max(0.0, pool_))   // pool -> agent
        // SPEC CORRECTION (S14 D-L): the GIVING term is floored at zero BEFORE the
        // sign is applied, so delta on this branch is always in [-energy_[i], 0].
        : std::max(a * std::max(0.0, energy_[i] - preyFloorAbs_), -energy_[i]);  // agent -> pool
    energy_[i] += delta;
    pool_      -= delta;
}
```

Transcribed from FR-071's closed formula, both signs, **with one correction to the negative branch**
recorded as S14 D-L.

**The defect in the spec's closed formula, and why it is a blocker rather than an edge case.** FR-071
writes the giving branch as `max(a · (e_i − preyFloor), −e_i)`. For `a < 0` and
`e_i < preyFloorAbs_` the inner term `(e_i − preyFloor)` is **negative**, so `a × negative` is
**positive**, and `std::max(positive, −e_i)` selects the positive value. The call then *gives the
agent energy*: a "take energy" request on an agent inside the refuge becomes a withdrawal of up to
`|a| · preyFloorAbs_` from the pool — and unlike the receiving branch, the giving branch has no
`std::max(0.0, pool_)` guard, so it pays out of a pool that may already be empty or negative. That
drives `pool_` below zero, which S4.10 increments `conservationViolations_` for and which this plan
calls "a real design error"; and because at steady state the pool *is* empty (agents hold ~96 % of
the budget, S4.7 / R-5), it is the normal case, not a corner. SC-001 (d) aims calls at "an agent at
energy 0" with amounts over `[-2, +2]`: `perturbAgent(i, -1.0f)` on an agent at energy 0 yields
`delta = max(−1 · (0 − preyFloorAbs_), 0) = +preyFloorAbs_`, so clause (b)'s
`getConservationViolationCount() == 0` would fail **by construction**.

Flooring the giving term at zero before the sign is applied is the minimal fix: it makes `delta` lie
in `[−energy_[i], 0]` on every path of the negative branch, which is exactly the behaviour the
spec's own stated consequences claim ("an agent at or below `preyFloor` gives nothing — the refuge
is respected"; "`−e_i` is the hard floor"). Nothing else in the formula changes.

Stated consequences, all asserted by SC-001 (d): an agent at `capacity` receives nothing; an agent
at or below `preyFloor` gives nothing — `delta == 0.0` **exactly**, and `pool_` is bit-unchanged;
an agent is never driven below zero; a **negative pool** pays out nothing on **either** branch, so
`perturbAgent` can never deepen a violation it did not cause; and the two writes are equal and
opposite, so the conserved total is unchanged **exactly**, in `double`, for every sign and every
index. Note `a * (capacityAbs_ - energy_[i])` can be negative if `energy_[i] > capacityAbs_`
(reachable only in the step between a `setCapacityShares()` shrink and the next clamp) — `std::min`
with a non-negative pool term then yields that negative value, which correctly returns the excess.
That asymmetry is deliberate: the receiving branch's negative result *returns* over-capacity energy
to the pool, whereas a positive result on the giving branch would *create* a withdrawal, which is
why only the latter is floored.

It takes a **plain scalar**, never a `SlowEventScheduler` reference: Phase 10 owns the scheduler
(`noise_organism.h:841-843`, `resonance_drift_network.h:765-766`), and
`processors/slow_event_scheduler.h` is **not** included.

---

## S7. The non-finite guard ladder, the repair rule, and the probes

### S7.1 Where the guard sits

FR-087 stage 1: the **top** of every step, before any read. Not at the end, and not "abandon the
write and carry on" — the spec is explicit that abandonment is not containment, because the offending
value stays in place, the next step's guard fires again, and FR-062's held outputs would freeze the
component at its last published values **forever** while every finiteness assertion passed.

### S7.2 The ladder

```cpp
bool contained = false;
for (i in [0, agentCount_)):
    if (!detail::isFinite(energy_[i]) || !detail::isFinite(x_[i]) || !detail::isFinite(y_[i]) ||
        !detail::isFinite(phase_[i])  || !detail::isFinite(freq_[i])) {
        energy_[i] = meanShare_;                       // the population mean share
        x_[i] = x0_[i];  y_[i] = y0_[i];               // prepare()'s values for THIS index
        phase_[i] = phase0_[i];  freq_[i] = freq0_[i];
        gate_[i] = dormant_[i] ? 0.0f : wake_[i];      // no half-ramp left behind
        contained = true;
    }
for (k in [0, resourceCells_)): if (!detail::isFinite(res_[k])) { res_[k] = 0.0; contained = true; }
if (!detail::isFinite(pool_)) { pool_ = 0.0; contained = true; }
if (contained) {
    double t = pool_;  for i t += energy_[i];  for k t += res_[k];
    pool_ += (energyBudget_ - t);                      // restore the invariant EXACTLY
    ++nonFiniteContainments_;
}
```

`detail::isFinite(double)` (`db_utils.h:125`) only — **never** `std::isnan`/`std::isinf`/
`std::isfinite`, which fold to constants under `-ffast-math` and which
`tools/lint-nonfinite-symbols.js` forbids. `x0_`, `y0_`, `phase0_` exist for exactly this rung
(S1.5); `freq0_` already holds the initial frequency as the OU mean.

**Spec correction (S14 D-C).** FR-083 says the total is restored "by charging the difference between
the pre-repair and post-repair totals to the pool". That is unimplementable when the pre-repair total
is NaN — NaN minus anything is NaN. The operative form is above: charge
`energyBudget_ − (post-repair total)` to the pool, which re-establishes `total == energyBudget_`
exactly, which is precisely what SC-001 (c) gates. The intent is met; the arithmetic is the only one
that can be written.

The step then **proceeds normally** — that is what SC-009 (b)'s "N ≥ 1000 further steps produce
finite, in-range outputs that CHANGE" tests.

### S7.3 Why no other rung is needed

The economy has no divisions by a possibly-zero quantity after FR-005's clamps: `energyBudget_ >=
1e-3`, `agentCount_ >= 1`, `resourceCells_ >= 1`, `dist = sqrt(d2) + 1e-9 > 0`, `demandSum > 0` is
tested before `res_[k]/ask`, `want > 0` is tested before `spare/want` (S4.3), `appetiteSum > 0` is
tested before the feed share, `sigmaSq_ > 0` because `kernelSigma_ >= 0.01`. `std::pow(0, x)` is 0
for `x > 0`. So a non-finite state is reachable only through the probe or through a caller-supplied
value, and every setter rejects non-finite input (FR-064). The ladder is a **trap**, not a routine
repair path — and SC-009 proves the trap fires.

**This argument rests on FR-064's value clamp, not only on its non-finite rejection.** Two of the
rungs above are statements about a *knob*, not about the economy: `sigmaSq_ > 0` holds **because**
`setKernelSigma` clamps to Appendix A's `[0.01, 0.35]`, and `energyBudget_ >= 1e-3` because
`prepare()` clamps. An unclamped `setKernelSigma(0.0f)` makes S4.6's foraging term `w * (d /
sigmaSq_)` a division by zero and this whole containment argument collapses — and it collapses
*silently*, because no criterion listed elsewhere probes past a range bound: SC-001's and SC-013's
fuzz boxes draw every knob **inside** its range by construction, and SC-009 (a) drives only
non-finite values. The clamp is therefore gated on its own, by
`EcosystemEngine_SettersClampToRange` (S10.4), which drives every runtime setter at (min − ε,
max + ε) and asserts the getter reports the Appendix-A bound. Without that case the third of
FR-064's three normative behaviours is untested.

### S7.4 The two probes

```cpp
// ecosystem_engine_nonfinite_test.cpp - the ONLY definition
struct Krate::DSP::detail::EcosystemEngineNonFiniteProbe {
    static void injectAgentEnergy(EcosystemEngine& e, std::size_t i, double v) { e.energy_[i] = v; }
    static void injectCellEnergy (EcosystemEngine& e, std::size_t k, double v) { e.res_[k]   = v; }
    static void injectPool       (EcosystemEngine& e, double v)                { e.pool_     = v; }
};
// ecosystem_engine_test.cpp - the ONLY definition
struct Krate::DSP::detail::EcosystemEngineInspectProbe {
    static std::size_t   pairCount(const EcosystemEngine& e) { return e.pairCount_; }
    static double        pairFlow (const EcosystemEngine& e, std::size_t p) { return e.pairFlow_[p]; }
    static std::uint8_t  pairI    (const EcosystemEngine& e, std::size_t p) { return e.pairI_[p]; }
    static std::uint8_t  pairJ    (const EcosystemEngine& e, std::size_t p) { return e.pairJ_[p]; }
    static double        scale    (const EcosystemEngine& e, std::size_t i) { return e.scale_[i]; }
    static double        outflow  (const EcosystemEngine& e, std::size_t i) { return e.outflow_[i]; }
    static bool          divided  (const EcosystemEngine& e, std::size_t i) { return e.divided_[i]; }
    static std::uint64_t divisions(const EcosystemEngine& e) { return e.exchangeDivisions_; }
    static double        lastSnap (const EcosystemEngine& e) { return e.lastDenormalSnap_; }
    static std::uint64_t clampedSteps(const EcosystemEngine& e, std::size_t i)
                                                             { return e.clampedSteps_[i]; }
    static void          setPool  (EcosystemEngine& e, double v) { e.pool_ = v; }
};
```

**Plan addition A-10: `outflow_`, `divided_` and `clampedSteps_` on the probe.** Three of the
criteria as drafted could not be *expressed* through the previous probe surface, which is a worse
failure than a wrong threshold — the implementer would have quietly weakened them:

* **SC-020 is a per-agent clause** — "`scale_i == 1.0` exactly whenever `want_i == 0` **and no
  division is performed in that case**". `exchangeDivisions_` is a single cumulative counter
  incremented inside the per-agent loop, so "`divisions()` did not advance on that step" is only
  meaningful on a step where *no* agent divides. In the defaults arm that same criterion prescribes,
  agents with `want > spare` divide on essentially every step, so the check would read false for
  reasons unrelated to the FR-023 guard and would have to be dropped — leaving the exact failure it
  exists to catch (a `min(1, spare/want)` paraphrase evaluating `0/0` at `want == 0`) unasserted.
  `outflow(i)` gives the test `want_i = outflow_[i] * dt_` directly, and `divided_` — a
  `std::array<bool, kMaxAgents>` cleared at the top of stage 3 and set on the division branch beside
  the counter increment — makes the clause per agent: *for every `i` with `want_i == 0`,
  `scale(i) == 1.0` exactly and `divided(i) == false`.*
* **SC-002 (d) is a windowed clause.** `getAgentClampedStepFraction(i)` is cumulative since
  `prepare()`, so reading it once at the end measures the whole 1800 s run and not the late 600 s.
  `clampedSteps(i)` gives the raw counts, so the window fraction is a difference of two boundary
  readings.

* **`setPool`** is the fourth addition, and it is a *finite* injection, which is why it belongs to
  this probe and not to the non-finite one. SC-009 (d) and the `perturbAgent` edge case both need a
  **negative but perfectly finite** pool — the S4.5 blocker's precondition — and
  `EcosystemEngineNonFiniteProbe::injectPool` lives in the `-fno-fast-math` TU, where those two
  cases do not belong: neither is about IEEE semantics, and running them there would prove them in
  a mode the header never ships in (the A-1 argument, applied the other way round). Both cases
  therefore sit in the behaviour TU with this probe.

`divided_` costs 48 bytes and one store on a branch that already writes a counter. No public getter
is added for any of the four: they exist for SC-020, SC-002 (d) and SC-009 (d), and a public setter
for `pool_` would be surface nothing consumes — and one a caller could break the invariant with.

Neither is ever defined by the library, so a shipping build has no way to call either; declaring the
friends in the header is deliberate so the TUs need no header edit (`bloom_engine.h:150-162`
rationale). `exchangeDivisions_` and `lastDenormalSnap_` have **no public getter** — they exist only
for SC-020 and SC-021, and a public counter for them would be surface nothing consumes.

---

## S8. Read surface and diagnostics — the neutral table

The neutrals are a **public contract**, not `#ifdef` scaffolding (the `entropy_processor.h:299-302`
form). There are **three** kinds of getter on this class and they have three different contracts;
collapsing them into one table is what makes a "getter returns 0" statement self-contradictory
against FR-064.

**(i) Indexed getters.** `(i < agentCount_)` — or `(k < resourceCells_)`, or a `Kind` in range —
`? value : neutral`. The neutral applies **only** to an out-of-range index, before and after
`prepare()`, and the getter reads nothing out of range.

| Indexed getter | Neutral on an out-of-range index |
|---|---|
| `getAgentOutput`, `getAgentClampedStepFraction`, `getAgentWake` | `0.0f` |
| `getAffinity(Kind, Kind)` (out of range reachable only by a cast) | `0.0f` |
| `getAgentEnergy`, `getAgentPositionX/Y`, `getAgentPhase`, `getCellEnergy` | `0.0` |
| `getAgentCountOfKind` | `0` |
| `getAgentKind` | `Kind::Partial` |
| `isAgentDormant` | `false` |

**(ii) State getters** — everything the simulation *produces* or that `prepare()` derives. The
neutral applies to an **unprepared** object (FR-007), which has produced nothing.

| State getter | Neutral before `prepare()` |
|---|---|
| `getPoolEnergy`, `getTotalEnergy`, `getStepDurationSeconds` | `0.0` |
| `getPairInteractionCount`, `getControlStepCount`, `getAllocatedBytes` | `0` |
| `getConservationViolationCount`, `getNonFiniteContainmentCount`, `getOutputClampEngagementCount` | `0` |
| `getEnergyEntropy` | `0.0` |
| `isPrepared` | `false` |
| every indexed getter of (i), at every index | its (i) neutral |

**(iii) Knob getters are neither, and this is the load-bearing distinction.** `getKernelSigma`,
`getExchangeRate`, `getPredation`, `getPreyFloorShares`, `getCapacityShares`, `getLeakRate`,
`getLeakExponent`, `getMoveRate`, `getMaxSpeed`, `getForageRate`, `getCrowding`,
`getCrowdingRadius`, `getSyncRate`, `getCellCapacityShares`, `getRegenRate`, `getGrazeRate`,
`getFeedRate`, `getSatiationShares`, `getAppetiteDepth`, `getFreqLoHz`, `getFreqHiHz`,
`getFreqDrift` and `getAffinity` **at an in-range `Kind` pair** always report the **current
configuration** — the S1.5 member-initialised Appendix-A default, or the last clamped value a setter
stored — whether the object is prepared or not. They have **no neutral**. FR-064 requires the getter
to report what was set, and S2.1 step 8 makes configuration survive `prepare()`
("`prepare()` re-derives state, never configuration"), so on a default-constructed instance
`getKernelSigma()` is `0.03f`, `getPredation()` is `0.55f`, `getAffinity(Partial, Partial)` is
`-1.0f` and `getAffinity(Partial, Resonator)` is `+0.45f`. A knob getter returning `0.0f` before
`prepare()` would break FR-064's setter/getter round trip and, for `kernelSigma`, would report a
value the setter is not even allowed to store.

The prepare-time read-backs (`getEnergyBudget`, `getResourceCells`, `getStepIntervalChunks`,
`getInitialPoolFraction`, `getSampleRate`, `getSeed`, `getAgentCount`) sit with the **knobs**, not
the state getters, for the same reason: they are member-initialised to the `PrepareConfig` defaults
(S1.5) and report them before `prepare()`, because FR-060's contract is "read back what `prepare()`
actually did" and a `0` there would be a value `prepare()` can never produce. `getStepDurationSeconds`
is the exception and belongs to (ii): it is *derived* (`dt_`), and `dt_` is `0.0` until `prepare()`
computes it.

An **unprepared** object therefore: returns every (i) and (ii) neutral, reports every (iii) knob's
current configuration, has `processChunk()` return immediately, accepts every setter (which stores
its clamped value without touching state), and reads nothing out of bounds — the arrays are
`kMaxAgents`/`kMaxResourceCells`-sized members that exist from construction. SC-007 (b) is the arm
that proves it, and it asserts (i)+(ii) against the neutrals and (iii) against the **Appendix-A
defaults**, never against `0`.

`getEnergyEntropy()` (FR-066): Shannon entropy in bits of the normalised agent-energy distribution,
`ecosystem-sim.js:538-550` verbatim — `sum <= 0 → return 0`, `p > 0 → h -= p*log2(p)`. Its doxygen
**must** state why it is reported and never gated: it is permutation-invariant, so it cannot see
*which* agent holds the energy, and it sat at 4.9–5.0 across regimes differing 5× in per-agent
activity (`FINDINGS.md:148-157`). D-9 and SC-002/SC-005 forbid gating on it; the one place the
prototype did gate on it (its `liveness()` cycle clause ran on `hSeries`, `ecosystem-sim.js:736-757`)
is the reason SC-013's 83.0 % reference is annotated as re-measurable.

---

## S9. Allocation and footprint ledger (FR-003, SC-007)

| Member group | Count × type | Bytes |
|---|---|---|
| agent doubles (`energy_ x_ y_ phase_ freq_ freq0_ x0_ y0_ phase0_`) | 9 × 48 × 8 | 3 456 |
| agent scratch doubles (`dE_ fx_ fy_ dPhase_ outflow_ scale_ appetite_ graze_ forage_ cellDemand_`) | 10 × 48 × 8 | 3 840 |
| agent floats (`wake_ gate_ output_`) | 3 × 48 × 4 | 576 |
| agent misc (`kind_ dormant_ divided_ cellTouched_`, `clampedSteps_`) | 48 × (1+1+1+1+8) | 576 |
| resource (`res_ cellPos_`) | 2 × 96 × 8 | 1 536 |
| pair scratch (`pairFlow_`, `pairI_`, `pairJ_`) | 1128 × 8 + 2 × 1128 | 11 280 |
| affinity | 25 × 4 | 100 |
| scalars, counters, RNG | — | ~200 |
| **total** | | **≈ 21.5 KB** |

No heap term anywhere: `getAllocatedBytes()` returns `0u` unconditionally, deliberately non-`static`
so the Phase-10 host can total its children uniformly (`resonance_drift_network.h:906-912`;
`bloom_engine.h:817` carries the same NOLINT for
`readability-convert-member-functions-to-static`).

This table is the document's **single** footprint authority (S1.5 defers to it). Two figures for one
object is how a later reader "reconciles" them by shrinking the object.

**If S12.3's L4 and/or L5 are adopted**, they are members and they land in this ledger:

| Conditional member (S12.3) | Count × type | Bytes | Total with it |
|---|---|---|---|
| L4 kernel LUT `std::array<double, 1025>` | 1025 × 8 | 8 200 | ≈ 29.7 KB |
| L5 phase-sine LUT `std::array<double, 1025>` | 1025 × 8 | 8 200 | ≈ 37.9 KB (both) |

Adopting both takes the object from ≈ 21.5 KB to **≈ 37.9 KB**, which R-9's stack-local warning is
restated against: at 38 KB a stack local is not merely inadvisable, it is within an order of
magnitude of a default 1 MB thread stack once a Phase-10 voice holds several children. The lever
list is not adopted silently — S14 D-M is the entry, and the ledger row is updated in the same
commit as the lever.

**A 21.5 KB object is too large for a casual stack local.** Tests and Phase-10 callers construct it
as a member or through `std::make_unique`; the header says so in one line. SC-007's
`AllocationScope` must therefore wrap only `prepare()` and the stepping, with the object constructed
*outside* the scope if it is heap-allocated — otherwise the `make_unique` itself counts as an
allocation and the criterion fails for the wrong reason.

---

## S10. Test plan

### S10.1 TU assignment (FR-091, plus one test-local helper header)

| TU | Criteria | Tags |
|---|---|---|
| `dsp/tests/unit/systems/ecosystem_engine_test.cpp` | SC-002, SC-004, SC-006, SC-007, SC-008, SC-010, SC-014 (incl. the new (e) hold arm), SC-015 (include-discipline arm), SC-019, SC-020, SC-021, **plus the four FR-level cases with no SC of their own**: `EcosystemEngine_SettersClampToRange` (FR-064's value clamp), `EcosystemEngine_KindAssignmentIsStratified` (FR-011/FR-060), `EcosystemEngine_ExchangeDisabledAtPredationHalf` (FR-021), `EcosystemEngine_NegativePoolIsNotAbsorbedByAnAgent` (SC-009 (d), one step), `EcosystemEngine_PerturbAgentConservesUnderFuzz` (SC-001 (d)'s targeted edges — moved here from the longrun TU: it is ~30 k steps and needs `InspectProbe::setPool`), and — only if S12.3's levers are adopted — `EcosystemEngine_ApproximationTablesAreAccurate` | untagged |
| `dsp/tests/unit/systems/ecosystem_engine_longrun_test.cpp` | SC-001 **(a)–(d), the perturb schedule folded into the one 1000-config batch, D-P)**, SC-003, SC-005, SC-012, SC-013, SC-017, SC-018 | `[long]` |
| `dsp/tests/unit/systems/ecosystem_engine_perf_test.cpp` | SC-011 | `[.perf]` |
| `dsp/tests/unit/systems/ecosystem_engine_nonfinite_test.cpp` | SC-009 | untagged; **only** TU in the `-fno-fast-math` block |
| `dsp/tests/unit/systems/ecosystem_metrics_test_helpers.h` | — (shared metric code) | header, no CMake entry |

**The helper header is a plan addition (A-9) and a spec correction (S14 D-D).** FR-091 lists four
TUs and nothing else. The verdict function, the activity/frozen metrics, the pairwise-correlation
metric and the recurrence cycle scan are needed by **both** the behaviour TU (SC-002, SC-004) and the
longrun TU (SC-003, SC-005, SC-013, SC-017, SC-018); duplicating ~150 lines of statistics across two
TUs is how two copies drift apart and a criterion quietly stops measuring what it claims. A
test-local header beside the TUs is house-legal, needs **no** CMake edit (the list enumerates `.cpp`
only), and has two precedents: `dsp/tests/unit/processors/arpeggiator_core_test_helpers.h` and
`dsp/tests/unit/systems/harmonic_cloud_pre_amendment_fingerprints.h`.

### S10.2 The shared metric code (`ecosystem_metrics_test_helpers.h`)

Namespace `Krate::DSP::TestUtils::Eco`. All statistics in **`double`** — `statistical_utils.h`'s
helpers are `float`-only (`:41`, `:76`) and a `float` std/mean over 1800 samples of a 0.03-magnitude
signal loses the discrimination SC-002 needs. The house helper is cited and deliberately not used;
S14 D-E records it.

```cpp
struct Trace {                       // one sampled run
    std::size_t agents = 0;
    std::vector<std::vector<double>> energy;   // [sample][agent]   (test-side heap: legal)
    std::vector<std::vector<double>> output;   // [sample][agent]
    double sampleHz = 1.0;
};

Trace runTrace(EcosystemEngine& e, double seconds, double sampleHz = 1.0);
double meanD(std::span<const double>);  double stdDevD(std::span<const double>);
double pearsonD(std::span<const double>, std::span<const double>);
double autocorrD(std::span<const double>, std::size_t lag);   // ecosystem-sim.js:606-614

struct Liveness { double lateActivity; std::size_t lateFrozen; double latePairCorr;
                  bool cycle; };
Liveness lateWindow(const Trace&, double windowSeconds = 600.0);   // :688-716
bool     hasShortCycle(std::span<const double> series);            // :724-757
bool     verdictAlive(const Trace&);                               // SC-002's verdict function
std::size_t distinctPositions(const EcosystemEngine&, int dp = 2); // SC-002 (c)
```

Transcriptions, each with its prototype line:

* **`lateWindow`** (`:688-716`): over the last `windowSeconds` on the fixed 1 Hz grid, per agent
  `s_i = std(e_i)/grandMean` where `grandMean = mean over agents of mean(e_i)` (floored at 1e-12);
  `lateActivity = mean_i s_i`; `lateFrozen = count(s_i < 0.02)`; `latePairCorr = mean over i<j of
  |pearson(e_i, e_j)|`.
* **`hasShortCycle`** (`:724-757`, and SC-005): scan `lag = 1 …` until `autocorr < 0.2` → `decorrLag`;
  if none within `len/2`, **no cycle** (one slow trend). Otherwise the maximum autocorrelation over
  `[decorrLag, len/2)` must be `<= 0.8`. **Guarded escape (SC-005):** a series whose sample variance
  over the window is exactly 0 **fails outright** rather than taking the escape — a railed constant
  output also never decorrelates.
* **`verdictAlive`** (SC-002's verdict function, distinct from its defaults gate): last 600 s of a
  run of ≥ 900 s; alive iff `lateActivity >= 0.10` **and** `lateFrozen <= 0.25 * agents` **and** no
  agent's **output** series has a short cycle. Clause (iii) runs on **output**, never on entropy
  (Clarification Q3) — this is the one place the C++ deliberately differs from
  `ecosystem-sim.js:576-583`, whose cycle clause ran on `hSeries`.

### S10.3 The fuzz harness (longrun TU, SC-001 / SC-012 / SC-013)

A `struct RuleConfig` mirroring **all 28 Appendix-A knobs** (affinity as one `float`
`affinityMagnitude` driving `randomAffinity`'s range, matching `run.js:283-296`), an `applyTo
(EcosystemEngine&)` that calls `prepare()` with the five prepare-time fields and then every runtime
setter, and a **static knob table**:

```cpp
struct Knob { const char* name; double RuleConfig::* member; };   // size_t knobs via a double field
constexpr std::array<Knob, EcosystemEngine::kConfigKnobCount> kKnobs{ /* all 28, by name */ };
static_assert(kKnobs.size() == EcosystemEngine::kConfigKnobCount,
              "a knob was added to the engine without a fuzz-coverage entry");
// Each array is sized to its CONTENTS. No sentinel, no padding to a common length:
// clause (c) below is an unconditional loop over every element, and a nullptr entry
// would be strcmp'd - UB, and on a hardened runtime a crash in the [long] lane rather
// than a test failure, in the one test whose whole purpose is that nothing is exempted
// by omission or by typo.
constexpr std::array<const char*, 2> kExemptHostile{ "energyBudget", "stepIntervalChunks" };
constexpr std::array<const char*, 3> kExemptSane   { "energyBudget", "stepIntervalChunks", "feedRate" };
```

SC-012's three clauses, each a hard assertion:
(a) the table length equals `kConfigKnobCount` (the `static_assert` above — **plan addition A-2**
is what makes this structural: adding a setter without raising the constant is a lie the author has
to write, and raising it without extending the table fails to compile);
(b) for every knob **not exempt**, `min < max` across the batch, or the test fails outright;
(c) every exemption name is asserted to be present in `kKnobs` — an **unconditional** loop over
every element of both arrays (which is why neither carries a `nullptr` sentinel) — so a knob can
never be exempted by omission or by typo.

The boxes are `run.js:302-332` (hostile) and `:338-377` (sane), with **two documented omissions and
one documented restatement**:

* `dimensions` is dropped (FR-012 fixes 2-D), so the prototype's 500/500 covers a **superset** of the
  shipped geometry — the spec's own SC-001 note.
* The share-unit knobs (`preyFloor`, `capacity`, `cellCapacity`, `satiation`) are drawn over their
  **Appendix-A share ranges**, not the prototype's absolute ranges. At the boxes' `energyBudget = 1`
  and `agentCount ∈ [24, 48]` the two cover nearly the same absolute region (hostile `preyFloor`
  0–0.05 absolute ≈ 0–1.8 shares vs the Appendix-A [0, 1.6] shares; `capacity` 0.01–1.0 absolute ≈
  0.36–36 shares vs [0.32, 32]); this is FR-008's restatement of the same box, recorded here so the
  comparison to 500/500 is honest rather than implied.
* The meta-RNGs use the prototype's seeds (`0xf0f0f0` hostile, `0x5a5e0001` sane) so the batches are
  *comparable*, but the C++ draws are **not** expected to be per-config identical to the JS ones
  (`Xorshift32::nextUnipolar()` is `float` in C++, `double` in JS). The comparison is statistical
  (500/500 bounded; ≥ 70 % alive), never per-config.

### S10.4 Criterion by criterion

| Criterion | TEST_CASE | Strategy, thresholds, seeds |
|---|---|---|
| **SC-001 (a)–(d)** | `EcosystemEngine_HostileFuzzStaysBounded` `[long]` | 1000 hostile configs × 900 s (84 375 steps each) + a 50-config sub-batch at 1800 s. One engine instance reused (FR-065's counters clear on `prepare()`). **The seeded `perturbAgent` schedule of clause (d) runs inside this batch, throughout every configuration** — that is SC-001's letter ("For every configuration assert: (a)…(d) **with a seeded `perturbAgent` schedule running throughout**"), and it is also the cheapest reading: a separate 1000-config perturb batch would double the 93 M steps and blow the ≤ 30 min budget, while a 100-config sub-batch would be a silent 10× shrink of a spec-stated workload, which FR-085's inherited stop-and-surface rule forbids. Schedule: index drawn over `[0, 2·kMaxAgents)` so out-of-range is hit; amount over `[-2, +2]` so FR-071's `clamp(-1, 1)` is exercised past both ends; NaN/±Inf amounts built from **bit patterns via a `volatile` sink** (not `std::numeric_limits`); calls fired on a seeded ~1 Hz schedule so the per-config step cost rises by < 2 %. Per config: no non-finite in any agent energy/position/phase or the pool, tested by `detail::isFinite(double)`; `getConservationViolationCount() == 0`; `getNonFiniteContainmentCount() == 0` **separately**; `|getTotalEnergy() − getEnergyBudget()| / getEnergyBudget() <= 1e-9` sampled every 93 steps (~1 Hz). Wall-clock budget **≤ 30 min** single-threaded, Release, printed by the test. |
| **SC-001 (d) edges** | `EcosystemEngine_PerturbAgentConservesUnderFuzz` — **behaviour TU, untagged** (it is now ~30 k steps, not a `[long]` batch; it needs `EcosystemEngineInspectProbe::setPool`, which lives there) | The spec's second named case, now **targeted rather than a fuzz sub-batch** — the fuzz half of (d) lives in the row above, so this case buys the coverage the random schedule reaches only by luck. At the defaults, after 5000 settling steps, assert per call that `getTotalEnergy()` is **bit-unchanged** and `getConservationViolationCount()` does not advance, for: an agent at exactly `capacity` (`+1.0f` → `delta == 0.0`, energy and pool bit-unchanged); an agent at energy `0` (`-1.0f`); **an agent strictly below `preyFloorAbs_` with `amount < 0` → `delta == 0.0` exactly and `getPoolEnergy()` bit-unchanged** — the S14 D-L regression, which the *un*corrected FR-071 formula fails by paying the agent `+|a|·preyFloorAbs_` out of the pool; a probe-injected **negative** `pool_` with `amount > 0` → pays nothing; an out-of-range index; and a non-finite amount. Each clause names the consequence sentence it gates (S6.3). |
| **SC-002 (a)** | `EcosystemEngine_LateWindowLiveness` | 1800 s, three seeds (`0xC0FFEE`, `0xC0FFEF`, `0x5EED`), defaults. `lateActivity >= 0.30`; prototype reference **0.44**. **Re-measure first** — FR-011's stratified kind draw is not the prototype's i.i.d. draw, so the reference is a comparison baseline (spec note). A measured value below 0.30 is a **finding to surface** (FR-085), never a threshold to move. |
| **SC-002 (b)** | same | `lateFrozen == 0` exactly (prototype 0 of 32). |
| **SC-002 (c)** | `EcosystemEngine_AgentsDoNotCollapseSpatially` | `distinctPositions(engine, 2) >= 0.8 * agentCount` (prototype 31 of 32); **plus a `crowding = 0` control arm asserted to FAIL that bound** (prototype 6 of 32), so the metric is demonstrated to discriminate FR-032 rather than assumed to — the ablation prices crowding at −2 % activity in 2-D, "within noise", so SC-002 (a) and SC-004 provably cannot see it. |
| **SC-002 (d)** | `EcosystemEngine_OutputsAreNotRailed` | **Windowed, not cumulative.** `getAgentClampedStepFraction(i)` is cumulative since `prepare()`, so reading it once at the end measures the whole 1800 s run, not the late 600 s the criterion names — which judges an implementation that rails only in the early transient too harshly, and (worse) *dilutes* one that rails only late, letting a railed constant published output pass the criterion that exists to catch it. The fraction is therefore computed from `EcosystemEngineInspectProbe::clampedSteps(i)` (A-10) and `getControlStepCount()` read at **both** window boundaries and differenced: `(clamped_end − clamped_start) / (steps_end − steps_start)`. Mean over agents **≤ 0.05**; no single agent **> 0.25**. The C++ figure is transcribed into the test comment at implementation time from the measured defaults run. |
| **SC-003** | `EcosystemEngine_LivenessIsDurationStable` `[long]` | 600/900/1200/1800/3600 s × 3 seeds. `verdictAlive` true in all 15 cells; `(max − min)/mean` of `lateActivity` across the five lengths **< 0.20** (the statistic is written out because the three readings of "varies by < 20 %" differ by up to 2×). Prototype: 0.430/0.434/0.433/0.442/0.459 → 0.066. |
| **SC-004 (a)** | `EcosystemEngine_AgentsDecorrelate` | 1800 s, last 600 s, defaults: mean pairwise `|corr(e_i, e_j)| <= 0.35` (prototype 0.18; the failure regime measured 0.84–0.99). |
| **SC-004 (b)** | `EcosystemEngine_SeedsProduceDifferentVoices` | **Relative, never absolute** (Clarification Q7). Within-run floor: mean `|corr(e_i, e_j)|` between different agents of the **seed-0** run, over the **full 900 s** on the ~1 Hz grid, on **energies**. Across 8 seeds, mean per-agent cross-seed `|corr|` (same quantity, grid, duration) **≤ 1.5 × floor**. Reference: 0.13 vs 0.12. The round-1 0.61/0.50 figures are struck. |
| **SC-005** | `EcosystemEngine_NoShortLimitCycle` `[long]` | 1800 s; per agent, `hasShortCycle(output_i)` false. Escape clause guarded: zero late-window sample variance **fails**. Prototype worst post-decay peak 0.175 at 557 s. |
| **SC-006 (a)** | `EcosystemEngine_DeterministicUnderSeed` | Two instances, same seed, 100 000 control steps: `memcmp`-equal on every agent `double` (energy, x, y, phase, freq) and the pool — bit-identical, same binary. **The compared set explicitly includes `getAgentOutput(i)` and `getAgentWake(i)` for every agent**, plus every resource cell: a doubles-only comparison leaves FR-062's published surface outside both bit-identity criteria (see the SC-014 (e) row). |
| **SC-006 (b)** | same | Seeds `n`, `n+1`: per-agent cross-seed `|corr|` under SC-004 (b)'s relative bound; adjacent-seed **entropy** correlation `|ρ| <= 0.2` (prototype −0.02). |
| **SC-006 (c)** | same | `reset()` reproduces `prepare()`'s state exactly (bit-identical). |
| **SC-006 (d)** | same | **No bit-exact float golden is checked in.** Any non-same-binary comparison uses `render_fingerprint.h`'s `kSampleTolerance = 5.0e-4f` / `kMetricTolerance = 2.5e-4`. `tools/lint-float-bit-goldens.js` must pass. |
| **SC-007 (a)** | `EcosystemEngine_NoAllocationAfterPrepare` | Engine constructed **outside** the scope; `AllocationScope` (`allocation_detector.h:111`) around `prepare()`, 10 000 `processChunk` calls, every setter, `reset()`, `setSeed()`, `perturbAgent()`: `getAllocationCount() == 0`. `getAllocatedBytes() == 0`. |
| **SC-007 (b)** | `EcosystemEngine_UnpreparedIsNeutral` | Default-constructed instance, **asserting S8's three getter classes separately — they do not share a contract**. (i) every **state** getter returns its S8 (ii) neutral: `isPrepared() == false`, `getAllocatedBytes() == 0`, `getPoolEnergy()`/`getTotalEnergy()`/`getStepDurationSeconds()`/`getEnergyEntropy() == 0.0`, every counter and `getControlStepCount() == 0`. (ii) every **knob** getter returns its **Appendix-A default** from S1.5, *not* `0.0f` — `getKernelSigma() == 0.03f`, `getPredation() == 0.55f`, `getLeakExponent() == 1.0f`, `getSyncRate() == 0.0f` (which is its default, not a neutral), `getAffinity(Partial, Partial) == -1.0f`, `getAffinity(Partial, Resonator) == +0.45f`, and the prepare-time read-backs at their `PrepareConfig` defaults (`getAgentCount() == 32`, `getEnergyBudget() == 1.0`, …). A `0.0f` here is the bug this clause exists to catch: it would mean a knob getter is lying about its stored value and FR-064's round trip is broken. (iii) `processChunk` at several sizes and every setter called before `prepare()`: no crash, no allocation, and a setter's value is then visible through its getter (FR-064 holds unprepared). Then every **indexed** accessor at out-of-range indices, before **and** after `prepare()`: the S8 (i) neutrals, no crash, no allocation. |
| **SC-008** | `EcosystemEngine_BlockPartitionInvariance` | 1 000 000 samples delivered as (i) one call, (ii) 512-blocks, (iii) 64-chunks, (iv) a seeded irregular partition (1, 7, 513, 63, …): bit-identical state in all four, and `getControlStepCount()` equal. |
| **SC-009 (a)** | `EcosystemEngine_NonFiniteInputsRejected` | `-fno-fast-math` TU. NaN/+Inf/−Inf built **from bit patterns through a `volatile` sink** (`std::numeric_limits` folds to finite garbage under `-ffast-math`, and macOS CI builds that way) fed to every setter: the previous value stands, asserted through the matching getter. |
| **SC-009 (b)** | `EcosystemEngine_NonFiniteStateIsContained` | Probe-inject a non-finite agent energy, cell energy and pool. Next step: every `getAgentOutput` finite and in `[0, 1]`; `getNonFiniteContainmentCount()` incremented; `getConservationViolationCount()` **unchanged**; total back inside 1e-9 relative. Then **1000 further steps produce finite, in-range outputs that CHANGE** (≥ 1 agent's output differs from its containment-step value) — the clause that fails a "freeze forever but finite" implementation. |
| **SC-009 (c)** | `EcosystemEngine_DivisorKnobExtremes` | `prepare()` with `energyBudget` 0 / negative / 1e-300 / 1e300 / non-finite; `agentCount` 0 and `SIZE_MAX`; `resourceCells` 0; `stepIntervalChunks` 0 and `SIZE_MAX`. **Sample rate: non-finite (NaN, ±Inf, bit-pattern-built) *and* below the floor** — `{0.0, -48000.0, 1.0, 7999.0}`, each asserting `getSampleRate() == kMinUsableSampleRate` (8000.0) and `getStepDurationSeconds() == stepChunks·64 / 8000.0`. FR-005 and the spec's Edge Cases clamp *both* cases ("a sample rate below `kMinUsableSampleRate` **or** non-finite: clamped, reported by the getter"), and only the sub-floor arm exercises S2.1 step 1's `std::max` floor, on which `dt_`, `sqrtDt_` and `rampSteps_` all depend. Each clamp asserted through `getEnergyBudget()`, `getAgentCount()`, `getResourceCells()`, `getStepIntervalChunks()`, `getSampleRate()`; the resulting outputs finite and in `[0, 1]`. |
| **SC-009 (d)** | `EcosystemEngine_NegativePoolIsNotAbsorbedByAnAgent` | The S4.5 blocker's regression, and a `[not long]` case because it is one step. Prepare at the defaults (`feedRate_ == 0`), settle 200 steps, snapshot every `getAgentEnergy(i)`; probe-inject `pool_ = -0.25 · energyBudget`; step **once**. Assert no agent's energy moved by more than the magnitude the ordinary rules can produce in one step (bound it by re-running the same step from the same snapshot with `pool_ = 0.0` and requiring the two per-agent deltas to agree to `1e-12` relative), and in particular that **agent 0** did not absorb ≈ the full deficit. An implementation with the clamp inside the `if (avail > 0)` branch charges agent 0 `influx = graze_[0] + avail` and fails on the first assertion. Repeat at `feedRate_ = 0.5` (where the deficit is *split* by appetite share rather than dumped on agent 0) so the case is not accidentally specific to the `-0.0 > avail` path. |
| **SC-010 (a)** | `EcosystemEngine_SampleRateIndependent` | 1800 s × 3 seeds at 44 100 / 48 000 / 96 000 Hz, and `stepIntervalChunks` 4 / 8 / 16 at 48 kHz: `verdictAlive` true in all 18 cells. |
| **SC-010 (b)** | `EcosystemEngine_StepIntervalBand` | `getControlStepCount()` within **one** of `duration · sampleRate / (stepIntervalChunks · 64)` in every cell; a second `prepare()` at a different rate is clean (no stale `dt_`, residues zeroed). |
| **SC-010 (c)** | same, `WARN` | Late activity per cell + cross-seed spread printed as a table. **Not gated** — no cross-`dt` band has ever been measured, and asserting one would be a coin flip. A band derived from the measured spread may be added by amendment. |
| **SC-011 (a)+(b)** | `EcosystemEngine_CpuBudget` `[.perf]` | See S12. |
| **SC-011 probe** | `EcosystemEngine_StageCostProbe` `[.perf]` | See S12.2. `REQUIRE`s only that every figure is finite and > 0; the verdict is `WARN`-reported. |
| **SC-012** | `EcosystemEngine_FuzzCoverageIsComplete` `[long]` | S10.3 (a)(b)(c). |
| **SC-013** | `EcosystemEngine_SaneBoxLiveness` `[long]` | 500 sane configs × 900 s: **≥ 70 %** `verdictAlive`, **0** unbounded. Prototype 83.0 % under its *entropy*-based cycle clause; the C++ figure re-measures under the corrected output-based clause and may differ — a divergence is surfaced, not absorbed. Also **reports** the alive fraction per tercile of each knob, so Phase 10 inherits the death predictors. |
| **SC-014 (a)** | `EcosystemEngine_DormancyMatchesHouseRule` | `setAgentWake(i, 0)` and `setAgentDormant(i, true)` produce **identical** output trajectories over 2000 steps. |
| **SC-014 (b)** | same | Wake 0 → 1 reaches target in exactly `rampSteps = max(1, ceil(0.050 / stepDuration))` control steps, **±1 step**, monotone non-decreasing, no step larger than `1/rampSteps`. Asserted at `stepIntervalChunks` = 1, 8 **and 64**; at 64 (85.3 ms > 50 ms) `rampSteps == 1` and the test asserts the **single-step jump** explicitly. A "50 ms ± 1 ms" form is forbidden by name (unmeasurable on a 10.667 ms publication grid). |
| **SC-014 (c)** | same | An output driven `<= 1e-6` publishes **exactly `0.0f`**. |
| **SC-014 (d)** | `EcosystemEngine_DormantAgentsStayInTheEconomy` | Half the population dormant, 1800 s: SC-001's conservation clauses hold; dormant agents' mean activity **≥ 0.30** and within **0.5×–2×** of the awake population's. |
| **SC-014 (e)** | `EcosystemEngine_OutputsAreHeldBetweenSteps` (FR-062) | Nothing else in the matrix reads FR-062 directly, and its consumer hazard is named (`feedback_ecology.h:2292-2296`: a Phase-8 agent republishing per block "would stretch every OTHER loop's ramp without bound"). Both bit-identity criteria are doubles-scoped, so an implementation that recomputed on **every** `processChunk` call would keep identical doubles and pass SC-006 (a) and SC-008 unchanged — which is why the SC-006 (a) row now names `getAgentOutput`/`getAgentWake` explicitly, and why this clause exists. At `stepIntervalChunks = 8` (512 samples/step), start a wake ramp from 0 → 1 so the published value is **in motion** (a held-vs-republished distinction is invisible on a settled output), then deliver the step's 512 samples as 16 calls of 32 samples: after each of the first 15, `getAgentOutput(i)` and `getAgentWake(i)` are **bit-unchanged** from the value published at the last step boundary, for every agent; only the call that crosses the boundary may change them. Repeat with `numSamples = 1` × 512. |
| **SC-015** | *(CI/lint, plus one in-TU arm)* | `lint-layers.js`, `lint-odr.js`, `lint-nonfinite-symbols.js`, `lint-float-bit-goldens.js`, `check-portability.js`, `lint-simd-aligned-loadstore.js` (vacuous, must still pass), `run-clang-tidy.ps1 -Target dsp`. **Plus** `EcosystemEngine_HeaderIncludesOnlyLayerZero` in the behaviour TU: read the header, assert every `<krate/dsp/...>` include names `core/` (S1.1 — `lint-layers.js` structurally cannot see FR-001's stricter rule). |
| **SC-016** | *(gate, compliance pass)* | `git diff --stat` touches only the new header, the four TUs, the helper header, the two `dsp/tests/CMakeLists.txt` lines and the one `dsp/lint_all_headers.cpp` line; all five dsp layer exes green. |
| **SC-017** | `EcosystemEngine_RefugeFloorPreventsPredationCliff` `[long]` | `predation ∈ {0.6, 0.7, 0.85}` × 3 seeds × 1800 s at the defaults: `lateFrozen == 0` at every point, and no agent's energy sits at 0 for any part of the late window. **`preyFloorShares = 0` control arm at `predation = 0.7` must FAIL** — without it, removing FR-022 makes SC-002 *greener* (+18 % activity, `FINDINGS.md:274`) and every other criterion would pass. |
| **SC-018** | `EcosystemEngine_OvernightSoak` `[long]` | 28 800 s × 3 seeds = **2 700 000 control steps per seed**. SC-001 (a)(b)(c) throughout (sampled ~1 Hz), plus SC-002 (a) `>= 0.30` and (b) `== 0` over the **final 600 s**. |
| **SC-019** | `EcosystemEngine_OutputAnchorIsScaleInvariant` | `energyBudget` 0.1×/1×/10× crossed with `agentCount` 24/32/48 (nine cells) × 3 seeds × 1800 s: late-window population mean of `getAgentOutput` is **0.5 ± 0.05**, and the 5th–95th percentile range agrees across all nine cells to **±0.05** absolute. Also **reports** `Σ energy_ / energyBudget` per cell — the mean output is `0.5 · (Σe)/B`, so if a cell lands outside the band the report says immediately whether the anchor or the agents' share of the budget moved (S13 R-5). |
| **SC-020** | `EcosystemEngine_ExchangeScaleIsWellFormed` | Via `EcosystemEngineInspectProbe`, 10 000 steps across the defaults, `agentCount = 1`, `preyFloorShares` at 1.6 (so most agents sit **below** the floor) and `kernelSigma = 0.01` (so most agents have **no** surviving pair). **The clause is per agent, which is why A-10 adds `outflow(i)` and `divided(i)` to the probe**: the old form ("`divisions()` did not advance on that step") reads a single cumulative counter and is therefore only meaningful on a step where *no* agent divides — in the defaults arm this row itself prescribes, agents with `want > spare` divide on essentially every step, so the check would be false for reasons unrelated to FR-023's guard and would have to be weakened or dropped, leaving the `0/0` paraphrase unasserted. Assert, per step, per agent `i`: `scale(i)` is finite and in `[0, 1]`; and where `want_i = outflow(i) · getStepDurationSeconds()` is exactly `0.0`, **`scale(i) == 1.0` exactly and `divided(i) == false`** — the pair of clauses a `min(1, spare/want)` paraphrase fails on both counts (it evaluates `0/0` → NaN and it performs the division). Plus, for every applied pair, the **sign of the applied flow equals the sign of its desired flow** (or is exactly 0). |
| **FR-021** | `EcosystemEngine_ExchangeDisabledAtPredationHalf` | FR-021's second defence, made observable rather than only documented (the first is SC-012's coverage assertion; the header note is S4.2). The trap already cost the prototype an ablation run reported as bit-identical to baseline (`FINDINGS.md:119-122`), and the spec's Edge Cases list `predation = 0.5` explicitly while no arm exercised it. Defaults, `setPredation(0.5f)`, 2000 steps: at every step `EcosystemEngineInspectProbe::pairCount() > 0` (pairs still survive the `w < 1e-6` cutoff — so the zero is the `(1 − 2·predation)` factor, not an empty pair list) **and** every `pairFlow(p)` for `p ∈ [0, pairCount())` is **exactly `0.0`**. Also assert the run stays bounded and finite and that `getConservationViolationCount() == 0`, per the Edge Case's "must be bounded". A control arm at `predation = 0.55` asserts at least one non-zero `pairFlow`, so the case is demonstrated to discriminate. |
| **FR-064 clamp** | `EcosystemEngine_SettersClampToRange` | FR-064 specifies **three** normative setter behaviours; SC-009 (a) gates non-finite rejection and SC-007 (b) gates the index no-op, but **no criterion gated value clamping** for any of the 23 runtime setters. That is load-bearing, not cosmetic: S7.3's containment argument leans on `sigmaSq_ > 0` *because* `kernelSigma_ >= 0.01`, and an unclamped `setKernelSigma(0.0f)` makes S4.6's `w · (d / sigmaSq_)` a division by zero while every other criterion still passes (SC-001/SC-013 draw knobs **inside** their ranges, so nothing else ever probes past a bound). Drive every knob setter from a static table sharing the **same** `kMin…`/`kMax…` constants the header uses — one row per knob, three calls each: `min − ε`, `max + ε`, and an in-range value — asserting the getter reports the Appendix-A bound (clamped) or the in-range value (stored), exactly. Includes `setAffinity` at `±3.0f` → `∓`/`±2.0f` (FR-031's `[kMinAffinity, kMaxAffinity]`) at several `Kind` pairs, and `setFreqRangeHz` **inversion** (`hi < lo`, both in range) asserting S1.4's `hi >= lo + 1e-4` ordering holds through `getFreqLoHz()`/`getFreqHiHz()`. Run before **and** after `prepare()` (S8 (iii): knob getters have no unprepared neutral), and assert `getAllocationCount() == 0` throughout. |
| **FR-011 / FR-060** | `EcosystemEngine_KindAssignmentIsStratified` | FR-011's stratified deal is a **deliberate deviation from the prototype** (S2.2 (a)) and FR-060 requires the five per-kind counts to sum to `getAgentCount()`, but no row gated either — the only downstream mention was R-2, which *prints* the histogram. An implementation that kept the prototype's i.i.d. draw, or dealt without shuffling (leaving kind correlated with agent index, which S2.2 says the shuffle exists to prevent), or mis-summed `kindCount_`, passed every criterion in this matrix. For `agentCount ∈ {1, 4, 5, 32, 47, 48}` × 8 seeds assert: `Σ_k getAgentCountOfKind(k) == getAgentCount()`; `max_k − min_k <= 1` (the deal plus a one-at-a-time remainder can differ by at most one); every kind's count `>= 1` whenever `agentCount >= kNumKinds` (5), and the below-5 cells permitted to have zeroes (FR-011 names that edge); `getAgentKind(i)` is `Kind::Partial` for out-of-range `i`. Shuffle: across the 8 seeds at `agentCount = 48`, assert the per-index kind vector is **not** the deal order (`kind_[i] == Kind(i / base)`) in at least 7 of 8, and that the mean over seeds of the rank correlation between agent index and kind index is within `±0.25` of 0 — a dealt-but-unshuffled implementation scores ≈ +1.0. |
| **L4 / L5**, *only if adopted* | `EcosystemEngine_ApproximationTablesAreAccurate` | S12.3's L4 and L5 replace formulas the spec states as **normative** (FR-012's `w = exp(−d²/2σ²)`, FR-050's `sin(2π·phase)`, FR-035's Kuramoto `sin`) and that Assumption 1 rests on. "Must be re-measured against SC-002/SC-004" is **not** a gate — those carry ±30 % and ≤ 0.35 margins that a 2e-5 kernel error cannot move, so they structurally cannot detect a mis-built table (a wrong index scale, an off-by-one at the last entry, a table built before `prepare()` set σ). The accuracy bound is asserted directly, on the table itself: over 100 001 uniform samples of `u ∈ [0, 13.8155]`, `max |LUT(u) − std::exp(−u)| / std::exp(−u) <= 2.3e-5`, and at the two endpoints exactly; over 100 001 uniform samples of `φ ∈ [0, 1)`, `max |LUT(φ) − std::sin(2π·φ)| <= 1.0e-5`, with `φ = 0`, `0.25`, `0.5`, `0.75` asserted against their exact values. Both arms also assert the table is populated after `prepare()` at three sample rates and unchanged by `reset()`. |
| **SC-021** | `EcosystemEngine_DenormalCellSnapConserves` | `cellCapacityShares` at its Appendix-A minimum, `grazeRate` at maximum, `regenRate = 0`. Step until a cell goes sub-guard; assert `getCellEnergy(k) == 0.0` **exactly** (not subnormal, not 1e-234), no cell in the field holds a subnormal, and `getPoolEnergy()` increased by **exactly** `InspectProbe::lastSnap()`. SC-001 (c) is fourteen decades too coarse to see this. |

### S10.5 Runtime ledger (so a `[long]` overrun is a known number, not a surprise)

At the defaults a control step costs ≈ 700 `exp` calls (S12.1), so a 900 s run (84 375 steps) is
~0.6 s and a 1800 s run ~1.2 s in Release.

| Case | Steps | Projected Release wall-clock |
|---|---|---|
| SC-002 (3 seeds × 1800 s) | 506 k | ~8 s (metrics included) |
| SC-004 (b) (8 seeds × 900 s) | 675 k | ~10 s |
| SC-003 (3 seeds × 7100 s total) | 666 k | ~10 s |
| SC-005 (1800 s, 32 autocorr scans) | 169 k | ~5 s |
| SC-017 (3 × 3 × 1800 s + control) | 1.5 M | ~25 s |
| SC-013 (500 × 900 s, sane box) | 42 M | ~8 min |
| SC-018 (3 × 2.7 M) | 8.1 M | ~2 min |
| **SC-001 (a)–(d) (1000 × 900 s hostile + 50 × 1800 s, perturb schedule throughout)** | **92.8 M** | **~25 min + ~2 % for the perturb calls = ~25.5 min — the budget is ≤ 30 min** |
| SC-001 (d) edges (`PerturbAgentConservesUnderFuzz`, targeted, behaviour TU) | ~30 k | < 1 s |
| `SettersClampToRange` / `KindAssignmentIsStratified` / `ExchangeDisabledAtPredationHalf` / `NegativePoolIsNotAbsorbedByAnAgent` / `ApproximationTablesAreAccurate` | ~60 k total | < 2 s total |
| **total, every case in this table** | **~146 M** | **~36 min** (of which the `[long]` lane is ~35 min; the untagged behaviour cases ~25 s) |

**The perturb schedule is inside SC-001's 1000-config batch, and the ledger costs it there.** The
earlier shape — a separate 100-config sub-batch — was a silent 10× shrink of a spec-stated workload
("For every configuration assert: (a)…(d) with a seeded `perturbAgent` schedule running throughout")
recorded in no S14 entry, and it also left the batch's own step count (93 M, ~25 min) computed on
*less* work than the plan scheduled. Folding the schedule into the one batch costs a few thousand
setter calls per configuration rather than 8.4 M extra steps, so clause (d) now holds for all 1000
configurations at ~2 % added cost instead of 10 % coverage at ~22 extra minutes. The 50-config
1800 s sub-batch is the spec's own ("A second sub-batch of 50 configurations at 1800 s"), not a
shrink.

SC-002 at ~8 s stays under the `[long]` convention's ~15 s threshold and therefore stays untagged in
the behaviour TU. **If the measured figure exceeds 15 s, tag it `[long]` and move it to the longrun
TU** — a recorded deviation, not a silent one. SC-001's budget is part of the criterion: a miss is
surfaced under FR-085, never repaired by shrinking the config count (the roadmap's own 1000), the
duration (the 900 s the 500/500 reference was measured at), or the perturb schedule's coverage.

---

## S11. Build integration — the exact edits

1. **`dsp/tests/CMakeLists.txt`**, inside the enumerated `dsp_systems_tests` list, after the last
   Phase-7 TU line (`unit/systems/bloom_engine_nonfinite_test.cpp`, `:476` — the four Phase-7 TUs
   sit `:473-476`, with `:471-472` still comment lines) and before the closing `)` (`:477`).
   **Re-read the file before splicing**: an earlier draft of this plan cited `:474`/`:475`, which
   would have spliced the Phase-8 block into the *middle* of the Phase-7 TU list. The `:477` figure
   matches the spec (`spec.md:13`).

   ```cmake
   # Vorago Phase 8 (specs/vorago-phase8-ecosystem): EcosystemEngine.
   # This list is ENUMERATED, not globbed - an unregistered TU silently drops
   # out of the build and its cases never run.
   #   ecosystem_engine_test.cpp           SC-002, SC-004, SC-006, SC-007, SC-008,
   #                                       SC-009 (d), SC-010, SC-014, SC-015, SC-019,
   #                                       SC-020, SC-021, plus the FR-011/FR-021/FR-064
   #                                       and perturb-edge cases (S14 D-O, D-P)
   #   ecosystem_engine_longrun_test.cpp   SC-001 (a)-(d), SC-003, SC-005, SC-012,
   #                                       SC-013, SC-017, SC-018   (the [long] set)
   #   ecosystem_engine_perf_test.cpp      SC-011 + the stage probe   [.perf]
   #   ecosystem_engine_nonfinite_test.cpp SC-009 only
   unit/systems/ecosystem_engine_test.cpp
   unit/systems/ecosystem_engine_longrun_test.cpp
   unit/systems/ecosystem_engine_perf_test.cpp
   unit/systems/ecosystem_engine_nonfinite_test.cpp
   ```

2. **`dsp/tests/CMakeLists.txt`**, inside the `-fno-fast-math` `set_source_files_properties` block
   (opens `:569`), immediately before `PROPERTIES COMPILE_FLAGS …` (`:921`), **one** entry with the
   house comment shape:

   ```cmake
   # Vorago Phase 8: SC-009 injects NaN/Inf via bit patterns in this TU and needs
   # IEEE semantics to assert on them; it is also the only definition of
   # detail::EcosystemEngineNonFiniteProbe. ONLY this one of the four Phase 8 TUs
   # is listed - the other three stay out so the FR-064/FR-083 guards are proved in
   # the /fp:fast + -ffast-math mode the header actually ships in, and so the perf
   # TU's figures are not changed by the flag.
   unit/systems/ecosystem_engine_nonfinite_test.cpp
   ```

3. **`dsp/lint_all_headers.cpp`**, after the Phase-7 include (`:191`), before the Layer-4 block
   (`:193`):

   ```cpp
   // Vorago Phase 8 (specs/vorago-phase8-ecosystem), FR-001
   #include <krate/dsp/systems/ecosystem_engine.h>
   ```

   **This edit is not in the spec's FR-091 file list** — S14 D-F records the correction. Without it
   `run-clang-tidy.ps1 -Target dsp` never analyses the new header and SC-015's clang-tidy clause is
   vacuous.

4. **No** edit to `tests/test_helpers/CMakeLists.txt` — the new helper header lives beside its TUs,
   not in the shared helper root, and the enumerated list names only `.cpp`.

### S11.1 Commands

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# build + run the layer that owns the new TUs
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "EcosystemEngine*" 2>&1 | tail -5

# SC-016's consumer regression (the suites compiling the untouched shared headers)
"$CMAKE" --build build/windows-x64-release --config Release \
    --target dsp_core_tests dsp_primitives_tests dsp_processors_tests dsp_effects_tests
for t in dsp_core_tests dsp_primitives_tests dsp_processors_tests dsp_systems_tests dsp_effects_tests; do \
    build/windows-x64-release/bin/Release/$t.exe 2>&1 | tail -3; done

# the [long] set, run explicitly (per-push CI excludes it; it runs nightly on all 3 OSes)
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "[long]" "EcosystemEngine*" 2>&1 | tail -20

# SC-015's gates
node tools/lint-layers.js && node tools/lint-odr.js && node tools/lint-nonfinite-symbols.js \
  && node tools/lint-float-bit-goldens.js && node tools/lint-simd-aligned-loadstore.js \
  && node tools/check-portability.js

# clang-tidy: single-TU on Windows for a small change set
clang-tidy -p build/windows-ninja dsp/lint_all_headers.cpp

# perf, ALONE, nothing else running
node tools/run-cpu-tests.js dsp_systems_tests
```

---

## S12. CPU budget — FR-085's ceiling, and the projection that says it is the phase's hard problem

### S12.1 Measurement basis (inherited verbatim)

Nanoseconds per **512-sample block at 48 kHz**; one block period is **10 666 667 ns**, so FR-085's
0.5 % ceiling is **53 333 ns/block**. Best-of-25 trials × 500 blocks after 400 warm-up blocks,
tagged `[.perf]`, **run alone** (`node tools/run-cpu-tests.js dsp_systems_tests`). A percent-of-core
figure is reported, never asserted (`resonance_drift_network_perf_test.cpp:66-88`). The
**stop-and-surface rule** is inherited verbatim into the TU's header comment.

### S12.2 The projection, made before the code is written

Cost model: `E` = `std::exp` calls per step, plus `S` = `std::sin`, plus `Q` = `std::sqrt`. There is
**no `fastExp` in this repo** — `core/fast_math.h:13-15` records that it was removed because MSVC's
`std::exp` was faster — so `E` is the term that decides the phase.

| Configuration | pairs surviving | cell visits surviving | E | S | Q | steps/block | projected ns/block |
|---|---|---|---|---|---|---|---|
| **Defaults** (32 agents, 64 cells, σ=0.03, sync off, `stepChunks`=8) | ~34 of 496 | ~640 of 2048 | ~674 | 32 | 34 | 1 | **~7 000** (0.07 %) |
| **SC-011 (a)** worst case (48, 96, σ=0.35, sync on, `stepChunks`=8) | 1128 of 1128 | 4608 of 4608 | 5 736 | 1 176 | 1 128 | 1 | **~58 000 (1.09× over)** |
| **SC-011 (b)** same at `stepChunks = 1` | as above | as above | 5 736 | 1 176 | 1 128 | **8** | **~464 000 (8.7× over)** |

(Assumes ~7 ns per `std::exp(double)`, ~10 ns per `std::sin`, ~3 ns per `std::sqrt` on the reference
machine, plus loop overhead. These are estimates whose only job is to say *where* the problem is; the
probe replaces them with measurements.)

**SC-011 (b) is projected to miss by roughly 9×, and SC-011 (a) by ~10 %.** That is stated here, in
the plan, on purpose — the Phase-3 precedent is that the plan projected the twelve-bank shape over
budget and the probe confirmed it, which is how the phase avoided discovering it at the end.

### S12.3 The ordered lever list (apply in this order; each is exact or error-bounded)

| # | Lever | Effect on the worst case | Cost |
|---|---|---|---|
| **L1** | The two-stage cutoff (S4.0, already in the design) | **none** at σ=0.35 (nothing is skipped); −93 % of pair `exp` and −68 % of cell `exp` at the defaults | free |
| **L2** | `syncRate == 0` guard on the `sin` (A-7) | none (sync is on in the worst case); −34 `sin`/step at the defaults | free |
| **L3** | `leakExponent == 1` fast path (S4.7) | −48 `std::pow`/step | free |
| **L4** | **σ-independent kernel LUT.** Tabulate `exp(−u)` for `u = d²/(2σ²) ∈ [0, 13.8155]` in a `std::array<double, 1025>` built once at `prepare()` (σ-independent by construction — the *only* σ-dependence is the `u` scaling), linear interpolation. Relative error ≤ **2.3e-5**, independent of σ. | `E` cost drops ~7 ns → ~1.5 ns: worst case **58 000 → ~26 000** (a) and **464 000 → ~208 000** (b) | **NOT pre-authorised.** 8 200 B member (S9's conditional ledger row); replaces **FR-012's normative** `w = exp(−d²/2σ²)`; requires the S14 D-M entry, the `ApproximationTablesAreAccurate` arm, and **user sign-off from the measured table — the same route as L6** |
| **L5** | **Phase-sine LUT.** `sin(2π·Δφ)` over `[0, 1)`, 1024 entries + lerp, for the Kuramoto term and the appetite gate. Error ≤ ~1e-5. | −1 176 `sin`/step: worst case **26 000 → ~14 000** (a) and **208 000 → ~112 000** (b) | **NOT pre-authorised.** 8 200 B; replaces **FR-050's** `sin(2π·phase)` appetite gate and **FR-035's** Kuramoto `sin`, both normative; same D-M entry, same accuracy arm, same sign-off |
| **L6** | **Escalate.** FR-085 names exactly two permitted responses when the cost cannot be reduced further: reduce cost, **or** put the measured table to the user with a proposal to **narrow FR-082's minimum**. Restating the budget per *step* is **forbidden by name** (it is an 8× relaxation wearing a derivation); so is raising the ceiling, lowering `kMaxAgents`, or exempting the cheap end. | With L1–L5 the projection clears (a) comfortably and still misses (b) by ~2×; the likely proposal is `kMinStepIntervalChunks = 4` (projected ~28 000 ns/block) | a **spec amendment**, decided by the user from the measured table |

**L1–L3 are ordinary levers; L4–L6 are not.** L1, L2 and L3 are exact — the two-stage cutoff is
monotone-equivalent to the normative test and strictly conservative (S4.0), the `sin` guard
multiplies by a factor that is zero either way (A-7), and `pow(x, 1) == x`. Not one number moves, so
they need no authorisation and no deviation entry. L4 and L5 are different in kind: they substitute
an approximation for a formula the **spec states as normative**, and the spec's Assumption 1 ("the
prototype's rules transfer to C++ unchanged") rests on those formulas. They are therefore routed the
way L6 is — measured table first, then the user — and **not** adopted on the implementer's own
judgement.

"Must be re-measured against SC-002/SC-004" is **not** a gate, and pretending it was one is the
trap this paragraph closes: SC-002's activity gate carries a ±30 % margin and SC-004's correlation
bound is ≤ 0.35, neither of which a 2.3e-5 kernel error can move. A *correctly built* table is
invisible to them — and so is a **badly** built one whose error is a few orders larger (a wrong
index scale, an off-by-one at entry 1024, a table built before `prepare()` knows σ). The gate is
`EcosystemEngine_ApproximationTablesAreAccurate` (S10.4), which asserts the stated bounds against
`std::exp`/`std::sin` directly, plus S9's conditional footprint rows and R-9's restated stack-local
warning at ≈ 37.9 KB with both tables.

**Sequencing.** The perf TU is written and run as soon as the step function is complete and before
any behaviour criterion is tuned (S15 order). The stage probe (`EcosystemEngine_StageCostProbe`,
`WARN`-reported, `REQUIRE`s only finiteness and positivity) breaks the step into **pair loop /
grazing loop / integrate+movement / publication**, at `stepIntervalChunks` 1, 8 and 64, plus the
defaults configuration and an all-dormant configuration (FR-072 predicts all-dormant is **not**
cheaper — the probe is where that prediction is either confirmed or found wrong). **L1–L3** are then
applied from the measured table; **L4, L5 and L6 go to the user with the table** and are not adopted
without sign-off. Every lever adopted is recorded in the compliance pass as a plan deviation
(S14 D-M for L4/L5), with the SC-002/SC-004 figures re-measured after it, the accuracy arm added,
and S9's conditional footprint row moved into the main ledger in the same commit.

---

## S13. Risks and mitigations

| # | Risk | Mitigation |
|---|---|---|
| **R-1** | **SC-011 (b) misses by ~9×** (S12). The worst case is pinned by the spec (48 agents × 96 cells × σ=0.35) and `stepIntervalChunks = 1` is reachable through the documented `PrepareConfig`. | S12.3's ordered levers, measured first. If L1–L5 are not enough, FR-085's named escalation (narrow FR-082's minimum) goes to the **user** with the table. Never a threshold move. |
| **R-2** | **SC-002's defaults figures were measured against a different kind histogram** (FR-011 stratifies; the prototype drew i.i.d.). Activity could land below the 0.30 gate for a reason that is a *design decision*, not a defect. | The spec already flags this. `EcosystemEngine_KindAssignmentIsStratified` (S10.4) **gates** the deal, the ≤ 1 spread, the ≥ 1-per-kind guarantee above `agentCount = 5` and the shuffle's decorrelation — printing the histogram was never a check, and without the gate an implementation that silently kept the i.i.d. draw, or dealt without shuffling, passed every criterion. SC-002 then prints the measured activity, frozen count and per-kind histogram; a deviation is surfaced under FR-085 with the histogram attached, so the user can decide between adjusting the gate and reverting stratification. |
| **R-3** | **A `min(1, spare/want)` paraphrase of FR-023** looks equivalent and is not: it yields a negative scale below the floor, reversing every flow in the pair — and the reversal is *antisymmetric*, so SC-001 cannot see it. | The exact ternary is in S4.3 and in the header verbatim, with the two guards named. SC-020 gates it at probe level with a configuration where both branches are the common case. |
| **R-4** | **Denormal cell energies.** The prototype reached `1e-234`; on x86 with FTZ/DAZ enabled by the test main the behaviour differs between the test binary and a plugin host that has not set MXCSR. | FR-043's snap is unconditional and in `double` (FTZ applies to SSE scalar doubles too, but the snap fires at 1e-30, far above the subnormal threshold, so behaviour is identical with and without FTZ). SC-021 asserts exactly `0.0` and that the snapped amount reached the pool. |
| **R-5** | **SC-019's `0.5 ± 0.05` anchor depends on the agents' share of the budget**, not only on FR-061's arithmetic: the population mean output is `0.5 · (Σe)/B`, and at steady state agents hold ~96 % of the budget, so the expected value is ~0.48. A configuration that shifts the pool/agent split (higher `leakRate`, lower `regenRate`) moves the mean without any anchor defect. | SC-019 **reports** `Σe/B` per cell alongside the mean, so a miss is immediately attributable. The nine cells are all at the *defaults* rule set, so the split is constant across them by construction — the criterion tests the anchor, and the report proves it. |
| **R-6** | **`float` vs `double` RNG.** `Xorshift32::nextUnipolar()` is `float`; the prototype is `double`. Using the shipped accessor would make every prototype figure incomparable for a reason unrelated to the rules. | S1.6's `nextUnipolarD`/`nextBipolarD`/`rangeD` over the shipped `next()`. `random.h` is not modified (FR-090). |
| **R-7** | **Conditional RNG draws** (the prototype draws the drift noise only when `freqDrift > 0`) make the stream position a function of a runtime knob, breaking SC-006/SC-008 the moment a test or a macro toggles it. | A-8: the draw is unconditional, application is conditional (`bloom_engine.h:914-916` rule). Behaviour at the defaults is unchanged. |
| **R-8** | **Portability.** MSVC accepts `std::size_t`/`double` narrowing in brace init that Clang rejects; `-ffast-math` on the macOS leg folds `std::isnan`. | `PrepareConfig` is designated-initialiser-only and documented as such; `detail::isFinite` only, enforced by `lint-nonfinite-symbols.js`; `node tools/check-portability.js` before commit; the WSL g++ probe for any doubt about libstdc++ (`std::span` in the test helper, `std::array` CTAD). No SIMD is introduced, so the aligned-load lint is vacuous but must pass. |
| **R-9** | **The 21.5 KB object as a stack local** in a test or a Phase-10 voice — **≈ 37.9 KB if both S12.3 LUTs are adopted** (S9's conditional rows). | Documented in the header, with S9's table as the single footprint authority (S1.5 states no competing total); SC-007 constructs it outside the `AllocationScope`. If L4/L5 are adopted, the header line and S9's ledger are updated in the same commit as the lever (S14 D-M). |
| **R-10** | **SC-001's 30-minute budget.** The projection (S10.5) lands at ~25 min in Release with L1 in place; without the two-stage cutoff it is ~3× that. A Debug build is 10–50× slower and will never fit. | L1 is part of the design, not a lever. The test prints its own wall clock and the `[long]` lane is Release-only. A miss is surfaced with the measured table; the config count (the roadmap's 1000) and the duration (900 s, the reference's own) are not to be shrunk. |
| **R-11** | **Two probe structs, one header.** A future edit that defines `EcosystemEngineInspectProbe` in a second TU is an ODR violation the linker may not diagnose. | Each probe's forward declaration in the header names its **one** defining TU by path, the `bloom_engine.h:150-162` form. |

---

## S14. Spec corrections and open items

Each of these is a place where this plan deviates from the spec's letter. All are recorded here so
the compliance pass cites the deviation rather than discovering it.

* **D-A — `Agent` is not a nested struct.** The spec's new-components table lists
  `EcosystemEngine::Agent` (private nested). This plan uses structure-of-arrays (S1.5) because the
  cell loop — the dominant cost — scans one attribute across all agents 96 times per step. No
  `Agent` type is introduced, so the ODR footprint is *smaller* than the spec assumed: exactly one
  namespace-scope name.
* **D-B — FR-087 does not list the FR-043 denormal snap.** This plan places it immediately after
  grazing (stage 6b, S4.6), which is the only point at which a cell can have just been driven
  sub-guard and is before the next step's regrowth reads `room`. Its delta joins `poolDelta` and is
  committed at stage 12.
* **D-C — FR-083's repair arithmetic is unimplementable as written.** "Charging the difference
  between the pre-repair and post-repair totals to the pool" cannot be computed when the pre-repair
  total is NaN. The operative form (S7.2) charges `energyBudget_ − (post-repair total)`, which
  re-establishes the invariant SC-001 (c) gates. Intent preserved; arithmetic corrected.
* **D-D — one test-local helper header is added** (`ecosystem_metrics_test_helpers.h`, S10.1),
  beyond FR-091's "four new test TUs". It needs no CMake edit and has two in-tree precedents. The
  alternative is duplicating ~150 lines of statistics across two TUs, which is how two copies of a
  metric drift apart.
* **D-E — the metric code does not use `tests/test_helpers/statistical_utils.h`.** Its
  `computeMean`/`computeStdDev`/`computeVariance` are `float`-only (`:41`, `:59`, `:76`); SC-002's
  activity statistic is a std/mean ratio on energies of order 0.03 over 1800 samples, where `float`
  accumulation loses the discrimination the 0.30 gate needs. The helper is read and deliberately not
  consumed; the plan's `meanD`/`stdDevD`/`pearsonD` are `double` and live in D-D's header.
* **D-F — FR-091's file list omits `dsp/lint_all_headers.cpp`.** One include line must be added
  (S11.3) or SC-015's clang-tidy clause never sees the new header. Every prior Vorago phase edited
  this file (`:179`, `:182`, `:185`, `:188`, `:191`).
* **D-G — SC-012 (a) says "the number of numeric fields in `PrepareConfig`".** `PrepareConfig`
  carries only FR-006's five prepare-time knobs (S1.3), so the clause as written would assert
  coverage of five knobs while the failure it exists to prevent (`predation` silently at its
  exchange-disabling 0.5) concerns a *runtime* knob. The knob list is asserted against the **full
  28-knob Appendix-A set** via the new `kConfigKnobCount` (A-2). This strengthens the criterion; it
  does not relax it.
* **D-H — share-unit setters are named `…Shares`** (A-4). FR-008 makes the unit part of the
  contract and a bare `setCapacity(float)` would read as absolute energy at every call site.
* **D-I — `getControlStepCount()` is added** (A-3). SC-010 (b) asserts the step count to within one
  step and FR-061's per-agent clamped-step *fraction* needs a denominator; neither is readable
  without it.
* **D-J — `lint-layers.js` cannot enforce FR-001.** It only forbids upward includes (`:5-8`), so
  "Layer 0 + stdlib only" is enforced by an in-TU literal check on the header's include block
  (S10.4, SC-015 row). SC-015's wording ("Layer 3 includes only Layer 0 + stdlib") attributes to the
  lint a capability it does not have.
* **D-K — SC-002's runtime may cross the `[long]` threshold.** Projected ~8 s (S10.5); the
  convention is > ~15 s. If measurement says otherwise, the case is tagged `[long]` and moved to the
  longrun TU as a recorded deviation.
* **D-L — FR-071's negative branch, as written, inverts sign inside the refuge and pays the agent
  out of the pool.** The spec's closed formula is
  `delta = max(a · (e_i − preyFloor), −e_i)` for `a < 0`. When `e_i < preyFloor` the inner term is
  negative, `a × negative` is positive, and `max` selects it: a "take energy" call **gives** the
  agent up to `|a| · preyFloor`, withdrawn from a pool that has no `max(0, pool)` guard on this
  branch (the positive branch has one). That drives `pool_` negative — which S4.10 counts as a
  conservation violation and this plan calls a real design error — and at steady state the pool *is*
  empty (agents hold ~96 % of the budget), so it is the normal case. SC-001 (d) aims calls at "an
  agent at energy 0" with amounts over `[-2, +2]`, so `perturbAgent(i, -1.0f)` there yields
  `delta = +preyFloorAbs_` and clause (b)'s `getConservationViolationCount() == 0` fails **by
  construction**. The operative form (S6.3) floors the giving term before applying the sign:
  `std::max(a * std::max(0.0, energy_[i] - preyFloorAbs_), -energy_[i])`, so `delta ∈ [−e_i, 0]` on
  every path. This is what the spec's own stated consequences already claim ("an agent at or below
  `preyFloor` gives nothing — the refuge is respected"; "`−e_i` is the hard floor"), so the
  correction restores the FR's intent rather than changing it. **This is a spec defect, not only a
  plan one**: the same formula is in `spec.md` FR-071 and should be corrected there. Regression:
  `EcosystemEngine_PerturbAgentConservesUnderFuzz`'s below-floor clause (S10.4).
* **D-M — S12.3's L4 and L5 are deviations from normative formulas, conditional on user sign-off.**
  L4 replaces FR-012's `w = exp(−d²/2σ²)` with a 1025-entry LUT + lerp; L5 replaces FR-050's
  `sin(2π·phase)` appetite gate and FR-035's Kuramoto `sin` with a 1024-entry LUT. The spec states
  both formulas as normative and its Assumption 1 ("the prototype's rules transfer to C++
  unchanged") rests on them, so neither is an ordinary lever an implementer may take on the strength
  of a perf table alone. They are routed exactly as L6 is: measured table to the user, adoption on
  sign-off. **If adopted**, three things land in the same commit as the lever —
  `EcosystemEngine_ApproximationTablesAreAccurate` (S10.4) asserting
  `max |LUT(u) − exp(−u)| / exp(−u) <= 2.3e-5` over `[0, 13.8155]` and
  `max |LUT(φ) − sin(2πφ)| <= 1e-5` over `[0, 1)`; S9's conditional footprint rows moved into the
  main table (≈ 21.5 KB → ≈ 29.7 KB with one, ≈ 37.9 KB with both); and R-9's stack-local line
  restated against the new figure. Re-measuring SC-002/SC-004 is *not* the gate — their ±30 % and
  ≤ 0.35 margins cannot see a 2e-5 error, correct or incorrect.
* **D-N — SC-020's clause is stated per agent, which needed probe surface the plan did not have**
  (A-10). `outflow(i)` and `divided(i)` are added to `EcosystemEngineInspectProbe`, and
  `clampedSteps(i)` for SC-002 (d)'s window. Without them SC-020's "no division is performed when
  `want_i == 0`" is readable only through a cumulative counter — meaningful only on a step where no
  agent divides, which the defaults arm is not — and SC-002 (d)'s "across the late window" is
  readable only as a whole-run cumulative fraction. Both criteria are **strengthened**, not relaxed;
  the deviation is the added private member `divided_` (48 bytes) and three probe accessors.
* **D-O — four FR-level test cases with no SC of their own** (S10.4): FR-064's value clamp
  (`EcosystemEngine_SettersClampToRange`), FR-011/FR-060's stratified draw
  (`EcosystemEngine_KindAssignmentIsStratified`), FR-021's `predation = 0.5`
  (`EcosystemEngine_ExchangeDisabledAtPredationHalf`) and the S4.5 negative-pool regression
  (`EcosystemEngine_NegativePoolIsNotAbsorbedByAnAgent`, filed as SC-009 (d)). Each gates a
  normative clause that no listed criterion reached; all four are additions to the spec's criterion
  set, none relaxes anything. FR-064's clamp in particular is load-bearing for S7.3's containment
  argument, which leans on `sigmaSq_ > 0` *because* `kernelSigma_ >= 0.01`.
* **D-P — SC-001 (d)'s perturb schedule runs inside the 1000-config batch**, not as a 100-config
  sub-batch. The sub-batch was a 10× shrink of a spec-stated workload ("For every configuration
  assert: (a)…(d) with a seeded `perturbAgent` schedule running throughout") that FR-085's inherited
  stop-and-surface rule forbids, and S10.5 had costed the batch without it. Folding the schedule in
  costs ~2 % of the batch instead of 8.4 M extra steps, so the coverage goes from 10 % of
  configurations to 100 % *and* the ledger becomes honest. The spec's second named case,
  `EcosystemEngine_PerturbAgentConservesUnderFuzz`, is kept and re-aimed at FR-071's closed-formula
  edges (capacity, zero energy, below refuge floor, negative pool, out-of-range index, non-finite
  amount) — the coverage a random schedule reaches only by luck, and the D-L regression.

**No open questions remain for the user at plan time.** OQ-1 through OQ-5 are resolved in the spec's
Clarifications and are encoded above (strip resource field, `stepIntervalChunks` default 8 / range
[1, 64], 32 agents / `kMaxAgents = 48`, no `ModulationSource` adapter, dormancy gates the output).
The decisions this plan *expects* to escalate are all downstream of the **measured** perf table, and
none before it: **L4 and L5** (approximating FR-012's kernel and FR-050/FR-035's sines, D-M) and, if
those are not enough, **FR-082's minimum** (L6). One correction is escalated *now* rather than
measured: **D-L**, FR-071's negative branch, which is wrong in the spec as well as in this plan's
first draft and should be fixed in `spec.md` in the same pass.

---

## S15. Suggested task order (`tasks.md` owns the real breakdown)

1. Header skeleton: constants, `Kind`, `PrepareConfig`, the SoA state block, the salt table, the two
   probe forward declarations and friends, the complete public API with every method a stub. Compile
   it into `lint_all_headers.cpp` and the four (empty) TUs; land the CMake edits **first** so no TU
   can silently drop out.
2. `prepare()` / `reset()` / `setSeed()` / `initialiseState()` (S2), the FR-008 conversion (S5), the
   clock (S3), and every setter with its FR-064 clamp (S1.4, S8 (iii)). Test: SC-006 (a)(c),
   SC-007 (including (b)'s knob-default clauses), SC-008, SC-010 (b), plus
   `EcosystemEngine_SettersClampToRange` and `EcosystemEngine_KindAssignmentIsStratified` —
   both belong here, not at the end: S7.3's containment argument depends on the clamp.
3. The thirteen-stage step (S4), in FR-087's order, one stage at a time, each with its prototype line
   in a comment. Test after stage 12: conservation on a short run, plus
   `EcosystemEngine_ExchangeDisabledAtPredationHalf` (stage 2) and
   `EcosystemEngine_NegativePoolIsNotAbsorbedByAnAgent` (stages 5 + 7 — write the `avail` floor
   outside the branch from the start, S4.5).
4. Publication, wake ramp, dormancy, `perturbAgent` (S4.11, S6 — S6.3's corrected negative branch,
   D-L). Test: SC-014 including the (e) hold arm, SC-019.
5. The non-finite ladder and both probes (S7). Test: SC-009, SC-020, SC-021.
6. **The perf TU and the stage probe (S12), run alone, before any behaviour tuning.** Decide the
   lever list from the measured table. This is the step whose outcome can change the phase.
7. The metric helper header and the behaviour criteria: SC-002, SC-004, SC-015's include arm.
8. The fuzz harness and the `[long]` set: SC-012 first (it gates the other two), then SC-001,
   SC-013, SC-003, SC-005, SC-017, SC-018.
9. Gates: the six lints, `check-portability.js`, clang-tidy, the five-suite regression, the
   byte-unchanged check on every header FR-090 names.

---

## Review notes (2026-09-15 revision pass)

**Nothing in the review was rejected.** Both blockers, all eight majors and all eleven minors are
applied above; the duplicated pairs (the `kExemptHostile` sentinel, the S1.5-vs-S9 footprint, the
S0 line-number slips) are each resolved once and cover both filings. No threshold, workload,
duration or config count was relaxed anywhere — SC-001's perturb coverage went *up* (100 → 1000
configurations, D-P), FR-064's clamp, FR-011's stratified draw, FR-021's `predation = 0.5` and
FR-062's held publication went from ungated to gated (D-O, SC-014 (e)), and SC-020 and SC-002 (d)
were made expressible at the strength they were written at rather than scoped down (D-N).

Three consequences of this pass reach outside `plan.md` and are flagged for the compliance step
rather than silently absorbed here:

1. **`spec.md` FR-071 carries the same defect as the plan's S6.3** (S14 **D-L**): the negative
   branch's `max(a · (e_i − preyFloor), −e_i)` inverts sign inside the refuge and pays the agent out
   of an unguarded pool. The plan is corrected; the spec's closed formula should be corrected in the
   same pass, or the two documents disagree on a line an implementer will copy verbatim.
2. **S12.3's L4 and L5 are no longer pre-authorised** (D-M). They are routed to the user with the
   measured perf table, exactly as L6 is, because they replace formulas the spec states as
   normative. If the measured table clears the budget with L1–L3 alone, no escalation is needed.
3. **The spec's criterion set is extended by four cases and one clause** (D-O, SC-014 (e)). These
   gate normative FR text that no existing SC reached; they are additions, not substitutions, and
   the compliance table should carry a row for each.
