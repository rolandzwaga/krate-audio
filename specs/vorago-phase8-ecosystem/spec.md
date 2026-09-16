# Feature Specification: Vorago Phase 8 — Ecosystem Engine

**Spec slug:** `vorago-phase8-ecosystem`
**Roadmap source:** `specs/Vorago-roadmap.md` → Part A → Phase 8 (lines 368–396); reuse-inventory row
`Ecosystem agents` (line 123); the ODR note (lines 127–129); the cross-cutting constraints
(lines 528–552), of which the Dormancy rule is lines 537–545 and names **Phase 8 agents** explicitly
(line 542); the dependency graph (lines 508–521); roadmap **Open Question 2** (lines 557–558), the
one item the roadmap defers *to this spec*. Every roadmap line number below was read and re-verified
against `specs/Vorago-roadmap.md` this session.
**Layer:** one new Layer 3 component, `dsp/include/krate/dsp/systems/ecosystem_engine.h`
(roadmap line 379).
**Test target:** `dsp_systems_tests` — an **enumerated, not globbed** source list opening at
`add_executable(dsp_systems_tests` (`dsp/tests/CMakeLists.txt:324`) and closing at `:477`; the list
says so itself at `:465-466` ("an unregistered TU silently drops out of the build and its cases never
run"). The `-fno-fast-math` block opens at `dsp/tests/CMakeLists.txt:569`
(`if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")`) and its
`PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"` is at `:921`.
**Depends on:** nothing from Vorago Phases 2–7 at compile time. This component includes **no** Vorago
header; it composes Layer 0 (`core/random.h`, `core/db_utils.h`) plus the standard library only. The
Phase 2–7 components are its *consumers*, and they already expose the plain-scalar hooks it writes
into — three of them name "a Phase-8 agent" in their own documentation
(`feedback_ecology.h:1249-1251`, `:2260-2262`, `:2294-2296`; `resonance_drift_network.h:765-766`).
**Plugin work:** none. The Vorago plugin starts at Phase 11; phases 1–10 are KrateDSP-only.
**De-risk gate: SATISFIED.** Roadmap lines 375–377 require an offline Node.js prototype *before* this
spec. It exists and is committed — `specs/vorago-phase8-ecosystem/prototype/{ecosystem-sim.js,
run.js, FINDINGS.md}` (commits `4f3eb105`, `14687a45`, `ec1bc900`) — and **this spec encodes its
round-2 rule set verbatim**, including the four places where the prototype **contradicts** the
roadmap's design sketch.

---

## Overview

The Ecosystem Engine is Vorago's identity layer: "agents, not modulation" (roadmap line 371). It is a
Layer 3, **control-rate only, audio-free** simulation of a fixed table of 24–48 tiny autonomous
entities living on a 2-D toroidal habitat. Each agent holds **energy**, occupies a position, carries
an intrinsic slow phase, and belongs to one of five kinds (partial / resonator / noise / feedback /
ghost — `prototype/ecosystem-sim.js:63`). Agents graze a **spatial resource field**, exchange energy
with neighbours, attract and repel one another, and leak back into a shared pool. Their energies,
normalised, are the modulation values every Phase 2–7 engine consumes.

**Energy lives in a closed system and total energy is invariant.** That is the whole boundedness
argument (roadmap lines 389–390), and the prototype's single most important finding is that
**the invariant is not structural for free**: an earlier design claimed it was, and fuzzing falsified
it twice (`prototype/FINDINGS.md:19-50`). 591 of 1000 configurations drove the pool negative because
two withdrawals each capped against a pool that did not know about the other; after that was fixed,
468 of 1000 still failed because an *antisymmetric* transfer can still ask an agent to give away more
than it holds, and clamping the shortfall to zero charges it to the pool. Both fixes are **normative
requirements here** (FR-023, FR-054), not implementation detail, because in C++ this class of defect
surfaces months later as a configuration-dependent energy leak under an audio-thread debugger.

Three further prototype findings overrule the roadmap's own text, and the spec follows the
measurement:

1. **A single global energy pool does not work** (roadmap line 389 implies one). Thirty-two agents
   feeding from one scalar produced mean pairwise output correlation **0.84–0.99** — one modulation
   shape copied thirty-two times, which is useless as the "bank of modulation sources" Phase 8 is
   supposed to deliver (`FINDINGS.md:59-68`). The resource must be **spatial**. FR-040.
2. **"Exchange energy" taken literally is an averaging operator** and destroys structure: measured
   distribution entropy 4.998 of a 5.000 maximum, adjacent seeds correlating at 0.9989
   (`FINDINGS.md:70-76`). The exchange rule survives only in a *mildly predatory* form behind a
   refuge floor. FR-020, FR-022.
3. **The roadmap's proposed success metrics do not measure what they claim** (`FINDINGS.md:125-174`).
   Shannon entropy of the energy distribution — roadmap line 394's non-triviality metric — is
   **permutation-invariant**: energy sloshing between agents in a fixed pattern holds it exactly
   constant while every agent's output swings, so it calls the liveliest case frozen. And a maximum
   autocorrelation over all lags always sits at the shortest lag scanned, so the first cycle detector
   reported "limit cycle at 9.0 s" for a 30-minute run whose scan simply started at 9 s — it was
   measuring smoothness. Entropy is kept as a **reported, non-gating** statistic (FR-066); the gates
   are per-agent activity, inter-agent decorrelation and a **recurrence-based** cycle test
   (SC-002, SC-004, SC-005).

A fourth correction concerns the consumer contract. Roadmap lines 384–388 assert the ecosystem "*is*
a bank of `ModulationSource`s, so every engine from phases 2–7 hooks in without new plumbing". Read
against the real headers this is **false at 24–48 agents**: `ModulationEngine` has no external
`ModulationSource` registration at all — its source roster is a closed enum and its only injection
point is `void setExternalSourceValue(uint8_t slot, float value) noexcept`
(`systems/modulation_engine.h:552`) with `kMaxExternalSources = 4` (`core/modulation_types.h:60`);
`VoiceModRouter` carries a closed 8-entry `VoiceModSource` enum (`systems/voice_mod_types.h:29-38`)
with no external hook; `ModulationMatrix::registerSource(uint8_t id, ModulationSource* source)`
(`systems/modulation_matrix.h:180`) accepts pointers but caps at `kMaxModulationSources = 16`
(`:41`). What *is* true — verified — is that every Phase 2–7 consumer takes a **plain scalar in
[0, 1]** and was written anticipating this component: `NoiseOrganism::setSourceWake`
(`noise_organism.h:844`), `ResonanceDriftNetwork::setPeakWake` (`resonance_drift_network.h:771`),
`FeedbackEcology::setLoopWake` (`feedback_ecology.h:1261`) and `setCoupling` (`:1107`). So the
ecosystem ships an **indexed read surface** (FR-060) and Phase 10 writes those scalars. No new
plumbing is needed — but "it is a bank of `ModulationSource`s" is not the mechanism, and pretending
otherwise would have produced a 48-way adapter nothing can consume. OQ-4 puts the adapter question to
the user rather than building it speculatively.

---

## Scope

In scope:

- One new Layer 3 component, `EcosystemEngine`, at
  `dsp/include/krate/dsp/systems/ecosystem_engine.h` (roadmap line 379), header-only, **no audio
  path**, control-rate only (roadmap line 383: "Control-rate only (per-block): sense neighbours'
  energy → apply rules → write outputs").
- A **fixed-capacity agent table** of 24–48 agents (roadmap line 381), each
  `{kind, energy, position, phase, intrinsic frequency, wake}` (roadmap lines 381–382).
- The **five rules proven by the prototype** (`FINDINGS.md:304-312`, ablation table `:260-276`):
  exchange (mildly predatory, refuge-floored), attract/repel + crowding, movement (affinity +
  foraging), metabolism (phase-gated appetite, demand-scaled grazing, proportional regrowth,
  nonlinear leak, carrying capacity), and synchronization **as an off-by-default knob**.
- The **spatial resource field** that replaced the roadmap's single pool (`FINDINGS.md:59-68`).
- The **conservation constraint** (roadmap lines 389–390) with its two guards (`FINDINGS.md:19-50`)
  and the refuge floor (`FINDINGS.md:224-226`), enforced as requirements and asserted as criteria.
- **Determinism under seed** (roadmap line 391) using the repo's own `Xorshift32` +
  `deriveStreamSeed` (`core/random.h:41`, `:102`) — the prototype's RNG is a bit-exact port of that
  header (`FINDINGS.md:14-15`), so seeds and stream splitting transfer unchanged.
- The **indexed output surface** consumed by Phase 10 (roadmap lines 384–388), plus per-agent
  sleep/wake inheriting the cross-cutting Dormancy rule (roadmap line 542 names Phase 8 agents).
- Unit tests covering the roadmap's four Phase-8 success criteria (lines 393–396) plus the
  cross-cutting gates (lines 528–552), each as a numbered criterion, and a **C++ fuzz harness with a
  structural coverage assertion** (`FINDINGS.md:327-328`).

## Non-Goals (owned by later phases, or deliberately excluded)

- **Any consumer mapping.** Which agent drives which resonator peak, noise source, ecology loop,
  cloud partial cluster or ghost trigger — and with what depth and curve — is **Phase 10**
  (roadmap lines 424–446). This component publishes numbers; it includes no consumer header and
  knows nothing about audio. The roadmap's parenthetical examples (lines 386–388) are documented in
  the read surface's doxygen as *intended use*, not implemented here.
- **Musical calibration of activity.** The prototype states plainly that this "is NOT settled here
  and cannot be, offline" (`FINDINGS.md:316-318`): late activity 0.44 means each agent's energy
  swings ±44 % of the population mean over ten minutes, and whether that is the right modulation
  depth is a listening decision belonging to the consumer mapping. Phase 10 owns it.
- **Owning a `SlowEventScheduler`.** House rule, verified: `noise_organism.h:841-843` ("Vorago Phase
  10 owns the `SlowEventScheduler`"); `resonance_drift_network.h:765-766` repeats it. This component
  takes a plain scalar (`setAgentWake`) and offers a plain trigger (`perturbAgent`), never a
  scheduler reference. `slow_event_scheduler.h` is **not** included.
- **Deriving from `ModulationSource`.** One object publishes N values; the interface publishes one
  (`core/modulation_source.h:37`, `getCurrentValue()`). Following `NoiseOrganism`,
  `ResonanceDriftNetwork`, `FeedbackEcology`, `SubharmonicEngine` and `BloomEngine`, none of which
  derive from it. See OQ-4.
- **Amending any shipped header.** FR-090 requires every consumer header byte-unchanged and every
  Seraphis/Vorago suite green (roadmap lines 550–552).
- **A visualization feed.** The ecosystem view is Phase 13 (roadmap lines 488–496) and rides the
  Membrum `MetersBlock` DataExchange pattern; this component only has to make the state *readable*
  (FR-060), which it does.
- **Voice instancing and count.** Per-voice vs shared, 4/6/8 voices — Phase 10 (roadmap OQ 5,
  line 565). The budget here is stated per instance, as the roadmap does (line 395).

---

## Prototype provenance (the rule set this spec encodes)

Roadmap lines 375–377: "The spec then encodes the proven rules." The mapping is one-to-one, and every
row was re-read in `prototype/ecosystem-sim.js` this session.

| Rule / constant | Prototype site | Encoded by |
|---|---|---|
| Agent kinds (5) | `:63` `['partial','resonator','noise','feedback','ghost']` | FR-011 |
| Default configuration | `:70` `defaultConfig()` | FR-005, Appendix A |
| Affinity matrix default | `:154` `defaultAffinity()` — same kind `-1.0`, other kinds `+0.45` | FR-031 |
| Seeded per-attribute streams | `:207-213`, `:219` (salts 0–6, `deriveStreamSeed`) | FR-080 |
| Seeded **initial resource fill** | `:219-232` — an empty field gave every seed the same opening transient and cross-seed correlation 0.60 | FR-041 |
| Neighbour kernel + cutoff | `:274-275` `w = exp(-d²/2σ²)`, skip `w < 1e-6` | FR-012 |
| RULE 1 exchange flow | `:285` `flow = exchangeRate·w·(e_j − e_i)·(1 − 2·predation)` | FR-020 |
| Exchange second pass (Guard 2) | `:334-347` `scale_i = spare_i/want_i`, pair scaled by `min(scale_i, scale_j)` | FR-023 |
| Refuge floor | `:340` `spare = e_i − preyFloor` | FR-022 |
| RULE 2 affinity force | `:292-300` force ∝ neighbour **energy** | FR-030 |
| Crowding repulsion | `:307-315` kind- and energy-independent, inside `crowdingRadius` | FR-032 |
| RULE 3 Kuramoto sync | `:317-322`, default `syncRate = 0` | FR-035 |
| RULE 4 appetite gate | `:355-360` `gate = 1 + appetiteDepth·sin(2π·phase)`, clamped ≥ 0 | FR-050 |
| Proportional regrowth | `:386-400` sum desired first, scale by what the pool can pay | FR-053 |
| Single withdrawal budget (Guard 1) | `:381-400`, `:455-461` one running `avail` balance | FR-054 |
| Demand-scaled grazing | `:404-451` `ask = grazeRate·res_k·demand·dt`, cell caps the total | FR-051 |
| Foraging gradient | `:428` `(res_k/cellCapacity)·w·(d/σ²)` | FR-033 |
| Nonlinear leak | `:466` `leak = leakRate·e^leakExponent·dt` | FR-052 |
| Carrying capacity spill | `:484-488` excess returns **to the pool** | FR-055 |
| Negative clamp returns overdraw | `:478-483` | FR-055 |
| Pool deliberately **not** clamped | `:513-521` a violation must be observable, not papered over | FR-056 |
| Bounded OU drift on intrinsic freq | `:503-510` anti-limit-cycle | FR-013 |
| Liveness thresholds (the **verdict function**, for classifying fuzz configs — *not* the defaults gate) | `:570-583` late window 600 s, frozen < 0.02, alive ≥ 0.10 with ≤ 25 % frozen, cycle > 0.8 | SC-002 (verdict function), consumed by SC-003, SC-010, SC-013; SC-005 |
| Hostile fuzz box | `run.js:302-332` | SC-001 |
| Sane (macro-reachable) fuzz box | `run.js:338-377` | SC-013 |
| Coverage assertion | `run.js:388-404` a knob that never varies **fails the run** | SC-012 |
| Seed-blindness at 1.5× the floor | `run.js:606-628` | SC-004 |

**Two prototype-only defects are NOT carried over**, recorded so a reader does not reproduce them:
(a) `pathLength` accumulates `min(|vx|, maxStep)` only (`:492`) — a 2-D path under-reported through
its x component; it is a diagnostic, never a gate. (b) The resource field is **one-dimensional** —
`cellPos[k] = (k + 0.5)/resourceCells` (`:228`), grazing weights the **x separation only** (`:421`)
and foraging is applied to `vx` only (`:490`) — so in the 2-D habitat the field is a set of vertical
strips. That geometry is what every published number was measured on, so FR-040 encodes it as
proven and OQ-1 puts the 2-D-grid alternative to the user rather than shipping an untuned rule.

---

## Existing components (verified this session)

Every row was opened and read in this session; signatures are quoted from the file.

| Component | Header (verified) | What Phase 8 reuses / relies on |
|---|---|---|
| `Xorshift32` / `deriveStreamSeed` (L0) | `core/random.h:41` / `:102` | **Consumed.** `explicit constexpr Xorshift32(uint32_t seedValue = 1) noexcept` (`:45`), `[[nodiscard]] constexpr uint32_t next() noexcept` (`:50`), `nextFloat()` bipolar (`:59`), `nextUnipolar()` (`:67`), `constexpr void seed(uint32_t)` (`:72`), `state()` (`:80`); `[[nodiscard]] constexpr std::uint32_t deriveStreamSeed(std::uint32_t base, std::size_t salt) noexcept` (`:102`). Load-bearing: `seed()` substitutes its default for 0 (`:72-74`) so two lanes hashing to 0 would **collapse onto one stream** — which is why FR-080 derives every lane through `deriveStreamSeed`. The prototype is a bit-exact port of both (`FINDINGS.md:14-15`), so its seeds transfer. |
| `detail::isNaN` / `isInf` (L0) | `core/db_utils.h:99`, `:260` | **Consumed.** The `-ffast-math`-proof finiteness tests (bit pattern behind an opaque barrier). FR-083 forbids `std::isnan`/`std::isinf`/`std::isfinite`; `tools/lint-nonfinite-symbols.js` enforces it. |
| `ModulationEngine` (L3) | `systems/modulation_engine.h:79` | **Named by reuse row line 123 ("routing"); read; NOT consumed and NOT modified.** Its source roster is the closed `enum class ModSource` (`core/modulation_types.h:36-57`, `kModSourceCount = 18`, `:63`); there is **no** API taking a `ModulationSource*`. The only injection point is `void setExternalSourceValue(uint8_t slot, float value) noexcept` (`:552`) over `kMaxExternalSources = 4` (`modulation_types.h:60`) — four slots against 24–48 agents. It is therefore a **Phase-10 routing option for a handful of aggregate lanes**, not this component's output contract. Overview correction 4. |
| `VoiceModRouter` (L3) | `systems/voice_mod_router.h:58` | **Read; NOT consumed, NOT modified.** `static constexpr int kMaxRoutes = 16` (`:61`), `void setRoute(int index, VoiceModRoute route) noexcept` (`:87`), `[[nodiscard]] float getOffset(VoiceModDest dest) const noexcept` (`:213`); sources are the closed `enum class VoiceModSource : uint8_t` of 8 (`voice_mod_types.h:29-38`) and destinations the closed `VoiceModDest` of 10 (`:49-61`) — neither carries an external hook. Same correction. |
| `ModulationMatrix` (L3) | `systems/modulation_matrix.h:41`, `:180` | **Read; not consumed (see OQ-4).** `bool registerSource(uint8_t id, ModulationSource* source) noexcept` (`:180`) over `kMaxModulationSources = 16` (`:41`) — the one generic `ModulationSource*` consumer in the tree, and still smaller than the agent table. |
| `ModulationSource` (L0) | `core/modulation_source.h:31` | **Read; deliberately not inherited.** `[[nodiscard]] virtual float getCurrentValue() const noexcept = 0` (`:37`), `[[nodiscard]] virtual std::pair<float,float> getSourceRange() const noexcept = 0` (`:41`) — one value per object. Fourteen implementers exist (`brownian_drift.h:94`, `tidal_modulator.h:122`, `perlin_noise_source.h:174`, `slow_event_scheduler.h:143`, …); all are single-lane. OQ-4. |
| `EnvelopeFollower` (L2) | `processors/envelope_follower.h:82` | **Named by reuse row line 123 ("energy sensing"); read; NOT consumed.** `[[nodiscard]] float processSample(float input) noexcept` (`:164`), `getCurrentValue()` (`:192`), `setAttackTime`/`setReleaseTime` in ms (`:220`, `kMinAttackMs = 0.1f … kMaxReleaseMs = 5000.0f`, `:88-91`). It senses an **audio** envelope in milliseconds; this component has no audio input and its slowest agent period is 667 s (`ecosystem-sim.js:137`). The agent's *own* energy is already a smooth state variable, so wrapping a millisecond-scale audio follower around it would add a second time constant with nothing to sense. D-3. |
| `SlowEventScheduler` (L2) | `processors/slow_event_scheduler.h:143` | **Read; not included.** `class SlowEventScheduler final : public ModulationSource` with `void prepare(double sampleRate) noexcept` (`:207`), `setSeed` (`:229`), `processBlock(std::size_t numSamples)` (`:304`), `getEnvelopeValue()` (`:391`), `getActiveDepth()` (`:397`). The Phase-1 precedent for a seeded, RT-safe, slow stochastic component — and the thing Phase 10 will wire into `perturbAgent` (FR-071). Not a compile dependency. |
| `NoiseOrganism` (L3) | `systems/noise_organism.h:142,150,178,190-195,833,844,880,999` | **Consumer + house style**, read, not included. `kMaxSources = 4` (`:142`); `kControlChunkSamples` pinned at 64 (`:150`); `kGainRampMs = 50.0f` (`:178`) — the Dormancy rule's 50 ms re-entry fade (roadmap lines 537–540); designated-initialiser-only nested `struct PrepareConfig` (`:190-195`); `void setSourceDormant(std::size_t slot, bool dormant)` (`:833`) and `void setSourceWake(std::size_t slot, float amount)` (`:844`, "A plain scalar input, not a scheduler reference", `:841-843`); `getAllocatedBytes()` (`:999`). |
| `ResonanceDriftNetwork` (L3) | `systems/resonance_drift_network.h:128,765-771,784,852,904` | **Consumer + the sleep/wake precedent**, read, not included. `static constexpr std::size_t kMaxPeaks = 12` (`:128`). `void setPeakWake(std::size_t peak, float amount) noexcept` (`:771`) whose doxygen names this phase directly — "from agent energy, or a Phase-10 caller writing `getEnvelopeValue() * getActiveDepth()`, that passed 1.5 would scale one peak 50 % past unity … Clamping in the setter is the only place that cannot be bypassed" (`:765-770`) — and `setPeakDormant` (`:784`), the two "behaviourally identical" (`:787-790`). `getClampEngagementCount()` (`:904`) is the diagnostic-counter idiom FR-056, FR-061 (`getOutputClampEngagementCount`) and FR-083 (`getNonFiniteContainmentCount`) copy — one counter per meaning, never one counter for two. |
| `FeedbackEcology` (L3) | `systems/feedback_ecology.h:193,1107,1249-1265,2268-2296` | **Consumer**, read, not included. `static constexpr std::size_t kMaxLoops = 6` (`:193`); `void setCoupling(std::size_t from, std::size_t to, float amount)` (`:1107`) — the roadmap's "a feedback agent opens an ecology loop's coupling" (line 388); `void setLoopWake(std::size_t loop, float amount)` (`:1261`) whose doxygen states "a Phase-8 agent driving wake from energy … must not be able to push a loop past unity or invert it" (`:1249-1251`). Two warnings this spec obeys: the `kWakeSilenceEpsilon` snap at `gateSteady()` (`:2268-2273`) exists because "a release tail that stops at 1e-8 must not leave a loop burning forever" (`:2260-2262`), and `refreshGates()`'s `target != lastGateTarget` guard is **load-bearing, not an optimisation**, because "a Phase-8 agent writing one loop's wake per block would otherwise stretch every OTHER loop's ramp without bound" (`:2290-2296`). FR-062 therefore fixes the publication rate and FR-063 snaps sub-epsilon outputs to exactly zero **at the source**. |
| `HarmonicCloud` (L3) | `systems/harmonic_cloud.h:138`, `:144` | **Consumer (partial-cluster agents), read, not included.** `static constexpr std::size_t kMaxPartials = 64` (`:138`), `kControlChunkSamples = 64` (`:144`). |
| `AtmosphereEngine` (L3) | `systems/atmosphere_engine.h:828`, `:982`, `:1013` | **Consumer (ghost agents), read, not included.** `void setDensity(float grainsPerSecond)` (`:828`), `setLevel(float)` (`:982`), `setSeed(std::uint32_t)` (`:1013`) — the scalar surface a ghost agent's energy drives in Phase 10. |
| `BloomEngine` (L3) | `systems/bloom_engine.h:221,338,383,494,637-663,817,893-916` | **The current house template**, read, not included. `static constexpr std::size_t kControlChunkSamples = 64;` + live `static_assert` (`:221-222`); nested `struct PrepareConfig` (`:338`); `void prepare(double sampleRate, const PrepareConfig& config) noexcept` (`:383`); `[[nodiscard]] std::size_t processChunk(float*, float*, std::size_t, std::size_t numSamples) noexcept` (`:494`) with "`numSamples == 0` applies the current state WITHOUT advancing"; `setDormant`/`setWake`/`triggerBloom` (`:637`, `:648`, `:663`); `getAllocatedBytes()` returning 0 (`:817`); and the **absolute-residue control clock** carried across calls, whose failure mode is documented at `:899` ("`numSamples / kControlChunkSamples` … is the natural mistake") — FR-081 adopts it verbatim. |
| Test helpers | `tests/test_helpers/` | `render_fingerprint.h:58 kSampleTolerance = 5.0e-4f`, `:61 kMetricTolerance = 2.5e-4`, `:63 struct RenderFingerprint`, `:122 compareFingerprints` (SC-006 — **no bit-exact goldens**, roadmap line 546); `allocation_detector.h:48 AllocationDetector`, `:111 AllocationScope` (SC-007); `statistical_utils.h:41 computeMean`, `:76 computeStdDev`, `:90 computeMedian` (SC-002, SC-004, SC-005). |
| Perf-test idiom | `dsp/tests/unit/systems/resonance_drift_network_perf_test.cpp:50-90` | The measurement basis SC-011 inherits: **nanoseconds per 512-sample block at 48 kHz** ("A percent-of-core figure is not reproducible across dev machines or CI runners", `:66-71`), best-of-25 × 500 blocks after 400 warm-up blocks (`:77-78`), tagged `[.perf]`, **run alone** (`:83-86`). One 512-sample block period at 48 kHz is **10 666 667 ns**. The **stop-and-surface rule** (`:57-64`) — "NO IMPLEMENTING AGENT MAY lower [a capacity], raise [a budget], relax a threshold, or shrink a workload to make a figure fit. Reduce cost, never move the line." — is inherited verbatim by FR-085. |

---

## New components

| Class | Layer | Header | ODR sweep result (run this session) |
|---|---|---|---|
| `EcosystemEngine` | 3 | `dsp/include/krate/dsp/systems/ecosystem_engine.h` | **Clean.** `grep -rn "\(class\|struct\|enum class\|enum\) EcosystemEngine\b" dsp/ plugins/ tools/` → no matches. `grep -rn "Ecosystem" dsp/include dsp/tests plugins/ --include=*.h --include=*.cpp` → **no matches anywhere in the tree**. The file does not exist. |
| `EcosystemEngine::Kind` (nested enum) | 3 | same file | **Clean.** No namespace-scope `AgentKind`/`Kind` is claimed; `grep` for `enum class AgentKind` → none. `tools/lint-odr.js:20-21` qualifies nested types by their enclosing class, so a nested `Kind` collides with nothing. |
| `EcosystemEngine::PrepareConfig` (nested struct) | 3 | same file | **Clean by construction** — nested, same lint rule. Matches the house pattern (`noise_organism.h:190`, `resonance_drift_network.h:297`, `bloom_engine.h:338`). |
| `EcosystemEngine::Agent` (private nested struct) | 3 | same file | **Clean.** `grep -rn "\(class\|struct\) Agent\b" dsp/ plugins/ tools/` → no matches; nested and private besides. |
| `detail::EcosystemEngineNonFiniteProbe` (test friend) | 3 | same file | **Clean.** No `Ecosystem*` symbol exists in the tree. Mirrors `detail::BloomEngineNonFiniteProbe` (forward-declared `bloom_engine.h:162`, befriended at `:1661` — `friend struct detail::BloomEngineNonFiniteProbe;`). |

**Near-name hazards checked and cleared** (the roadmap's list at lines 128–129 plus the ones this
component could plausibly collide with): `ResonatorBank`, `FeedbackNetwork`, `NoiseGenerator`,
`GranularEngine`, `PatternScheduler` — none is referenced or shadowed here. `AgentTable`,
`ResourceField`, `Habitat`, `EcosystemAgent`, `EnergyPool` were swept and are **unused in the tree**;
none is introduced either — every internal type stays nested and private, so the component adds
exactly **one** namespace-scope name to `Krate::DSP`.

---

## Functional Requirements

### FR-001 series — Component contract and lifecycle

- **FR-001** The component is a single Layer 3 class `Krate::DSP::EcosystemEngine` in
  `dsp/include/krate/dsp/systems/ecosystem_engine.h`, header-only (roadmap line 379). It includes
  Layer 0 + standard library **only**: `core/random.h`, `core/db_utils.h`, `<array>`, `<algorithm>`,
  `<cmath>`, `<cstddef>`, `<cstdint>`. No Layer 1/2/3 header, no Vorago header, no consumer header.
  `node tools/lint-layers.js` must pass (SC-015).
- **FR-002** The component has **no audio path**: no method takes or returns an audio buffer, and it
  renders nothing. Its entire output is state read through FR-060's surface.
- **FR-003** Real-time safety: every public method is `noexcept`; no allocation, lock, exception, or
  I/O anywhere, **`prepare()` included**. All storage is fixed-size `std::array` members sized by
  the compile-time capacities of FR-004. `getAllocatedBytes()` returns `0` (the
  `resonance_drift_network.h:906-912` idiom — kept "so the Phase-10 host can total its children
  uniformly").
- **FR-004** Compile-time capacities, each a `static constexpr` with a live `static_assert`:
  `kMaxAgents = 48` (roadmap line 381's upper bound), `kMinAgents = 1`, `kMaxResourceCells = 96`
  (the fuzz box's upper bound, `run.js:317`), `kNumKinds = 5`, `kControlChunkSamples = 64` (the
  shared grid, `bloom_engine.h:221`), `kMinUsableSampleRate = 8000.0`
  (`resonance_drift_network.h:281`).
- **FR-005** `void prepare(double sampleRate, const PrepareConfig& config) noexcept` and
  `void reset() noexcept`. `PrepareConfig` is a nested struct documented **designated-initialisers
  only**, with the house rationale that a positional brace init can hide a narrowing conversion
  Clang errors on and MSVC does not (`resonance_drift_network.h:297-306`). Fields and defaults are
  Appendix A. A sample rate below `kMinUsableSampleRate`, a non-finite sample rate, an `agentCount`
  outside `[kMinAgents, kMaxAgents]`, a `resourceCells` outside `[1, kMaxResourceCells]`, a
  `stepIntervalChunks` outside `[8, 64]` or an `energyBudget` outside **`[1e-3, 1e3]`** (non-finite
  included) is **clamped**, and the getter reports the clamp. The `energyBudget` floor is load-bearing
  and not decorative: it is FR-061's divisor and SC-001 (c)'s reference value, so a zero or non-finite
  budget accepted here would publish a non-finite output through the public API (SC-019, SC-009 (c)).
  `prepare()` establishes the full seeded initial state (FR-080); `reset()` returns to exactly the
  state `prepare()` produced for the current seed.
- **FR-006** **Prepare-time set.** `agentCount`, `resourceCells`, `energyBudget`,
  `stepIntervalChunks` and `initialPoolFraction` (Clarifications, Q5) are **fixed at `prepare()`** and
  have no runtime setter. For the first two, changing them mid-flight would have to re-partition the
  conserved energy budget; for `energyBudget` it is worse — it is simultaneously the initial conserved
  total, FR-061's output divisor and SC-001 (c)'s conservation reference, so a mid-run change either
  breaks SC-001 (c) by construction or performs an energy creation/destruction event no rule in this
  spec describes, and neither this phase nor Phase 10's macro system needs it. `stepIntervalChunks`
  fixes `dt` at `prepare()` (FR-082), so a live setter would silently re-scale every per-second rate
  mid-trajectory. `initialPoolFraction` only acts inside FR-041's one-time initial-energy partition —
  a "runtime setter that takes effect at the next `prepare()`" is not a runtime setter in FR-064's
  sense (an FR-064 setter acts on the current state immediately), so it belongs in this set rather
  than among FR-064's knobs. Every *rule* knob is settable at runtime (FR-064). Habitat dimensionality
  is not a knob at all: FR-012 fixes the habitat as a 2-D torus.
- **FR-007** An **unprepared** object is a silent no-op: advancing does nothing, every getter returns
  its documented neutral (`0.0f` for floats, `0` for sizes, `Kind::Partial` for kinds), and nothing
  reads out of bounds. Never a crash.
- **FR-008** **Share-unit energy-scale knobs** (Clarifications, Q1). `preyFloor` (FR-022), `capacity`
  (FR-055) and `satiation` (FR-051) are specified, set and reported in units of the population mean
  share `energyBudget / agentCount`; `cellCapacity` (FR-042) is specified, set and reported in units of
  the per-cell share `energyBudget / resourceCells`. Each is converted to an absolute energy quantity
  **exactly once**, at `prepare()` (and at any full re-derivation of the initial state, i.e.
  `setSeed()` — FR-080), using the `energyBudget`, `agentCount` and `resourceCells` fixed for that
  call; the absolute quantity is what every rule (FR-022, FR-042, FR-051, FR-055) actually operates on
  for the life of that prepared state — it does not re-derive between `prepare()` calls, and a runtime
  setter for one of these knobs (FR-064) takes a share-unit value and re-converts immediately using the
  *current* prepare-time `energyBudget`/`agentCount`/`resourceCells`. This makes the four knobs — and
  every criterion stated on them, including SC-019's output-anchor gate — invariant to `energyBudget`
  and `agentCount`/`resourceCells` scaling by construction, rather than by coincidence of the
  particular values Appendix A happens to default to. Appendix A restates all four defaults and ranges
  in share units: `preyFloor` 0.016 → **0.5 shares** (range **[0, 1.6] shares**), `capacity` 1.0 →
  **32 shares** (range **[0.32, 32] shares**), `cellCapacity` 0.05 → **3.2 cell-shares** at the default
  `resourceCells = 64` (range **[0.32, 12.8] cell-shares**), `satiation` stays **0 = off** (range
  **[0, 16] shares** when nonzero). Getters (`getPreyFloor()`, `getCapacity()`, `getCellCapacity()`,
  `getSatiation()`, per FR-064) report the value in share units, matching what was set.

### FR-010 series — Agent table and habitat

- **FR-010** A fixed table of `agentCount` agents (default 32; roadmap line 381 "~24–48
  agents/voice"). Each agent is `{Kind kind, double energy, double x, double y, double phase,
  double freq, double freq0, float wake, bool dormant}` — the roadmap's
  `{kind, energy, position, rule params}` (lines 381–382) with the prototype's phase/frequency pair
  (`ecosystem-sim.js:198-206`).
- **FR-011** `enum class Kind : std::uint8_t { Partial = 0, Resonator, Noise, Feedback, Ghost }`,
  `kNumKinds = 5` — the roadmap's roster (line 372: "partial clusters, resonator peaks, noise
  emitters, feedback loops, ghost triggers"), matching `ecosystem-sim.js:63` in **order**, because
  the affinity matrix (FR-031) is indexed by it. **Kinds are assigned at `prepare()` by a stratified
  draw from the seeded kind stream (`:196`), not by the prototype's unconstrained i.i.d. draw**
  (Clarifications, Q6): deal `floor(agentCount / kNumKinds)` agents to each kind first, distribute the
  remainder (`agentCount mod kNumKinds`) one kind at a time by a seeded draw, then apply a seeded
  shuffle to the resulting per-agent kind assignment so kind is not correlated with agent index. This
  guarantees **at least one** agent of every kind whenever `agentCount >= kNumKinds` (5); below that
  (the `kMinAgents = 1` edge) some kinds are necessarily absent and `getAgentCountOfKind()` (FR-060)
  may return 0 for them. Kinds are fixed thereafter. Because this changes the kind histogram from the
  prototype's unconstrained draw, SC-002's defaults figures are provisional and must be re-measured
  against the stratified assignment at implementation time (SC-002).
- **FR-012** Habitat: a **2-D torus**, unit square, both axes wrapping, separation on each axis
  wrapped into `[-0.5, +0.5]` (`ecosystem-sim.js:242-246`). Neighbourhood weight is
  `w = exp(-d² / (2σ²))` with `σ = kernelSigma`, and a pair with `w < 1e-6` is **skipped entirely**
  (`:274-275`) — the cutoff is normative, not an optimisation: it is what makes the interaction
  local. 2-D is the proven configuration: **+70 % activity, −38 % correlation and 4× fewer pair
  operations than 1-D** (`FINDINGS.md:237-241`). `kernelSigma` is the single most consequential knob
  measured (`FINDINGS.md:78-83`): 0.12 → activity 0.06 / correlation 0.84; 0.03 → activity 0.32 /
  correlation 0.44. Default 0.03.
- **FR-013** Each agent carries an intrinsic frequency drawn once in `[freqLo, freqHi]` (defaults
  0.0015–0.018 Hz, i.e. periods of 667 s to 56 s — `ecosystem-sim.js:136-138`) and a **bounded
  Ornstein–Uhlenbeck drift** on it: `freq += noise·freqDrift·sqrt(dt) − 0.5·(freq − freq0)·dt·0.01`,
  hard-clamped to `[freqLo, freqHi]` (`:503-510`). Its job is anti-limit-cycle ("nothing repeats
  exactly", roadmap line 30); the ablation prices it at +2 % activity, i.e. within noise, and it is
  kept because it is cheap and it is what SC-005 rests on (`FINDINGS.md:276`).
- **FR-014** Phase integrates as `phase = frac(phase + (freq + dPhase)·dt)` (`:511`), always in
  `[0, 1)`.

### FR-020 series — Rule 1: energy exchange

- **FR-020** For every surviving pair, the desired flow is
  `flow = exchangeRate · w · (e_j − e_i) · (1 − 2·predation)` (`ecosystem-sim.js:285`). The factor
  flips the character: `predation = 0` is pure diffusion (strong feeds weak), `1` pure predation
  (weak feeds strong). Default `predation = 0.55`, `exchangeRate = 0.35`.
- **FR-021** **`predation = 0.5` disables the exchange rule exactly** — the `(1 − 2·predation)`
  factor is zero. The header must say so, because it was a real trap: an ablation run reported "no
  exchange" as **bit-identical to baseline, to every printed digit**, since the default at the time
  *was* 0.5 and the rule had been silently off in the very configuration being validated
  (`FINDINGS.md:119-122`). SC-012's coverage assertion and the header note are the two defences.
- **FR-022** **Refuge floor.** An agent may only transfer away what it holds **above** `preyFloor`
  (default **0.5 shares** of the mean per-agent energy `energyBudget / agentCount` — FR-008 — 0.016 in
  absolute terms at the default budget and population, i.e. half the mean share). Without it,
  predation ≥ 0.6 parks a third of the agents at zero permanently — a cliff 0.05 (absolute) away from
  the default; with it, no agent freezes up to predation 0.85 (`FINDINGS.md:224-226`,
  `ecosystem-sim.js:340`).
- **FR-023** **Exchange is applied in two passes, and this is a conservation requirement, not an
  implementation choice** (Guard 2, `FINDINGS.md:36-50`). Pass 1 records each pair's desired flow and
  accumulates each agent's total desired outflow. Pass 2 computes, **verbatim in this guarded form**
  (`ecosystem-sim.js:337-341`):

  ```
  want_i  = outflow_i · dt
  spare_i = e_i − preyFloor
  scale_i = (want_i > 0 && want_i > spare_i) ? (spare_i > 0 ? spare_i / want_i : 0.0) : 1.0
  ```

  then applies each pair scaled by **`min(scale_i, scale_j)`**. The two guards in that expression are
  normative, and a `min(1, spare_i / want_i)` paraphrase loses both:
  (a) **no division is performed when `want_i == 0`** — an agent with no surviving pair, or with only
  inflows, would otherwise evaluate `0/0` (NaN) or `spare/0` (±Inf), which is the *common* case at the
  default `kernelSigma = 0.03` where only ~34 of 496 pairs survive (`FINDINGS.md:254`) and the only
  case at the FR-004 `kMinAgents = 1` edge, where no pair exists at all; and
  (b) **`scale_i` is always in `[0, 1]`** — an agent below `preyFloor` has `spare_i < 0` and takes the
  `0.0` branch, whereas `min(1, negative)` would yield a *negative* scale and `min(scale_i, scale_j)`
  would then apply every flow in that pair with **reversed sign** and arbitrary magnitude. That state
  is routine, not exotic: the hostile box draws `preyFloor` up to 0.05 (`run.js:330`) against a mean
  share of `energyBudget/agentCount` ≈ 0.021–0.042, so most agents sit below the floor in much of the
  box. A sign-reversed flow is still antisymmetric, so SC-001 provably cannot see it — SC-020 gates
  it directly.
  Scaling *both sides of a pair by the same factor* is
  what preserves antisymmetry while making solvency structural; a solvent agent is never throttled
  (`scale = 1`). Antisymmetry **alone is not sufficient**: 468 of 1000 fuzzed configurations drove
  the pool negative before this pass existed, because an agent asked to give more than it holds was
  clamped at zero with the shortfall charged to the pool — an uncapped withdrawal.
- **FR-024** Exchange never touches the pool or the resource field: the pairwise sum is exactly zero
  by FR-023.

### FR-030 series — Rule 2: movement (attract / repel / forage / crowd)

- **FR-030** **Affinity force**, per pair: `f_i += A[k_i][k_j] · w · e_j · û`, and symmetrically for
  `j` with `e_i` and `−û`, where `û` is the unit toroidal separation (`ecosystem-sim.js:292-300`).
  The force is scaled by the **neighbour's energy**, so a spent agent stops pulling and the topology
  tracks where the energy actually is.
- **FR-031** A `kNumKinds × kNumKinds` **affinity matrix**, runtime-settable per entry, clamped
  `[-2, +2]`. Default: same kind `−1.0` (a species spreads across the habitat), different kinds
  `+0.45` (mixed neighbourhoods exchange energy) — `ecosystem-sim.js:154-167`.
- **FR-032** **Crowding repulsion** (mandatory, kind- and energy-independent): inside
  `crowdingRadius` (default 0.02) add `crowding · (1 − d/crowdingRadius)` along `−û` for `i` and
  `+û` for `j` (`:307-315`). Without it the 32 agents collapsed to **six distinct positions** after
  30 minutes; co-located agents receive identical grazing input and produce one modulation signal
  copied, and a starved clump exerts no affinity force, feels none, and never moves again
  (`FINDINGS.md:210-214`). With it, distinct positions went 6 → 31. Default `crowding = 0.05`.
  Gated by **SC-002 (c)**, which measures distinct positions directly: at the 2-D defaults the
  ablation prices crowding removal at −2 % activity and −2 % correlation, "within noise in 2D"
  (`FINDINGS.md:274`), so SC-002 (a) and SC-004 provably cannot discriminate this rule and a
  positional metric is the only thing that can.
- **FR-033** **Foraging**: movement up the locally sensed resource gradient,
  `forage_i += (res_k / cellCapacity) · w · (d / σ²)` accumulated over cells
  (`ecosystem-sim.js:428`), added to the x-velocity term (`:490`). Rationale, verified: the affinity
  force is scaled by neighbour energy, and at a 1.0 budget over 32 agents a neighbour holds ~0.016,
  so affinity alone never approached `maxSpeed` and "no movement" ablated as a no-op
  (`ecosystem-sim.js:95-103`). Foraging is **load-bearing in 1-D (−56 % activity without it) and
  within noise in 2-D (+3 %)**, and is kept because it is what makes movement mean anything — path
  per agent per hour 2.9 with it, 0.85 without (`FINDINGS.md:218-222`). Default `forageRate = 0.01`.
- **FR-034** Velocity is `v = (moveRate·f_x + forageRate·forage_x, moveRate·f_y) · dt`,
  **slew-limited** to `maxSpeed · dt` in magnitude (`ecosystem-sim.js:490-499`), then wrapped into
  the torus. Defaults `moveRate = 0.20`, `maxSpeed = 0.03`.
- **FR-035** **Rule 3 — synchronization (Kuramoto), OFF by default.**
  `dPhase_i += syncRate · w · sin(2π(phase_j − phase_i))`, antisymmetric (`:317-322`). Default
  `syncRate = 0`. (The sine is evaluated as `s_j c_i − c_j s_i` from a per-agent sin/cos table —
  lever E-2, FR-085, an identity gated by SC-011 (c); the formula is unchanged.) The roadmap lists "synchronize" among the core rules (line 372); the measurement
  demotes it to a knob: aligned phases mean aligned appetites, so agents feed in unison and their
  energies correlate — removing it at 0.03 nearly **doubled** activity in 1-D (+86 %) and halved
  correlation, and re-adding it at the final 2-D defaults costs 18 % activity
  (`FINDINGS.md:228-233`). It is retained so a Phase-10 *coherence* macro can dial it in
  deliberately. The header must state this in one sentence: **synchronize is a macro, not a default
  rule.**

### FR-040 series — Rule 4a: the spatial resource field

- **FR-040** The resource is a field of `resourceCells` independent cells (default 64), **not** a
  single scalar pool. A single global pool was the first design and it produced mean pairwise output
  correlation 0.84–0.99 (`FINDINGS.md:59-68`). **Geometry, as proven:** cell `k` sits at
  `x = (k + 0.5)/resourceCells` and grazing/foraging weight uses the **x separation only**
  (`ecosystem-sim.js:228`, `:421`, `:428`), so in the 2-D habitat the field is a set of vertical
  strips. Every published figure was measured on this geometry. See OQ-1. (The weight
  `exp(−d²/(2σ²))` is evaluated along the cell grid by the exact Gaussian recurrence — lever E-1,
  FR-085, gated by SC-011 (c); the formula is unchanged.)
- **FR-041** **Initial energy partition** (Clarifications, Q5). The prototype's three-way split of
  `energyBudget` is encoded verbatim (`ecosystem-sim.js:197-232`), computed once at `prepare()` and
  re-computed identically at `setSeed()` (which re-derives the whole initial state, FR-080): each
  agent's energy is drawn `U(0,1)` and the set of agent energies is normalised so their sum is
  `(1 − initialPoolFraction) · energyBudget`; each cell's energy is drawn `U(0,1) · cellCapacity` (the
  absolute quantity, via FR-008's conversion) and the set of cell energies is then scaled so their sum
  is `min(resSum, remaining · 0.5)`, where `resSum` is the drawn cells' raw sum and
  `remaining = energyBudget − Σ agent energies`; the pool takes whatever is left:
  `energyBudget − Σ agent energies − Σ cell energies`. Cells are **never** left empty — starting them
  at zero gave every seed an identical opening transient that dominated the trajectory — cross-seed
  per-agent correlation 0.60, "voices would have breathed in unison" (`ecosystem-sim.js:219-232`).
  `initialPoolFraction` (default 0.5, range [0.1, 0.9]) is a **prepare-time** knob (FR-006) with no
  runtime setter, because it acts only during this one-time partition.
- **FR-042** Per-cell capacity `cellCapacity` (default **3.2 cell-shares** of the per-cell share
  `energyBudget / resourceCells` — FR-008 — 0.05 in absolute terms at the default `resourceCells = 64`)
  is a hard ceiling; regrowth never exceeds it.
- **FR-043** A cell's stored energy that would fall below a denormal guard (`1e-30`) is **snapped to
  zero and the snapped amount returned to the pool**, so flushing can never destroy energy. This is
  not hypothetical: the prototype's regrowth defect left cells at `1e-234` (`FINDINGS.md:194-200`).
  Gated by **SC-021**, a probe-level test, because SC-001 (c) structurally cannot see it: that gate is
  a *relative* drift of 1e-9 on a budget of order 1, while the quantity here is ≤ 1e-30 per cell per
  step — even 84 375 steps × 96 cells × 1e-30 ≈ 8e-24 sits fourteen decades below the gate.

### FR-050 series — Rule 4b: metabolism

- **FR-050** **Phase-gated appetite**: `appetite_i = max(0, 1 + appetiteDepth · sin(2π · phase_i))`
  (`ecosystem-sim.js:355-360`). Default `appetiteDepth = 0.8`. (The sine is the per-agent table entry
  of lever E-2, FR-085 — the same `std::sin` call, taken once at the start of the step.) This is **the load-bearing rule**: the
  ablation prices its removal at **−96 % activity, +323 % correlation, 26 of 32 agents frozen**
  (`FINDINGS.md:264`).
- **FR-051** **Demand-scaled grazing.** For each cell, each agent's demand is
  `w · appetite_i · hunger_i` (with `hunger_i = max(0, 1 − e_i/satiation)` when `satiation > 0`,
  else 1 — `satiation`, when nonzero, is in **shares** of `energyBudget / agentCount`, FR-008; default
  **0 = off**); the cell offers `grazeRate · res_k · dt` per unit demand and **caps the total at what it
  holds** (`ecosystem-sim.js:441-451`). The earlier fixed-amount-split-by-share form is forbidden by
  name: it made a lone grazer eat the same whatever its appetite said, so the phase gate only decided
  who *won* a contested cell and every configuration settled at a starvation fixed point
  (`FINDINGS.md:202-207`). Default `grazeRate = 0.75` (1.5 over-grazed: the field sat under 3 % full).
- **FR-052** **Nonlinear leak** back to the pool: `leak_i = leakRate · e_i^leakExponent · dt`
  (`ecosystem-sim.js:466`). Defaults `leakRate = 0.06`, `leakExponent = 1.0`. `leakExponent > 1`
  punishes hoarding, turning predation's rich-get-richer into boom/bust rather than one permanent
  winner — but above ~1.3 it *kills*: at per-agent energies of ~0.03 a superlinear leak all but
  vanishes, agents fill to capacity and sit (63 % alive in the lowest tercile vs 13 % in the highest,
  `run.js:349-353`). The header must carry that band as guidance for Phase 10's macros.
- **FR-053** **Proportional regrowth** (pool → cells): sum every cell's desired
  `regenRate · (cellCapacity − res_k) · dt` **first**, then scale every cell by what the pool can pay
  (`ecosystem-sim.js:386-400`). Index-order regrowth is forbidden by name: at steady state the pool
  is empty (agents hold ~96 % of the budget), so cells 0–19 took every joule and cells 20–63 never
  regrew — **70 % of the habitat was a permanent desert** and the frozen-agent count went from 12–14
  to **0** when this was fixed (`FINDINGS.md:192-200`). Default `regenRate = 0.05`.
- **FR-054** **Single withdrawal budget per step** (Guard 1, `FINDINGS.md:30-34`). Every draw on the
  pool within one step — cell regrowth *and* the optional global feed — decrements **one running
  balance**, and the balance is never allowed below zero. Two independent caps against a
  start-of-step pool drove the pool negative in **591 of 1000** fuzzed configurations.
- **FR-055** **Carrying capacity and the zero clamp both return their delta to the pool.** Energy
  above `capacity` (default **32 shares** of the mean per-agent energy `energyBudget / agentCount` —
  FR-008 — 1.0 in absolute terms at the default budget and population) spills back to the pool; an
  agent driven below zero is clamped to zero with the overdraw returned to the pool
  (`ecosystem-sim.js:478-488`). Neither may silently create or destroy energy.
- **FR-056** **The pool is never clamped**, and a negative pool is a latched, readable defect
  signal — `getConservationViolationCount()` (monotonic, non-saturating) and `getPoolEnergy()`.
  This counter counts **exactly one thing**: the pool went negative. Non-finite containment has its
  **own** counter (FR-083), because one counter carrying two meanings would make SC-001 (b) unable to
  say which of the two failure modes it caught.
  Clamping a negative pool to zero would *create* energy and break the invariant the whole
  boundedness argument rests on; the prototype states it outright ("A negative pool is a real design
  error and the metrics must see it", `ecosystem-sim.js:514-521`). The counter idiom follows
  `ResonanceDriftNetwork::getClampEngagementCount()` (`resonance_drift_network.h:904`).
- **FR-057** The **optional global feed** (`feedRate`, default **0** — disabled) draws from what
  regrowth left, never from the start-of-step pool, and is split by appetite share
  (`ecosystem-sim.js:453-461`). It exists only because the hostile fuzz box exercises it; it is not a
  default rule.

### FR-060 series — Output and read surface

- **FR-060** The output surface is **indexed**, not `ModulationSource`-shaped (Overview correction 4):
  `[[nodiscard]] float getAgentOutput(std::size_t i) const noexcept` in `[0, 1]`,
  `getAgentEnergy(std::size_t i)` (the raw conserved quantity, for tests and for the Phase-13 view),
  `getAgentKind(std::size_t i)`, `getAgentPositionX/Y(std::size_t i)`, `getAgentPhase(std::size_t i)`,
  `getAgentCount()`. **Additionally** (Clarifications, Q6, Q8): `getAgentCountOfKind(Kind kind)` —
  the count of agents assigned that kind under FR-011's stratified draw; the five per-kind counts sum
  to `getAgentCount()` — and `getEnergyBudget()`, `getResourceCells()`, `getStepIntervalChunks()` and
  `getStepDurationSeconds()`, which report the FR-005-clamped prepare-time values, so a fuzz harness or
  a test (SC-010, SC-012, SC-019) can read back what `prepare()` actually did rather than trusting its
  own `PrepareConfig` input. Every out-of-range index returns the documented neutral and reads
  nothing. The doxygen states the roadmap's intended consumers (lines 386–388) as *use*, not as
  coupling.
- **FR-061** **Output normalisation.** `getAgentOutput(i) = clamp(0.5 · e_i · agentCount /
  energyBudget · wakeGain_i, 0, 1)`. The anchor is deliberate: the population mean share is
  `energyBudget / agentCount`, so an agent at the mean publishes **0.5**. *(An earlier draft added
  "and the measured ±44 % late-window swing maps to roughly `[0.28, 0.72]`" — struck: `FINDINGS.md`'s
  0.44 is `std(e_i)/grandMean` **per agent over time**, not a population spread, and the measured
  pooled band at the defaults is `[0.18, 0.92]`. See SC-019, which was written on that misreading and
  is corrected there.)* Raw energy would hand Phase 10 a value whose scale depends on `agentCount` and
  `energyBudget` — two knobs the macros move. The consumers' own clamps
  (`resonance_drift_network.h:765-770`) are a second net, not the first. The anchor invariance is
  gated by **SC-019**, not left as prose.
  The clamp **saturates at exactly twice the population mean share**, so an agent above
  `2 · energyBudget / agentCount` (0.0625 at the defaults) publishes a flat `1.0` however much more it
  holds. That is a routine state for a system whose documented character is boom/bust under predation
  (FR-052), and a railed output is invisible to any criterion stated on raw energy. The component
  therefore exposes `getOutputClampEngagementCount()` and
  `getAgentClampedStepFraction(std::size_t i)` (FR-065). **Both counters count the UPPER rail only**
  (Clarifications, Q4) — a step in which `getAgentOutput(i)` published exactly `1.0` because this
  clamp saturated it. A published output of `0.0` arising from dormancy (FR-070), wake gating, or the
  FR-063 epsilon snap is a **designed** state, not a rail engagement, and is **never** counted by
  either counter — a `getAgentZeroOutputStepFraction()` covering that low end may be added later,
  append-only, if a consumer needs it; it is not part of this phase. **SC-002 (d)** gates the
  (upper) rail directly.
- **FR-062** Outputs are **recomputed only at a control step** (FR-081) and held between steps, so a
  consumer polled per block sees a stable value. This is required, not incidental:
  `FeedbackEcology::refreshGates()` restarts a 50 ms ramp on every re-target, and its header warns
  that "a Phase-8 agent writing one loop's wake per block would otherwise stretch every OTHER loop's
  ramp without bound" (`feedback_ecology.h:2292-2296`).
- **FR-063** An output whose value is `<= 1e-6` is published as **exactly `0.0f`**, at the source.
  The consumer-side rationale is quoted in the header: a release tail that stops at 1e-8 "must not
  leave a loop burning forever" (`feedback_ecology.h:2260-2262`, `kWakeSilenceEpsilon`).
- **FR-064** Every knob of Appendix A **except the FR-006 prepare-time set** (`agentCount`,
  `resourceCells`, `energyBudget`, `stepIntervalChunks`, `initialPoolFraction` — marked *prepare* in
  Appendix A) has a runtime setter, and every setter has a matching getter. The semantics are uniform and are spelled
  out here rather than referred to by number (the house rule is `bloom_engine.h:524-529`; there is no
  FR-009 in this spec, and a criterion cannot trace to a requirement that does not exist):
  a **non-finite** float argument is **rejected** and the previous value stands; an **out-of-range
  index** (an agent index, cell index or affinity row/column) is a **silent no-op** that reads and
  writes nothing; an **out-of-range value** is **clamped** to the Appendix A range and the getter
  reports the clamped value. A setter never allocates, never throws and never advances the
  simulation.
- **FR-065** Diagnostics for tests and for the Phase-13 view, all `[[nodiscard]] … const noexcept`:
  `getPoolEnergy()`, `getCellEnergy(std::size_t k)`, `getTotalEnergy()` (pool + cells + agents),
  `getPairInteractionCount()` (surviving pairs at the last step),
  `getConservationViolationCount()` (FR-056, negative pool only),
  `getNonFiniteContainmentCount()` (FR-083), `getOutputClampEngagementCount()` (FR-061, total steps in
  which any agent's published output hit the **upper** rail — Clarifications, Q4),
  `getAgentClampedStepFraction(std::size_t i)` (FR-061, that agent's fraction of steps at the upper
  rail), `isPrepared()`, `getSampleRate()`, `getAllocatedBytes()`. **Counter lifetime** (Clarifications,
  Q8): `prepare()`, `reset()` and `setSeed()` all clear **every** counter in this list —
  `getConservationViolationCount()`, `getNonFiniteContainmentCount()`, `getOutputClampEngagementCount()`
  and every agent's `getAgentClampedStepFraction()` — back to zero, so a fuzz harness (SC-001, SC-012,
  SC-013) may reuse a single instance across configurations without reconstructing it.
- **FR-066** `getEnergyEntropy()` (Shannon entropy of the normalised agent-energy distribution, in
  bits) is provided as a **reported, non-gating** statistic and its doxygen must state why: it is
  permutation-invariant, so it cannot see *which* agent holds the energy, and it sat at 4.9–5.0
  across every regime tested including ones differing 5× in per-agent activity
  (`FINDINGS.md:148-157`). No success criterion gates on it.

### FR-070 series — Sleep / wake (the cross-cutting Dormancy rule)

- **FR-070** `void setAgentWake(std::size_t i, float amount) noexcept` (clamped `[0, 1]`, non-finite
  rejected) and `void setAgentDormant(std::size_t i, bool dormant) noexcept`. As required by the
  cross-cutting rule (roadmap lines 537–545), the two are **behaviourally identical**:
  `setAgentWake(i, 0)` and `setAgentDormant(i, true)` fold into one steady gate value, exactly as
  `ResonanceDriftNetwork::gateSteady()` and `FeedbackEcology::gateSteady()` do
  (`resonance_drift_network.h:787-790`, `feedback_ecology.h:2268-2273`). Re-entry is a **50 ms**
  linear ramp on the published output, targeting `kGainRampMs = 50.0f`
  (`noise_organism.h:178`) — **but evaluated on the control-step grid, not per sample**, and the
  header must say so. This component has no per-sample path at all (FR-002) and FR-062 forbids
  publishing between steps, so the ramp is realised as
  `rampSteps = max(1, ceil(0.050 s / stepDuration))` equal increments of `1 / rampSteps`, one per
  control step, where `stepDuration = stepIntervalChunks · 64 / sampleRate`. At the FR-082 default
  (512 samples, 10.667 ms at 48 kHz) that is **5 steps = 53.33 ms**: the realised duration is
  quantised to the step grid and cannot be 50 ms exactly. Above `stepIntervalChunks ≈ 47` at 48 kHz
  one step already exceeds 50 ms, so `rampSteps` floors at 1 and the output reaches its target in a
  single step — a documented consequence of the publication rate, not a defect. SC-014 (b) is stated
  in control steps for exactly this reason.
- **FR-071** `void perturbAgent(std::size_t i, float amount) noexcept` — a bounded,
  conservation-safe event hook, so a Phase-10 `SlowEventScheduler` can "wake" an agent the way
  roadmap line 387 describes. A plain scalar, not a scheduler reference (house rule,
  `noise_organism.h:841-843`). Because this is the phase's **only** external energy-injection API and
  Phase 10 will drive it from production wiring, the transfer is specified as a closed formula for
  both signs rather than as "scaled by the available headroom":

  ```
  a     = clamp(amount, -1, 1)            // non-finite amount → no-op
  delta = a >= 0 ? min(a · (capacity − e_i), max(0.0, pool))          // pool → agent
                 : max(a · max(0.0, e_i − preyFloor), −e_i)          // agent → pool
  e_i  += delta;   pool -= delta;
  ```

  *(Corrected at plan time, S14 D-L: an earlier draft wrote the giving term as `a · (e_i − preyFloor)`
  without the inner floor. Below the refuge that term is negative, `a × negative` is positive and the
  `max` selects it, so a "take energy" call paid the agent up to `|a| · preyFloor` out of an unguarded
  pool and SC-001 (d) failed by construction. Flooring the giving term first keeps
  `delta ∈ [−e_i, 0]` on the negative branch, which is what the consequences below already claim.)*

  Stated consequences: an agent at `capacity` receives nothing; an agent at or below `preyFloor` gives
  nothing (the refuge is respected, FR-022); an agent is never driven below zero; a **negative pool**
  (which FR-056 deliberately permits and never clamps) pays out nothing on the positive branch, so
  `perturbAgent` can never deepen a violation it did not cause; and the two writes are equal and
  opposite, so the conserved total is unchanged **exactly**, in `double`, for every sign and every
  index. An out-of-range `i` is a silent no-op (FR-064). Gated by **SC-001 (d)**, which runs a seeded
  `perturbAgent` schedule inside the boundedness fuzz — the conservation clause is the load-bearing
  half of this FR and, before this revision, no criterion exercised it at all.
- **FR-072** **Dormancy applies to the OUTPUT, not to the simulation, and the header must say why.**
  The rule states that a gain at zero skips the component's processing chain while "its
  source/generator and modulation lanes keep running" (roadmap lines 537–539). For this component the
  agent **is** the generator and the modulation lane — there is no chain behind it — and its energy
  is a share of a **conserved** budget: excluding a sleeping agent from grazing, exchange and leak
  would either strand its energy outside the economy or destroy it, breaking FR-023/FR-054 and every
  boundedness criterion. A dormant agent therefore keeps simulating and keeps its energy; only its
  published output is gated (with the 50 ms ramp). Nothing "burns" that the rule was written to stop:
  the per-agent cost is the simulation itself, which must run regardless. This is the rule's
  *stated-exception* shape (roadmap lines 543–545, Phase 5 FR-063), argued from the mechanism rather
  than assumed. See OQ-5.

### FR-080 series — Determinism, control clock, budget, non-finite

- **FR-080** **Determinism under seed** (roadmap line 391). `void setSeed(std::uint32_t) noexcept`
  re-derives the whole initial state. Every random lane draws from its own stream derived through
  `deriveStreamSeed(seed, salt)` with a documented **salt table** carrying a `static_assert` that no
  two salts collide — positions, energies, frequencies, phases, kinds, drift and the initial resource
  fill are the prototype's seven streams (`ecosystem-sim.js:207-219`, salts 0–6). Same seed + same
  binary ⇒ bit-identical trajectory (SC-006). **No bit-exact cross-toolchain golden** is asserted
  anywhere (roadmap line 546).
- **FR-081** **Control clock.** `void processChunk(std::size_t numSamples) noexcept` advances an
  **absolute-residue** sample counter carried **across calls**, so the state after N samples is a
  function of N alone and never of how N was partitioned into calls — the `bloom_engine.h:893-916`
  contract, whose natural mistake (`numSamples / kControlChunkSamples` per call, phase reset each
  call) is named at `:899`. `numSamples == 0` applies the current state without advancing and draws
  from no RNG stream.
- **FR-082** **Step interval.** One simulation step occurs every `stepIntervalChunks` × 64 samples,
  `stepIntervalChunks` defaulting to **8** — i.e. 512 samples ≈ 10.67 ms at 48 kHz, which is
  **exactly the `dt` the prototype's rule set was tuned at** (`ecosystem-sim.js:619`,
  `blockRate = 48000 / 512`). Range **`[8, 64]`** — the floor is the tuned default. The minimum was 1
  until two **2026-09-16 rulings** narrowed it, 1 → 4 → 8, each on SC-011's measured table (FR-085's
  named escalation; the record is under SC-011). `dt = stepIntervalChunks · 64 / sampleRate` is
  computed at `prepare()`, so all rates stay per-second and behaviour is wall-clock-driven at any
  sample rate (SC-010). The 64-sample residue grid is kept so a Phase-10 voice can call this inside
  the same chunk loop every other Vorago component uses. See OQ-2.
- **FR-083** Non-finite hygiene: no `std::isnan` / `std::isinf` / `std::isfinite` anywhere — the
  bit-pattern helpers `detail::isNaN` / `detail::isInf` (`core/db_utils.h:99`, `:260`) only, because
  macOS CI builds with `-ffast-math`. `tools/lint-nonfinite-symbols.js` enforces it. Every setter
  rejects non-finite input (FR-064). A state variable that somehow becomes non-finite is caught by
  the guard ladder at the **top** of the step, and the step is **contained and repaired** — it is not
  merely abandoned. "Abandon the write and carry on" is not a containment strategy here: the
  offending value stays in place, so the *next* step's guard fires too, and with FR-062 holding
  outputs between steps the component would freeze at its last published values **forever** while
  every finiteness assertion passed. The repair rule is therefore normative:
  - the offending agent's energy (or a cell's, or the pool's) is **re-derived**, not left in place:
    the non-finite agent is reset to the population mean share `energyBudget / agentCount`, a
    non-finite cell to `0.0`, a non-finite pool to `0.0`; its position, phase and frequency are
    reset to the values `prepare()` produced for that index under the current seed;
  - the conserved total is then **restored exactly** by charging the difference between the
    pre-repair and post-repair totals to the pool, so the invariant SC-001 (c) gates is re-established
    in one step rather than abandoned;
  - `getNonFiniteContainmentCount()` (monotonic, non-saturating, **separate from**
    `getConservationViolationCount()`) increments once per containment event, and the step then
    proceeds normally.
  *(Corrected at implementation time, in the same spirit as FR-071's S14 D-L correction above, and
  for the same reason: the normative text as written is not executable arithmetic. Two clauses, both
  live in `ecosystem_engine.h:1216-1344`.*

  1. ***"Charging the difference between the pre- and post-repair totals" cannot be computed.*** *The
     pre-repair total contains the non-finite value that triggered the repair, and NaN minus anything
     is NaN — the charge would publish the very quantity the rung exists to remove. The operative form
     is the algebraic equivalent that **is** computable: sum the post-repair bodies (agents + cells)
     and set `pool = energyBudget − bodies`. That re-establishes `total == energyBudget` to one
     rounding, which is precisely and only what SC-001 (c) gates. Intent preserved; it is the one
     arithmetic that exists.*
  2. ***The mean share is the repair TARGET, not an entitlement the budget must fund.*** *Reinstating a
     repaired agent at `energyBudget / agentCount` **creates** energy whenever the value it replaced
     was smaller, and the pool is the residual of a budgeted economy — at steady state the population
     holds ~97 % of the budget and the pool ~0.06 % of it. Measured at the defaults: pool 6.2e-4,
     repaired agent 2.51e-2 against a mean share of 3.125e-2, pool after the charge **−4.9e-3**. A
     negative pool is exactly what `getConservationViolationCount()` means (FR-056), so funding the
     repair from an overdraft would make that counter fire on a **containment** event — the
     one-counter-two-meanings confusion FR-056 and SC-009 (b) forbid by name. The pool floor is
     therefore honoured first: where the budget cannot fund the full reinstatement, the repaired
     agents (and only they) are cut **pro rata** — an equal split, since they were all just set to the
     same share — and the pool lands at 0 instead of below it. Conservation stays exact, the
     containment stays a repair rather than an abandonment, and FR-056's counter keeps its single
     meaning.)*

  SC-009 gates this, including the clause that the component keeps *moving* after a containment event
  rather than being permanently dead but finite.
- **FR-084** **The energy economy is `double`.** Agent energies, cell energies and the pool are
  `double`; positions, phases and frequencies are `double`; the published outputs are `float`.
  Rationale: SC-001 asserts a relative conservation drift bound of 1e-9 over 84 375 steps per
  configuration, and **SC-018 holds it over 2 700 000 steps** of an 8-hour soak; `float` has ~1.2e-7
  epsilon, which is already coarser than the gate at one step. The prototype measured **2.4e-15 to
  1.9e-13** in double (`FINDINGS.md:23`), leaving four decades of margin even at the soak length,
  whereas float could not distinguish a real leak from rounding. The cost is ≤ 48 + 96 + 1 doubles.
- **FR-085** **CPU budget** ≤ **0.5 % of one core per instance** (roadmap line 395), measured as **ns
  per 512-sample block at 48 kHz** — one block period is 10 666 667 ns, so the ceiling is
  **53 333 ns/block**. The ceiling is stated **per block, at every legal `stepIntervalChunks`**, not
  only at the FR-082 default. That is deliberate and it is a correction: the roadmap states the budget
  as an unconditional FR, while the floor of FR-082's range is reachable through the documented
  `PrepareConfig` and runs a multiple of the step rate and cost of the default — a configuration that,
  gated only at the default, would exceed the roadmap's FR with no criterion failing. SC-011
  therefore gates **both ends of the range it retains** (the floor and 8) at the same 53 333 ns/block
  ceiling, at the worst-case rule configuration.
  The **stop-and-surface rule** applies verbatim (`resonance_drift_network_perf_test.cpp:57-64`): no
  implementing agent may lower `kMaxAgents`, raise the budget, relax a threshold or shrink a workload
  to make a figure fit. Reduce cost, or put the measured table to the user. Concretely, if the
  measured figure at the floor cannot be brought inside 53 333 ns/block, the **only** permitted
  responses are to reduce the per-step cost, or to put the measured table to the user with a proposal
  to **narrow FR-082's minimum** (a spec amendment, which re-scopes the public API rather than the
  gate). Raising the budget, restating it per step, or exempting the cheap end are all forbidden —
  restating a per-block budget as a per-step budget is a relaxation wearing a derivation.
  **Ruling of 2026-09-16, taken on the first measured table (recorded under SC-011):** at
  `stepIntervalChunks = 1` the worst case measured **404 614 ns/block**, 7.6× the ceiling, and cannot
  fit at any lever — 1 128 pairs plus 4 608 cell visits eight times a block is ~120 000 ns of plain
  loop body with every transcendental free. The user took the named escalation: **FR-082's minimum is
  4**, together with two **exact** cost levers, neither of which changes a normative formula:
  - **E-1** — FR-040's cell weight `exp(−d²/(2σ²))` is evaluated along the uniform cell grid by the
    Gaussian recurrence `w(d+h) = w(d)·exp(−(2dh+h²)/(2σ²))`, whose ratio advances by the constant
    `exp(−h²/σ²)` per cell (`h = 1/resourceCells`): two `exp` seed a run of consecutive in-range
    cells, each further cell costs two multiplies; the run re-seeds at the torus wrap and after any
    cell the FR-012 pre-test rejects. ~192 `exp` per step instead of 4 608 at the worst case.
  - **E-2** — one `sin`/`cos` per agent of `2π·phase_i` at the start of the step; FR-035's
    `sin(2π(phase_j − phase_i))` is `s_j c_i − c_j s_i` and FR-050's appetite `sin` is the table
    entry. 96 transcendental calls per step instead of 1 176.
  Both are algebraic identities with rounding-level error and are **gated by SC-011 (c)** against
  `std::exp` / `std::sin` on the component's own state. L4/L5 (lookup tables, approximations of the
  same formulas) were offered and **not** adopted; they remain not pre-authorised.
  **Second ruling, same day, on the re-measured table:** with E-1, E-2 and **E-3** (step invariants
  hoisted into locals and the pair loop's row accumulators kept in registers — bit-identical, the
  same operations in the same order) the worst case reads **27 224 ns/block at 8** (51 % of the
  ceiling) but **56 587–60 176 ns/block at 4**, 1.06–1.13× over across four pinned runs. Reciprocal
  multiplies measured at zero and were reverted (not bit-identical); an agent-outer two-pass grazing
  walk measured 25 % *slower* and was reverted; the remaining per-visit cost is memory- and
  dependency-bound and the 1 128 pair `exp` have no exact reduction for arbitrary positions. The user
  again declined the pair-kernel LUT and took the escalation once more: **FR-082's minimum is 8** —
  the floor is the tuned default, and nothing below it had a consumer. SC-011 gates the floor (a) and
  the cheap end (b) at 64.
- **FR-086** Worst-case cost is `O(agentCount²)` pairs plus `O(resourceCells · agentCount)` cell
  visits per step: at the capacities of FR-004 that is 1 128 pairs + 4 608 cell visits per step. The
  `w < 1e-6` cutoffs (FR-012, `ecosystem-sim.js:423`) cut the *work*, not the *visits* — at the
  defaults only ~34 of 496 pairs survive (`FINDINGS.md:254`: 5.7 M pair operations over a 30-minute
  run at 93.75 Hz).
- **FR-087** **Order of operations within one simulation step** (Clarifications, Q2). The FR body
  above states every rule but not their sequence, and the sequence is load-bearing
  (`ecosystem-sim.js:236-525`): regrowth runs before grazing, grazing reads pre-exchange energies and
  pre-move positions, and movement integrates forces computed from start-of-step energies. Every
  step executes, **in this exact order**:
  1. Non-finite guard and repair (FR-083).
  2. A single pass over all surviving pairs (FR-012) that **records** each pair's desired exchange
     flow (FR-020), affinity force (FR-030), crowding force (FR-032) and synchronization phase term
     (FR-035) — none of these is applied yet.
  3. Exchange pass two (FR-023): each recorded flow is scaled by `min(scale_i, scale_j)` and applied,
     above the refuge floor (FR-022).
  4. Appetite gate (FR-050), from each agent's phase at the start of the step.
  5. Proportional regrowth, pool → cells (FR-053) — **before** grazing, so a grazed cell cannot be
     refilled within the same step it was grazed.
  6. Demand-scaled grazing plus foraging, cells → agents (FR-051, FR-033).
  7. The optional global feed, plus nonlinear leak (FR-057, FR-052).
  8. Energy integrate, with the zero clamp and the carrying-capacity clamp (FR-055).
  9. Movement (the recorded affinity/crowding/foraging forces, FR-034) and torus wrap (FR-012).
  10. Intrinsic-frequency drift (FR-013).
  11. Phase integrate (FR-014).
  12. Pool update (the net of every pool-touching rule above).
  13. Publish outputs (FR-060, FR-061, FR-062, FR-063).
  A future edit that reorders these stages is a spec amendment, not an implementation detail — the
  prototype's own findings (regrowth-before-grazing, FR-053; exchange in two passes, FR-023) are
  order-dependent results, not order-independent rules that happen to have been measured in this
  sequence.

### FR-090 series — Shared components stay untouched

- **FR-090** This phase modifies **no** shipped header. `modulation_engine.h`,
  `modulation_matrix.h`, `voice_mod_router.h`, `voice_mod_types.h`, `modulation_source.h`,
  `modulation_types.h`, `envelope_follower.h`, `slow_event_scheduler.h`, `noise_organism.h`,
  `resonance_drift_network.h`, `feedback_ecology.h`, `subharmonic_engine.h`, `bloom_engine.h`,
  `harmonic_cloud.h` and `atmosphere_engine.h` are **byte-unchanged**, and this file includes none of
  them. Seraphis's and Vorago's existing suites must stay green (roadmap lines 550–552).
- **FR-091** The only repository files this phase adds or edits are: the new header, four new test
  TUs **plus one test-local metric helper header beside them**, their lines in
  `dsp/tests/CMakeLists.txt`'s enumerated `dsp_systems_tests` list (plus one line in the
  `-fno-fast-math` block for the non-finite TU), **one `#include` line in `dsp/lint_all_headers.cpp`**,
  and this spec's own directory.

  *(Enumeration corrected at implementation time, two additions, both recorded here rather than left
  as an undeclared file in `git status`:*
  1. *`dsp/tests/unit/systems/ecosystem_metrics_test_helpers.h` — the verdict function and the
     activity / frozen / pairwise-correlation / cycle statistics are needed by **both** the behaviour
     TU (SC-002, SC-004, SC-010) and the longrun TU (SC-003, SC-005, SC-013, SC-017, SC-018), and two
     copies of ~150 lines of statistics are how a criterion quietly stops measuring what it claims.
     It takes **no CMake entry** — the `dsp_systems_tests` list names `.cpp` only — and the pattern
     has two precedents in this tree: `dsp/tests/unit/processors/arpeggiator_core_test_helpers.h` and
     `dsp/tests/unit/systems/harmonic_cloud_pre_amendment_fingerprints.h`. Plan S14 D-D.*
  2. *`dsp/lint_all_headers.cpp` — one `#include <krate/dsp/systems/ecosystem_engine.h>` line. This
     is the house's standalone-compile lint: every shipped header is included there exactly once, and
     a new header omitted from it is never compiled outside its own test TU. Every prior Vorago phase
     added the same single line (`bloom_engine.h` for Phase 7, immediately above it). FR-090 is
     untouched by this: no **shipped DSP component header** is modified, and this file includes
     rather than alters them.)*

---

## Success Criteria

Each criterion names its measurement and a test-name sketch. TU allocation follows the Phase 5/6/7
shape: `ecosystem_engine_test.cpp` (behaviour), `ecosystem_engine_longrun_test.cpp` (the `[long]`
accelerated simulations and the fuzz), `ecosystem_engine_perf_test.cpp` (`[.perf]`),
`ecosystem_engine_nonfinite_test.cpp` (the **only** TU in the `-fno-fast-math` block,
`dsp/tests/CMakeLists.txt:569`, `:921`).

**"Accelerated" means stepping the simulation directly** — no audio is rendered anywhere in this
phase. At the FR-082 default the control rate is 93.75 Hz, so **900 simulated seconds = 84 375 control
steps**, 1800 s = 168 750, and the SC-018 overnight soak of 28 800 s = **2 700 000 steps**. A single
run of any of these costs seconds, not minutes; it is the *multi-configuration sweeps* that are
expensive, and those carry `[long]` and a stated runtime budget.

- **SC-001 — Boundedness and conservation under hostile rule fuzzing** (roadmap line 393; the
  theme-level FR, roadmap lines 532–535).
  **1000 seeded configurations** (the roadmap's count, line 393) drawn from the hostile box
  (`run.js:302-332`: every knob over its full plausible range, including dead-by-construction values),
  each run for **900 simulated seconds** — the duration the prototype's 500/500 reference was actually
  measured at (`run.js:401`, `const seconds = 900; // >= late window + settle`), i.e. **84 375 control
  steps per configuration, ~8.4e7 steps for the batch**. A second **sub-batch of 50 configurations at
  1800 s** covers the longer horizon without multiplying the batch. The C++ hostile box is the cited
  box with **one documented omission**: the prototype's `dimensions` knob (`run.js:305`, randomised
  1/2) is dropped because FR-012 fixes the habitat as 2-D, so the prototype's 500/500 figure covers a
  **superset** of the shipped geometry.
  **Runtime budget:** the batch must complete in **≤ 60 minutes** single-threaded on the nightly
  `[long]` lane — written as 30 minutes; **amended to 60 by the user on 2026-09-16** from the measured
  table: 32.9 min run alone and 43 min after a full suite (Release, pinned), every clause green
  (worst drift 2.8e-12, 0 violations, 0 containments, 92 812 500 steps, 998 750 perturb calls), and
  the exact per-step CPU levers exhausted (SC-011's record). Count, durations and schedule unchanged. Reference for the estimate: the prototype's JS runner takes ~16.5 s per 900 s hostile
  config (measured: `node run.js fuzz 8` = 131.7 s wall clock), so 1000 configs is ~4.6 h in
  JavaScript and the C++ port must be roughly an order of magnitude faster to fit. If it does not fit,
  FR-085's stop-and-surface rule applies: **the config count and the duration are not to be shrunk** —
  reduce per-step cost, or put the measured wall-clock table to the user.
  For every configuration assert: (a) **zero** non-finite values in any agent energy,
  position, phase or the pool, tested by bit pattern; (b) `getConservationViolationCount() == 0` —
  the FR-056 counter, meaning **the pool never went negative**, and *separately*
  `getNonFiniteContainmentCount() == 0` (FR-083), so the two failure modes cannot be confused;
  (c) relative total-energy drift
  `|total − energyBudget| / energyBudget <= 1e-9` at every sampled step; (d) **with a seeded
  `perturbAgent` schedule running throughout** — randomised indices (including out-of-range),
  randomised amplitudes spanning `[-2, +2]` so the `clamp(amount, -1, 1)` of FR-071 is exercised past
  both ends, non-finite amounts built from bit patterns, and calls deliberately aimed at an agent at
  energy 0 and at `capacity` — clauses (a), (b) and (c) still hold. Clause (d) exists because FR-071
  is the phase's only external energy-injection API and the hook Phase 10 drives in production; a sign
  or headroom error there breaks the invariant the whole boundedness argument rests on.
  Prototype reference under the same rules, at 900 s: **500/500 with 0 non-finite, 0 pool-negative,
  0 unbounded**, drift 2.4e-15–1.9e-13 (`FINDINGS.md:21-23`, `:295-300`). A regression in either guard
  (FR-023, FR-054) fails here first.
  *`EcosystemEngine_HostileFuzzStaysBounded` `[long]`,
  `EcosystemEngine_PerturbAgentConservesUnderFuzz` `[long]`.*
- **SC-002 — Liveness at the defaults** (roadmap line 394's "no frozen fixed points", restated as a
  metric that works — `FINDINGS.md:158-162`).
  **Note on kind assignment (Clarifications, Q6):** FR-011's kind draw is stratified, not the
  prototype's unconstrained i.i.d. draw. Every prototype reference figure quoted in this criterion is
  therefore a comparison baseline only and **must be re-measured** in C++ against the stratified
  histogram at implementation time; a measured deviation is a finding to surface (FR-085's
  stop-and-surface rule), not a threshold to move without recording why.
  **Two distinct things are defined here, and they are not the same number.**

  **The verdict function** (used by SC-003, SC-010 and SC-013 to classify *arbitrary* configurations,
  and by nothing else): over the **last 600 s of a run of at least 900 s**, on a fixed 1 Hz sample
  grid, a configuration is *alive* when (i) per-agent activity `std(e_i)/grandMean` averaged over
  agents is **≥ 0.10**, (ii) the count of frozen agents (activity < 0.02) is **≤ 25 %** of the
  population, **and (iii)** (Clarifications, Q3; series amended by the 2026-09-16 ruling) **the
  population-mean `getAgentOutput` series** (the mean over agents at every sample) **exhibits no
  short limit cycle**, by the same recurrence test SC-005 defines — scan lags upward until
  autocorrelation first falls below 0.2, then the maximum afterward must be **≤ 0.8**; a series that
  never decorrelates within half the verdict window passes, subject to SC-005's zero-variance guard —
  applied here over the verdict function's own window rather than SC-005's full 1800 s run.
  Clause (iii) is stated on published **output**, never on `getEnergyEntropy()` (FR-066 and D-9 forbid
  gating on entropy); this is a deliberate difference from the prototype, whose own cycle clause ran
  on the entropy series instead (see SC-013's note below). **Why the population mean and not each
  agent (ruling 2026-09-16, T023):** scanned per agent the clause flagged FR-050's *designed*
  oscillation — each agent's appetite is phase-gated at its own intrinsic frequency (periods 56–667 s
  at the defaults), so its output decorrelates at a quarter period and recurs at one period; measured
  at the defaults 3 of 32 agents above 0.8 (worst 0.848 at 109 s) with activity 0.42–0.48, zero frozen
  and pairwise |corr| 0.17–0.19, and 127 of 500 sane-box configurations dead on that clause alone. The
  limit cycle the roadmap forbids is the ecosystem's — the population locked into a common boom/bust
  — which is what the population mean shows and what independent intrinsic oscillations average out
  of; the prototype's clause was population-level too. Per-agent recurrence is **reported** by SC-005. Clauses (i)–(ii) are the prototype's
  classification thresholds verbatim (`ecosystem-sim.js:570-583`, `kAliveActivity = 0.10`,
  `kAliveMaxFrozenFraction = 0.25`, consumed by `liveness()` at `:576-583` to bucket fuzz runs) and,
  together, they are **not** a defaults gate: an implementation landing at activity 0.11 with 12 of 48
  agents permanently frozen would satisfy them, which contradicts roadmap line 394's "no frozen fixed
  points" outright.

  **The defaults gate** (this criterion, three seeds, all must pass, 1800 s run):
  (a) late activity **≥ 0.30** — the prototype measured **0.44** at the defaults
  (`FINDINGS.md:252`), so this is a ~30 % margin for toolchain and sampling variation, not a
  re-derivation; (b) **frozen count == 0** exactly (prototype: **0 of 32**), not 25 %;
  (c) **spatial non-collapse**: the number of distinct agent positions, rounded to 2 dp — the metric
  `FINDINGS.md:210-214` used — is **≥ 0.8 × agentCount** (prototype: 31 of 32 with crowding, **6** of
  32 without), with a `crowding = 0` control arm asserted to **fail** that bound, so the metric is
  demonstrated to discriminate FR-032 rather than assumed to; (d) **the published outputs are not
  railed**: the mean over agents of `getAgentClampedStepFraction(i)` across the late window is
  **≤ 0.05**, and no single agent exceeds **0.25**. Clause (d) is the bridge between this criterion's
  measurand and the consumer's: (a)–(c) are stated on raw energy `e_i` because that is the conserved
  quantity every rule acts on and the quantity every prototype figure was measured on, but the only
  value a consumer ever sees is FR-061's clamped output, which saturates at twice the mean share —
  without (d) a configuration whose published outputs are constant could pass (a) while
  `std(e_i)/grandMean` reported its railed agent as the liveliest in the run. The C++ figure for (d)
  is transcribed into this criterion at implementation time from the measured defaults run; a
  measured value above the threshold is a finding to surface, not a threshold to move.
  Any deviation from (a)–(d) is surfaced under FR-085's stop-and-surface rule rather than absorbed by
  widening the gate toward the verdict function's 0.10 / 25 %.
  *`EcosystemEngine_LateWindowLiveness`, `EcosystemEngine_AgentsDoNotCollapseSpatially`,
  `EcosystemEngine_OutputsAreNotRailed`.*
- **SC-003 — The liveness verdict does not depend on how long you looked.**
  The same configuration at 600 / 900 / 1200 / 1800 / 3600 simulated seconds, three seeds: **SC-002's
  verdict function** returns *alive* at every length, and the spread of late activity across the five
  lengths, stated as **`(max − min) / mean` < 0.20**, holds. The statistic is written out because
  "varies by < 20 %" has three incompatible readings — `(max−min)/min`, `(max−min)/mean` and
  `max/min − 1` — which differ by up to a factor of two on the prototype's own numbers. Computed the
  same way, the prototype's 0.430 / 0.434 / 0.433 / 0.442 / 0.459 gives
  `(0.459 − 0.430) / 0.4396 = 0.066`, so the C++ result is comparable to a real figure.
  This criterion exists because round 1's "current best configuration" was a decaying
  transient whose whole-run activity of 0.32 came entirely from its first ten minutes, and whose
  verdict flipped between 1200 s and 1800 s (`FINDINGS.md:177-190`). Prototype reference:
  0.430 / 0.434 / 0.433 / 0.442 / 0.459, zero frozen at every length (`FINDINGS.md:256-258`).
  *Record (2026-09-16): with the verdict's cycle clause per agent, 5 of 15 cells read "not alive"
  on that clause alone while activity was 0.42–0.48 and zero agents frozen in every cell; under the
  population-mean clause (ruling, SC-002 (iii)) alive in 15 of 15, spreads 0.078 / 0.080 / 0.112,
  seed-mean 0.434 / 0.435 / 0.440 / 0.450 / 0.467.*
  *`EcosystemEngine_LivenessIsDurationStable` `[long]`.*
- **SC-004 — Decorrelation: the agents are a bank, not one signal copied.**
  (a) **Inter-agent**: over the last 600 s of an **1800 s** run at the defaults, mean pairwise
  `|corr(e_i, e_j)|` **≤ 0.35** (prototype 0.18, measured at 1800 s — `FINDINGS.md:252`; the failure
  regime this guards against measured 0.84–0.99, `FINDINGS.md:62-63`). (b) **Seed-blindness, measured
  relatively — never against an absolute number** (Clarifications, Q7). Over **900 s** runs
  (`run.js:575-611`), the duration its reference figures were measured at: the **within-run floor** is
  computed over the **full 900 s** run (not the last-600-s window clause (a) uses), on a fixed
  **~1 Hz** sample grid, on agent **energies** `e_i` (not output, not entropy), as the mean
  `|corr(e_i, e_j)|` between different agents of the **first** (seed 0) run. Across 8 seeds, the mean
  per-agent cross-seed `|corr|` (same quantity, same grid, same 900 s duration) must be **≤ 1.5 ×**
  that within-run floor. The absolute form is explicitly forbidden by the prototype: these signals
  decorrelate over ~150 s, so a 900 s window holds only a handful of independent samples and unrelated
  slow signals correlate high by chance — judged against a fixed 0.5 threshold the *passing*
  configuration was declared "SEED-BLIND" (`FINDINGS.md:130-145`). Reference at the shipped (round-2)
  defaults: cross-seed **0.13** against a within-run floor of **0.12** (`FINDINGS.md:278-281`,
  `run.js:620-628`) — "a comparison of two noisy estimates should not flip on a hair." **The round-1
  figures — 0.61 at 900 s, 0.50 at 3600 s (`run.js:600-604`) — are struck from this criterion**: they
  were measured under different, no-longer-shipped defaults and do not apply to the 1.5× gate.
  *`EcosystemEngine_AgentsDecorrelate`, `EcosystemEngine_SeedsProduceDifferentVoices`.*
- **SC-005 — No short limit cycle, by recurrence** (roadmap line 395: "no limit cycles shorter than
  N minutes").
  For the **population-mean** `getAgentOutput` series (the mean over agents at every sample; the
  2026-09-16 ruling, see SC-002's clause (iii) for the measured reason) over an **1800 s** run: scan
  lags upward until autocorrelation first falls **below 0.2** (the signal forgets itself); only
  **after** that lag, the maximum autocorrelation must be **≤ 0.8**. A series that never decorrelates
  within half the run passes — it is one slow trend, the opposite of a short cycle. Every agent's own
  series is scanned and **reported** (count recurring, worst post-decay peak and lag); recurrence at
  an agent's intrinsic period is FR-050's design, not a defect. *Record, first measurement with the
  gate per agent (2026-09-16): 3 of 32 flagged, worst 0.848 at 109.3 s (agent 1, decorrLag 25.1 s),
  0 constant, 0 never-decorrelated. Under the ruling, the population-mean output: not cycled,
  post-decay peak 0.188 at 17.0 s, late window not constant — the compliance record.* **That escape clause is guarded, not
  open:** a series railed at FR-061's clamp also never decorrelates, so this criterion alone would
  award a constant output a pass; SC-002 (d) gates clamp engagement at the same defaults, and a series
  whose sample variance is zero over the late window fails here outright rather than taking the
  escape. The naïve "maximum autocorrelation over all lags
  ≥ 10 s" form is forbidden by name: for any smooth signal that maximum always sits at the shortest
  lag scanned, and it reported "limit cycle: 0.909 at lag 9.0 s" for a run whose scan merely started
  at 9 s — it was measuring smoothness (`FINDINGS.md:164-174`). Prototype reference: worst post-decay
  peak **0.175 at 557 s** (`FINDINGS.md:253`).
  *`EcosystemEngine_NoShortLimitCycle` `[long]`.*
- **SC-006 — Determinism harness** (roadmap line 395).
  (a) Same seed, same binary, two instances stepped 100 000 control steps: **bit-identical** agent
  energies, positions, phases and pool. (b) Seeds differing by 1 diverge: per-agent cross-seed
  `|corr|` under SC-004 (b)'s relative bound, and adjacent-seed entropy correlation `|ρ| <= 0.2`
  (prototype −0.02, `FINDINGS.md:281`). (c) `reset()` reproduces `prepare()`'s state exactly.
  (d) **No bit-exact float golden is checked in** (roadmap line 546): any cross-run comparison that
  is not same-binary uses `tests/test_helpers/render_fingerprint.h` tolerances
  (`kSampleTolerance = 5.0e-4f`, `kMetricTolerance = 2.5e-4`). `tools/lint-float-bit-goldens.js` must
  pass.
  *`EcosystemEngine_DeterministicUnderSeed`.*
- **SC-007 — Zero allocation after prepare, and none in prepare either.**
  `AllocationScope` (`tests/test_helpers/allocation_detector.h:111`) around `prepare()`, 10 000
  `processChunk` calls, every setter, `reset()`, `setSeed()` and `perturbAgent()`: **zero**
  allocations. `getAllocatedBytes() == 0`.
  (b) **The unprepared object (FR-007)**, which no other criterion covers: on a
  default-constructed instance, call `processChunk()` with several sizes, every setter and every
  getter **before** `prepare()`, and assert `isPrepared() == false`, `getAllocatedBytes() == 0`, and
  that every getter returns its documented neutral (`0.0f` for floats, `0` for sizes and counters,
  `Kind::Partial` for kinds). Then repeat every indexed accessor with out-of-range indices — before
  and after `prepare()` — and assert the same neutrals, no crash and no allocation. FR-007 specifies
  concrete behaviour and without this arm its compliance row would have no evidence to cite.
  *`EcosystemEngine_NoAllocationAfterPrepare`, `EcosystemEngine_UnpreparedIsNeutral`.*
- **SC-008 — Block-partition invariance** (FR-081).
  State after 1 000 000 samples delivered as one call, as 512-sample blocks, as 64-sample chunks and
  as a seeded irregular partition (1, 7, 513, 63, …) is **bit-identical** in all four cases.
  *`EcosystemEngine_BlockPartitionInvariance`.*
- **SC-009 — Non-finite injection is survived, under `-ffast-math`.**
  In the `-fno-fast-math` TU only:
  (a) every setter fed NaN/+Inf/−Inf **built from bit patterns via a
  volatile sink** (the house pattern — `std::numeric_limits` folds to finite garbage under
  `-ffast-math`) leaves the previous value standing;
  (b) the friend probe injects a non-finite agent energy, a non-finite cell energy and a non-finite
  pool directly, and the next step leaves every published output finite and in `[0, 1]` with
  **`getNonFiniteContainmentCount()`** incremented (FR-083) and `getConservationViolationCount()`
  **unchanged** — the two counters are asserted separately, because one counter with two meanings
  cannot say which failure mode fired. After the injection, the conserved total is back inside
  SC-001 (c)'s 1e-9 relative bound (FR-083's repair rule), and **N ≥ 1000 further steps produce
  finite, in-range outputs that CHANGE** — at least one agent's published output differs from its
  value at the containment step — so an implementation that merely abandons the write and freezes
  forever at its last published values fails here rather than passing as "contained";
  (c) **every divisor-valued knob is driven to its extremes through the public API**, not only through
  the probe: `prepare()` with `energyBudget` of 0, negative, `1e-300`, `1e300` and non-finite, with
  `agentCount` 0 and `SIZE_MAX`, with `resourceCells` 0, and with a non-finite sample rate — each is
  clamped per FR-005 and the resulting `getAgentOutput` values are finite and in `[0, 1]`. Without
  this arm FR-061's division by `energyBudget` is reachable at zero through documented API.
  *`EcosystemEngine_NonFiniteInputsRejected`, `EcosystemEngine_NonFiniteStateIsContained`,
  `EcosystemEngine_DivisorKnobExtremes`.*
- **SC-010 — Sample-rate independence, step-interval band, and re-prepare.**
  Both halves of this criterion apply the **same** standard, because changing the sample rate from
  48 kHz to 96 kHz halves `dt` exactly as changing `stepIntervalChunks` from 16 to 8 does, and gating
  one while exempting the other was incoherent.
  (a) **Gated, on the verdict:** the same configuration run for 1800 simulated seconds on **three
  seeds** at 44 100 / 48 000 / 96 000 Hz, and at `stepIntervalChunks` of 8 / 16 / 32 at 48 kHz (the
  floor and two slower settings; the band was 4 / 8 / 16 until the 2026-09-16 rulings set FR-082's
  floor at 8) — SC-002's *verdict function* returns *alive* in every one of those cells.
  (b) **Gated, exactly:** the number of simulation steps is within one step of
  `duration · sampleRate / (stepIntervalChunks · 64)` in every cell, and `prepare()` called a second
  time with a different rate is clean (no stale `dt`, no drift in the residue clock).
  (c) **Reported, not gated:** the late-activity value in every cell, plus its cross-seed spread,
  printed as a table. No numeric band is asserted here, because none has been measured: every
  prototype figure was taken at a single `dt` (`ecosystem-sim.js:619`, `blockRate = 48000/512`) and
  the only spread data that exists (0.430…0.459, `FINDINGS.md:256-258`) is *within*-seed across run
  lengths, not across `dt`. Asserting a 10 % band on a statistic whose seed-to-seed spread has never
  been measured would be a coin flip. Once the C++ table exists, a band **derived from the measured
  cross-seed spread in the same test** may be added by amendment; if the rule set turns out to be
  `dt`-sensitive within the band, that is a finding to surface under FR-085's rule, not to hide.
  *`EcosystemEngine_SampleRateIndependent`, `EcosystemEngine_StepIntervalBand`.*
- **SC-011 — CPU ≤ 0.5 % per instance** (roadmap line 395; FR-085).
  ns per 512-sample block at 48 kHz, best-of-25 × 500 blocks after 400 warm-up blocks, `[.perf]`,
  **run alone** via `node tools/run-cpu-tests.js dsp_systems_tests`. Worst-case rule configuration:
  `agentCount = 48`, `resourceCells = 96`, `kernelSigma = 0.35` so every pair survives the cutoff.
  Gate: **≤ 53 333 ns/block at BOTH ends of FR-082's retained range** —
  (a) `stepIntervalChunks = 8` (the floor **and** the default since the 2026-09-16 rulings: one step
  per block, the dearest legal configuration), and
  (b) `stepIntervalChunks = 64` (the cheap end, one step per eight blocks). (b) cannot fail where (a)
  passes; it is retained so the gate states the ceiling at every legal step interval, as FR-085
  does. If (a) cannot be met, FR-085 names the only permitted responses — reduce cost, or put the
  table to the user; with the floor at the default there is nothing left to narrow.
  (c) **The two exact levers are exact.** On the component's own state and through its own member
  functions: FR-040's cell weight by the E-1 recurrence equals `std::exp(−d²/(2σ²))` to **≤ 1e-12
  relative** over every in-range (cell, agent) visit at σ ∈ {0.01, 0.03, 0.12, 0.35} and cell counts
  {1, 2, 7, 64, 96}, with the multiply path exercised at least 4 000 times; FR-035's
  `sin(2π(phase_j − phase_i))` by the E-2 identity equals `std::sin` to **≤ 1e-14 absolute** over all
  1 128 pairs, and FR-050's table entry equals `std::sin(2π·phase_i)` exactly.
  A stage probe reports, as a non-gating
  table: pair loop / grazing loop / integrate / output publication, at `stepIntervalChunks` 8 and
  64, plus the defaults configuration and an all-dormant configuration.
  *`EcosystemEngine_CpuBudget`, `EcosystemEngine_StageCostProbe`,
  `EcosystemEngine_ExactIdentitiesMatchFormulas`.*
  **Record — first measured table, 2026-09-16** (pinned to P-cores, alone, best-of-25 × 500 blocks,
  floor still 1, before E-1/E-2): (a) **55 274.8 ns/block** (1.04× over), (b at 1)
  **404 613.6 ns/block** (7.59× over); stage probe: grazing loop 32 452 ns (4 608 cell `exp`), pair
  kernel 11 120, Kuramoto `sin` 6 680, traversal 3 358; Appendix-A defaults 8 143 (15 % of the
  ceiling); all-dormant −4 % of the worst case (FR-072's prediction held). The plan's projection had
  blamed the pair `exp`s; the probe put two thirds of the step in the cell loop, which is why E-1 is
  the lever that mattered. The first ruling followed from this table.
  **Record — after E-1/E-2/E-3, floor 4** (four pinned runs): (a) at 8 **26 576–28 796 ns/block**
  (50–54 % of the ceiling); (b) at 4 **56 587–60 176 ns/block** (1.06–1.13× over); stage probe at 8:
  grazing loop 17 487, pair kernel 11 166, Kuramoto term −213 (noise: the sines are gone),
  traversal 2 690; defaults 6 387. The second ruling (floor 8) followed from this table.
- **SC-012 — The fuzz harness cannot silently stop testing a rule.**
  The C++ fuzz asserts **coverage** over the `PrepareConfig` knob set:
  (a) the harness carries a **static knob list** and asserts its length **equals the number of numeric
  fields in `PrepareConfig`** (affinity matrix entries counted as one knob), so a knob added later
  cannot be omitted by silence — this replaces a "present in every generated config" clause, which is
  vacuous in C++ where a struct field always exists; the real failure mode is a fuzzer that never
  *writes* a field and leaves the default standing, which is exactly what put `predation` at the
  exchange-disabling 0.5;
  (b) for every knob **not on the exemption list**, `min < max` across the batch, or the test fails
  outright;
  (c) the **exemption list is a named, justified constant in the test**, not an implicit choice, and
  is asserted to be a subset of the static knob list — so a knob can never be exempted by omission.
  The list is normative and is exactly: **`energyBudget`** (pinned at its default in both boxes — it
  is FR-006 prepare-time, it is SC-001 (c)'s conservation reference, and varying it would make the
  relative drift figures incomparable across configurations; matching `run.js:415`, which exempts it
  in both boxes) and **`stepIntervalChunks`** (pinned at 8, the tuned `dt`; the prototype has no such
  knob, and SC-010 (a) gates the band instead), plus, **in the sane box only**, **`feedRate`** (pinned
  at 0 by design, `run.js:363`, exactly as `run.js:415` exempts it there). No other exemption may be
  added without amending this criterion.
  This is structural, not stylistic: the prototype's first fuzzer silently omitted `predation`,
  `capacity`, `leakExponent` and the whole resource field, so predation sat at its 0.5 default — **the
  exact value that zeroes the exchange rule** — and reported "0 unbounded / 1000" while never visiting
  the regime already known to break conservation (`run.js:381-387`, `FINDINGS.md:327-328`).
  *`EcosystemEngine_FuzzCoverageIsComplete`.*
- **SC-013 — The macro-reachable region is mostly alive.**
  500 configurations from the **sane** box (`run.js:338-377` — every knob within the band a Phase-10
  macro would plausibly reach), each run for **900 simulated seconds** — the duration the 83.0 %
  reference was measured at (`run.js:401`, `FINDINGS.md:284-293`), stated so the C++ figure is
  comparable to real evidence rather than to a number from a different regime: **≥ 70 %** *alive*
  under SC-002's **verdict function** (which is scoped to the last 600 s of a run of at least 900 s,
  so it references cleanly at this duration), and **0**
  unbounded. Prototype reference: **83.0 % alive (415/500), 0 unbounded** after narrowing
  `leakExponent` to [1, 1.3] and `appetiteDepth` to [0.6, 1] (`FINDINGS.md:284-293`). **That 83.0 %
  reference was produced by the prototype's own `liveness()` function, whose cycle clause was
  computed on the entropy series (`hSeries`, `ecosystem-sim.js:736-757`), not on per-agent output as
  SC-002's verdict function now requires (Clarifications, Q3)** — the C++ figure re-measures under the
  corrected, output-based cycle clause and may differ from 83.0 %; a divergence is a finding to
  surface (FR-085), not a threshold to move silently. *Record (2026-09-16): with the clause on
  per-agent output the C++ figure was **52.8 %** (264/500; deaths: 53 too quiet, 56 too frozen, 127
  on the cycle clause alone — the agents' own FR-050 oscillation, see SC-005), 0 unbounded, worst
  drift 5.1e-14, 13.6 min. The ruling moved the clause to the population-mean output; under it:
  **78.2 % alive (391/500)**, deaths 53 too quiet / 56 too frozen / **0** on the cycle clause,
  0 unbounded — the compliance record.* The test additionally **reports** the alive
  fraction per tercile of each knob, so Phase 10 inherits the death predictors rather than
  rediscovering them.
  *`EcosystemEngine_SaneBoxLiveness` `[long]`.*
- **SC-014 — Dormancy conforms to the cross-cutting rule** (roadmap lines 537–545).
  (a) `setAgentWake(i, 0)` and `setAgentDormant(i, true)` produce **identical** published output
  trajectories.
  (b) **The wake ramp, stated in the unit this component can actually produce.** A wake from 0 → 1
  reaches its target in exactly `rampSteps = max(1, ceil(0.050 s / stepDuration))` control steps
  (FR-070), **tolerance ±1 control step**, monotone non-decreasing, with no single step larger than
  `1 / rampSteps` of full scale. A "50 ms ± 1 ms" form is **unmeasurable here and is forbidden by
  name**: FR-062 recomputes the published value only at a control step, so at the FR-082 default the
  output changes only on a 10.667 ms grid and a 50 ms ramp completes at the 5th step = 53.33 ms — the
  measured duration is quantised to {42.7, 53.3} ms and can never land inside 50 ± 1 ms. The 50 ms
  figure is inherited from `noise_organism.h:178` (`kGainRampMs`), which is a *per-sample*
  `LinearRamp`; this component has no per-sample path at all (FR-002), so it structurally cannot
  conform to that rule as written and conforms to its intent instead. Asserted at
  `stepIntervalChunks` = 1, 8 and **64**: at 64 (4096 samples = 85.3 ms at 48 kHz) one step already
  exceeds 50 ms, so `rampSteps == 1` and the output reaches its target in a single step — the test
  asserts that single-step jump explicitly rather than asserting an unobservable "monotone ramp".
  (c) An output driven to `<= 1e-6` publishes **exactly `0.0f`** (FR-063).
  (d) **A dormant agent still lives in the economy**: with half the population dormant for 1800
  simulated seconds, SC-001's conservation clauses still hold **and the dormant agents' energies are
  measurably active** — their mean per-agent activity `std(e_i)/grandMean` over the late window is
  **≥ 0.30** (SC-002 (a)'s defaults threshold, on the same metric) **and within 0.5×–2× of the awake
  population's** mean activity in the same run. "Their energies still move" is not a criterion: any
  non-zero floating-point wiggle — one ULP of a neighbour's rounding — would satisfy it, including in
  an implementation that freezes the dormant agent entirely. This clause is the one place the spec
  argues *against* the cross-cutting Dormancy rule (FR-072's stated exception), so it carries the
  strongest available evidence rather than the weakest.
  *`EcosystemEngine_DormancyMatchesHouseRule`, `EcosystemEngine_DormantAgentsStayInTheEconomy`.*
- **SC-015 — Repository gates.** `node tools/lint-layers.js` (Layer 3 includes only Layer 0 +
  stdlib), `node tools/lint-odr.js`, `node tools/lint-nonfinite-symbols.js`,
  `node tools/lint-float-bit-goldens.js`, `node tools/check-portability.js` all clean;
  `./tools/run-clang-tidy.ps1 -Target dsp` clean; the four TUs present in the enumerated
  `dsp_systems_tests` list and the non-finite TU present in the `-fno-fast-math` block. No SIMD is
  introduced, so `lint-simd-aligned-loadstore.js` is vacuous but must still pass.
  *(CI/lint, not a Catch2 case.)*
- **SC-016 — No shared header moved** (FR-090).
  `git diff --stat` over the phase touches no file outside the new header, the four TUs and their
  test-local helper header, the `dsp/tests/CMakeLists.txt` lines, the one `#include` line in
  `dsp/lint_all_headers.cpp` and this spec directory (the enumeration FR-091 carries, corrected there
  at implementation time); `dsp_core_tests`, `dsp_primitives_tests`,
  `dsp_processors_tests`, `dsp_systems_tests`, `dsp_effects_tests` all green.
  *(Gate, verified in the compliance pass.)*
- **SC-017 — The refuge floor holds the predation cliff** (FR-022 — previously normative with no
  criterion that could see it).
  At the defaults with `predation` swept over **{0.6, 0.7, 0.85}**, three seeds, 1800 simulated
  seconds each: **frozen count == 0** at every point (the prototype's claim is "no agent freezes up to
  predation 0.85", `FINDINGS.md:224-226`), and no agent's energy sits at 0 for any part of the late
  window. A **`preyFloor = 0` control arm** at `predation = 0.7` must **fail** that bound — the
  prototype measured about a third of the agents parked at zero permanently there, "a cliff 0.05 away
  from the default" — so the metric is demonstrated to discriminate the floor rather than assumed to.
  The control arm is necessary because at the *defaults* the ablation shows removing the floor
  **raises** activity 0.522 vs 0.44, +18 % (`FINDINGS.md:274`): a missing or mis-scaled floor makes
  SC-002 **greener**, not redder, and SC-013 randomises `preyFloor` over [0.002, 0.02] alongside every
  other knob and gates only an aggregate. Without SC-017, an implementation that silently dropped
  FR-022 would pass every other criterion in this spec.
  *`EcosystemEngine_RefugeFloorPreventsPredationCliff` `[long]`.*
- **SC-018 — Overnight soak** (the theme-level cross-cutting constraint, roadmap lines 532–534: "a
  drone instrument that can run away or die overnight is broken by definition").
  **28 800 simulated seconds (8 hours)** at the defaults, three seeds, accelerated —
  **2 700 000 control steps per seed**, stated so the cost is on the record. Assert SC-001's clauses
  (a), (b) and (c) throughout, **and** SC-002's defaults gate (a) and (b) measured over the **final
  600 s** window: the component neither runs away, nor dies, nor drifts over an overnight hold. No
  other criterion in this spec reaches a drone-realistic duration — SC-001 runs 900 s, SC-003 tops out
  at 3600 s — and the risk is demonstrated rather than hypothetical: round 1's best configuration was
  a decaying transient whose verdict flipped between 1200 s and 1800 s (`FINDINGS.md:177-190`). There
  is no cost argument against it: 8.1 M steps at the defaults is a small multiple of a single SC-003
  sweep.
  *`EcosystemEngine_OvernightSoak` `[long]`.*
- **SC-019 — The published output anchor is invariant to budget and population** (FR-061, D-5 —
  promoted from an Edge Cases bullet, because the plan and tasks stages enumerate FR/SC and prose
  drops out silently).
  At `energyBudget` **0.1× / 1× / 10×** the default and `agentCount` **24 / 32 / 48** (nine cells),
  1800 s, three seeds, statistics pooled over the seeds' late 600 s at ~1 Hz:
  (a) the late-window **mean of `getAgentOutput` over the population is 0.5 ± 0.05** in every cell;
  (b) the late-window **5th percentile** agrees across all nine cells to within **±0.05** absolute;
  (c) every cell's late-window distribution sits inside the envelope **p05 ∈ [0.12, 0.24]**,
  **median ∈ [0.25, 0.55]**, **p95 ∈ [0.70, 1.00]**, and the fraction of late-window samples at
  FR-061's **upper rail is ≤ 0.20**;
  (d) the percentile ladder (p05 / p25 / p50 / p75 / p95), the rail fraction and `Σ energy_ /
  energyBudget` are **reported** per cell (plan R-5) and gated only through (a)–(c).
  This is the consumer contract Phase 10's depth mapping rests on — "independent of `agentCount` and
  `energyBudget`" — and SC-009 only asserts outputs stay finite and in `[0, 1]`, which a wrongly
  scaled anchor would also satisfy. The three clauses keep that tooth: dropping `agentCount` from
  FR-061's formula, doubling the anchor, or normalising by `resourceCells` instead of `agentCount`
  each breaks (a), (b) **and** (c) at once.

  **Clause (c) replaces this criterion's original second clause and that clause is STRUCK:** *"the
  population's late-window output range (5th–95th percentile) agrees across all nine cells to within
  ±0.05 absolute"*, together with the paragraph that claimed FR-008's share conversion made this
  criterion "invariant on **both** its axes by construction". Both were **false of the shipped rule
  set and of the prototype they were drawn from** — measured, not argued:
  * The implementation's nine cells (`dsp_systems_tests`, 1800 s × 3 seeds) give p05 ∈
    [0.174, 0.190] — **spread 0.0156, inside ±0.05** — but p95 ∈ [0.801, 1.000], **spread 0.199**,
    with the median spreading 0.156 and p25 0.064. The mean is invariant (0.460–0.487), which is why
    clause (a) survives untouched.
  * The **reference prototype reproduces those figures** (`prototype/ecosystem-sim.js`, same grid,
    share-converted knobs): p05 spread ~0.014, p95 spread ~0.21, p95 reaching 1.0 in the
    `B = 10 × n = 48` corner. The C++ is faithful to the proven rules; the criterion was not.
  * **Why the tail moves, in the spec's own normative rules.** *Budget axis:* FR-030's affinity
    force is scaled by the neighbour's **absolute** energy while FR-032's crowding repulsion is
    energy-**independent** and FR-034's `moveRate` / `maxSpeed` are absolute habitat units — FR-033
    states the asymmetry outright ("at a 1.0 budget over 32 agents a neighbour holds ~0.016, so the
    force never approached `maxSpeed`"). Every other rule is homogeneous of degree 1 in energy, and
    the ablation proves it: with `moveRate = forageRate = crowding = 0` the prototype's statistics
    are **identical to four decimals across B = 0.1 / 1 / 10**, and with movement on they are not.
    *Population axis:* `agentCount` agents in a **fixed** unit torus at a fixed `kernelSigma`,
    grazing a **fixed** `resourceCells` field, is a different interaction topology, not a rescaling:
    pair density per agent rises with `agentCount`, so predation (FR-020, default 0.55) concentrates
    energy harder and the upper tail widens. FR-008's share conversion (D-10) normalises the
    **thresholds** — refuge floor, carrying capacity, cell capacity, satiation — which is what keeps
    clauses (a) and (b) true on that axis; it cannot normalise the number of neighbours.
  * The struck clause's band also rested on an **arithmetic misreading**: FR-061's "±44 % swing maps
    to roughly `[0.28, 0.72]`" reads `FINDINGS.md:252`'s 0.44 as a population spread, but 0.44 is
    `std(e_i)/grandMean` **per agent over time**. The pooled band at the defaults is `[0.18, 0.92]`
    in both implementations — so the struck clause was unreachable even in the **default** cell,
    before any invariance question.
  **Finding on the record, not absorbed:** the rail fraction climbs from 0.03 % (`B = 1, n = 24`)
  to **10.7 %** in the `B = 10, n = 48` corner, where the mean also dips to its lowest 0.460. The
  corner is legal configuration, outside the defaults SC-002 (d) gates; clause (c)'s 0.20 ceiling
  carries ~2× margin over the measured worst cell, and a measured value above it is a finding to
  surface, not a threshold to move.
  *`EcosystemEngine_OutputAnchorIsScaleInvariant`.*
- **SC-020 — Exchange pass 2 is well-formed** (FR-023 — a probe-level assertion, because SC-001
  provably cannot discriminate it: a sign-reversed pair flow is still antisymmetric, so conservation
  and boundedness hold **exactly** while the exchange rule runs backwards).
  Through the friend probe, over 10 000 steps spanning the defaults, `kMinAgents = 1`,
  `preyFloor = 0.05` against the default mean share (so most agents sit **below** the floor), and
  `kernelSigma = 0.01` (so most agents have **no** surviving pair and `want_i == 0`): every
  `scale_i` is finite and in **`[0, 1]`**; `scale_i == 1.0` exactly whenever `want_i == 0` and **no
  division is performed** in that case; and for every applied pair the **sign of the applied flow
  equals the sign of its desired flow** (or the applied flow is exactly 0).
  *`EcosystemEngine_ExchangeScaleIsWellFormed`.*
- **SC-021 — The denormal cell snap returns its energy to the pool** (FR-043 — unobservable to
  SC-001, see FR-043).
  Drive a cell below the `1e-30` guard (`cellCapacity` at its Appendix A minimum, `grazeRate` at its
  maximum, `regenRate = 0`), then assert through the read surface and the probe: `getCellEnergy(k)`
  is **exactly `0.0`** (not subnormal, not `1e-234`), **no** cell holds a subnormal value anywhere in
  the field, and `getPoolEnergy()` increased by **exactly** the probe-recorded snapped amount. An
  implementation that drops the snapped energy on the floor passes every other criterion in this
  spec and fails here.
  *`EcosystemEngine_DenormalCellSnapConserves`.*

---

## Edge Cases

**RT-safety boundaries.**
- `processChunk(0)` applies the current state without advancing (FR-081) — it must not draw from any
  RNG stream, or determinism breaks under a host that delivers empty blocks.
- A `numSamples` larger than any plausible block (e.g. 1 000 000, an offline render) must advance the
  correct number of steps in **one** call without unbounded stack use and without overflowing the
  residue counter.
- Every setter is callable from the control thread **between** control steps; FR-062 means a setter
  storm cannot re-target a consumer's ramp per block.
- `getAllocatedBytes() == 0` is a contract, not a report: a future edit that adds a `std::vector`
  breaks SC-007 first.

**Parameter extremes.**
- `agentCount = 1`: no pairs exist. Exchange, affinity, crowding and sync are all no-ops; the single
  agent must still graze, leak and conserve. Activity is undefined, so SC-002 is not run here — the
  case asserts conservation and finiteness only.
- `resourceCells = 1`: the field degenerates to a global pool — the failure mode of
  `FINDINGS.md:59-68`. It must remain **bounded** (SC-001 covers it); it is not required to be
  *lively*.
- `kernelSigma = 0.01`: essentially no neighbours survive the `w < 1e-6` cutoff; the population is a
  set of independent grazers. Bounded, and decorrelated by construction.
- `kernelSigma = 0.35` (hostile-box max): every pair survives; this is SC-011's worst case and the
  regime where activity collapses to 0.06 and correlation rises to 0.84 (`FINDINGS.md:80-82`).
  Bounded, not lively — correct.
- `predation = 0.5` exactly: **the exchange rule is off** (FR-021). Must be bounded and must not be
  mistaken for "exchange tested".
- `predation = 1.0` with `exchangeRate = 3.0`: the regime that broke conservation before Guard 2; now
  holds to ~5e-14 in the prototype (`FINDINGS.md:110-112`). SC-001 must include it.
- `preyFloor >= capacity`: every agent is entirely refuge, so no exchange can occur. Bounded;
  exchange silently inert. `preyFloor > energyBudget / agentCount` is inside the hostile box.
- `capacity` below the mean share (`energyBudget / agentCount`): every agent saturates and spills
  continuously to the pool (FR-055). Bounded; the spill path is exercised.
- `energyBudget` scaling: outputs are normalised by `energyBudget / agentCount` (FR-061), so the
  published **anchor** — the population mean, and the lower band with it — is invariant to the budget
  — **gated by SC-019** at 0.1×, 1× and 10× crossed with `agentCount` 24 / 32 / 48. The **upper tail**
  is not, and SC-019 says why: the FR-030 affinity force scales with absolute energy against an
  energy-independent FR-032 crowding term and absolute FR-034 speeds, so the movement regime — and
  only the movement regime — moves with the budget. `energyBudget` is prepare-time (FR-006) and clamped to `[1e-3, 1e3]`
  (FR-005), so FR-061's division can never see zero; SC-009 (c) drives it to both clamps.
- `leakRate = 0` with `regenRate = 0`: nothing moves. Bounded, frozen, and correctly reported frozen.
- `cellCapacity` at its minimum with `grazeRate` at its maximum: cells empty every step and approach
  the FR-043 denormal guard; the snapped energy must return to the pool. **SC-021 gates this
  directly** — SC-001's relative 1e-9 drift bound is fourteen decades too coarse to see it, so
  "or conservation fails" was never true of any criterion here.
- `agentCount = 1` or `kernelSigma = 0.01`: most or all agents have **no surviving pair**, so
  FR-023's `want_i == 0` branch is the *normal* path, not an exotic one — SC-020 asserts no division
  is performed there.
- `preyFloor` above the mean share (the hostile box draws it to 0.05 against a mean share of
  0.021–0.042): most agents have `spare_i < 0`, which FR-023's guarded form sends to `scale_i = 0`.
  SC-020 asserts the scale never goes negative and no applied flow reverses sign.

**Sample-rate and timing changes.**
- `prepare()` at 44.1 / 48 / 96 / 192 kHz: `dt` changes, wall-clock behaviour does not (SC-010).
- A second `prepare()` with a different rate mid-life: residue clock and `dt` both refreshed, no
  stale state.
- A sample rate below `kMinUsableSampleRate` or non-finite: clamped, reported by the getter (FR-005).
- `stepIntervalChunks = 8` is both the floor and the default (step every 512 samples): the dearest
  legal configuration and the one a Phase-10 CPU regression would hit. **It is gated, not
  exempted:** FR-085 states the 53 333 ns/block ceiling at every legal step interval and **SC-011
  (a)** measures it at the worst-case rule configuration; (b) gates the cheap end at 64. (Floors of 1
  and then 4 were removed from the range by the two 2026-09-16 rulings rather than exempted: at 1 the
  worst case measured ~3.8 % of a core and at 4 still 1.06–1.13× the ceiling after every exact lever.)
- `stepIntervalChunks = 64` (4096 samples = 85.3 ms at 48 kHz): one control step is **longer than the
  50 ms wake ramp**, so FR-070's `rampSteps` floors at 1 and the published output reaches its target
  in a single step. SC-014 (b) asserts that single-step jump rather than an unobservable ramp.

**Seed determinism.**
- `setSeed(0)`: `Xorshift32::seed()` substitutes its default for 0 (`random.h:72-74`), and
  `deriveStreamSeed` substitutes `0x2545F491` for a zero hash (`:110`) — both substitutions must be
  exercised so two lanes cannot collapse onto one stream.
- Adjacent seeds (`n`, `n+1`) must not produce correlated voices (SC-004 (b), SC-006 (b)).
- `setSeed()` mid-run: documented as re-deriving the initial state (a re-`prepare()` in effect), not
  as re-seeding lanes in place — the two differ and the header must say which it is.
- The initial resource fill is seeded (FR-041): a test that zeroed it would recreate the shared
  opening transient that gave cross-seed correlation 0.60.

**Numerical.**
- Agent energies approach zero under heavy predation but never go negative (FR-022, FR-055).
- Cell energies decay geometrically toward the FR-043 guard; without the guard the prototype reached
  `1e-234`.
- `getEnergyEntropy()` with all energy on one agent returns 0, with a flat distribution
  `log2(agentCount)`; with total energy 0 it returns 0 rather than dividing by zero.

---

## Decisions taken where the roadmap is silent

- **D-1 — Outputs are an indexed read surface, not `ModulationSource` implementations.** Evidence in
  Overview correction 4 and the existing-components table: no consumer in the tree can accept 24–48
  sources (4 external slots in `ModulationEngine`, 16 in `ModulationMatrix`, a closed 8-entry enum in
  `VoiceModRouter`), while every Phase 2–7 consumer takes a plain `[0, 1]` scalar and says so in its
  own doxygen. Bound by FR-060; OQ-4 offers the user the adapter.
- **D-2 — The economy is `double`** (FR-084). SC-001's 1e-9 drift gate is unmeasurable in `float`.
- **D-3 — `EnvelopeFollower` is read and rejected** despite reuse row line 123. It senses an audio
  envelope on a millisecond time constant; there is no audio here and the agent's energy is already a
  smooth state variable on a 56–667 s time constant.
- **D-4 — Step interval defaults to 512 samples, not the 64-sample house grid** (FR-082), because
  that is the `dt` the rules were tuned at and because the 64-sample grid costs 8× the CPU for a
  simulation whose fastest process has a 56 s period. The residue clock still runs on the 64-sample
  grid so the component slots into the existing chunk loop. OQ-2.
- **D-5 — The published output is normalised to a 0.5 population-mean anchor** (FR-061) rather than
  handed over raw, so Phase 10's depth mapping is independent of `agentCount` and `energyBudget`.
  Gated by SC-019; the clamp's rail, which is the cost of the anchor, is gated by SC-002 (d).
- **D-6 — Dormancy gates the output, not the simulation** (FR-072), argued from conservation. OQ-5.
- **D-7 — Population, cell count, energy budget, step interval and the initial-pool fraction are
  prepare-time** (FR-006); only rule knobs are runtime (FR-064). `energyBudget` joins the set because
  it is simultaneously the conserved total, FR-061's output divisor and SC-001 (c)'s reference — a
  runtime setter for it would either break SC-001 (c) by construction or perform an unspecified energy
  creation event. `initialPoolFraction` joins it because it only acts inside FR-041's one-time initial
  partition (Clarifications, Q5). Habitat dimensionality is not in the set because it is not a knob at
  all: FR-012 fixes 2-D.
- **D-10 — Four energy-scale knobs are share-relative, converted once at `prepare()`** (FR-008,
  Clarifications Q1): `preyFloor`, `capacity` and `satiation` in units of the mean per-agent share,
  `cellCapacity` in units of the per-cell share. This is what keeps SC-019's **anchor** clauses (a)
  and (b) true on the `agentCount`/`resourceCells` axis and not only the `energyBudget` axis. It does
  **not** make the whole distribution invariant on that axis — it normalises the rule *thresholds*,
  not the number of neighbours a given `agentCount` produces in a fixed habitat; SC-019 carries the
  measurement and the correction.
- **D-11 — Agent kinds are stratified at `prepare()`, not drawn i.i.d.** (FR-011, Clarifications Q6):
  every kind gets at least one agent whenever `agentCount >= 5`. SC-002's defaults figures are
  re-measured against the resulting histogram rather than reused from the prototype's unconstrained
  draw.
- **D-8 — The strip resource geometry is shipped as proven** (FR-040), not silently upgraded to a
  2-D cell grid that no measurement covers. OQ-1.
- **D-9 — Entropy is reported, never gated** (FR-066, SC-002/SC-005), against roadmap line 394.

---

## Open Questions

The roadmap defers exactly one decision to this spec: **Open Question 2** — "Ecosystem rule set:
which agent kinds and interaction rules survive the offline prototype — Phase 8, after prototyping"
(lines 557–558).

**OQ-2 is RESOLVED by the prototype, and this spec encodes the resolution** (`FINDINGS.md:306-312`).
Surviving and load-bearing: phase-gated appetite (FR-050), movement as affinity + foraging (FR-030,
FR-033), exchange with a mildly predatory character behind a refuge floor (FR-020, FR-022, FR-023),
spatial resource with proportional regrowth and demand-scaled grazing (FR-040, FR-051, FR-053),
crowding repulsion (FR-032). Kept as knobs but **off by default** because they measurably hurt:
synchronization (FR-035) and satiation (Appendix A). Kept though decorative at the 2-D defaults,
because they are cheap and load-bearing elsewhere in the macro box: foraging, crowding, frequency
drift (FR-013). All five roadmap agent kinds survive (FR-011). The roadmap's
"attract / repel / synchronize / exchange" framing survives with **one correction: synchronize is a
macro, not a rule.**

Five sub-decisions inside that resolution were put to the user before implementation. **All five are
now RESOLVED** (Clarifications, session 2026-09-15) — every recommendation below was adopted verbatim
and each is already normative in the FR body cited; nothing here is still open.

- **OQ-1 — RESOLVED: ship the proven 1-D strip resource field inside the 2-D torus.**
  Verified: cells are positioned along x only and grazing/foraging use the x separation only
  (`ecosystem-sim.js:228`, `:421`, `:428`, `:490`), while agents move on a 2-D torus. Every number in
  `FINDINGS.md` was measured on that geometry. A 2-D grid costs the same number of cell visits but is
  **untuned and out of scope** — the activity/correlation figures would have to be re-established.
  Decision: ship the strip field (FR-040) and record the limitation (the "Two prototype-only defects"
  note above); a 2-D grid is deferred, to be revisited only if Phase 10 listening shows banding
  along y.
- **OQ-2 — RESOLVED: step interval default 8 control chunks (512 samples), range [8, 64] chunks.**
  512 samples is the `dt` every published figure was measured at. The range was resolved as [1, 64]
  on 2026-09-15; two 2026-09-16 rulings narrowed the minimum to 4 and then to 8 on SC-011's measured
  tables (64 samples cost 7.6× the ceiling at the worst case, 256 samples still 1.06–1.13× after
  every exact lever, FR-085). SC-010 tests an 8–32 chunk band. Normative at FR-082.
- **OQ-3 — RESOLVED: default population 32 agents, `kMaxAgents = 48`.**
  Every prototype figure is at 32; 48 raises pair cost 2.25×. Normative at FR-004 (`kMaxAgents = 48`)
  and Appendix A (`agentCount` default 32).
- **OQ-4 — RESOLVED: no `ModulationSource` adapter now.** `ModulationMatrix::registerSource`
  (`modulation_matrix.h:180`) could route up to 16 agents generically, but nothing in Vorago's planned
  wiring needs it — Phase 2–7 consumers take plain scalars — and the project rule is no speculative
  flexibility. The indexed read surface (FR-060) is the contract; an adapter is an append-only
  addition later if Phase 10 finds a use.
- **OQ-5 — RESOLVED: dormancy gates the published output while the simulation keeps running (FR-072),
  the only reading that preserves conservation.** It saves no CPU — which is what the cross-cutting
  rule's "skip the chain" clause was written to do — so FR-072 states the mechanism-level justification
  the rule demands (roadmap lines 543–545) rather than merely asserting compliance. This spec adopts
  FR-072 as written; recording it in the roadmap's own Dormancy paragraph as a second stated exception
  alongside Phase 5's FR-063 is a roadmap-file edit outside this spec's scope and is not made here.

---

## Traceability

| Roadmap statement | Line(s) | Covered by |
|---|---|---|
| New component L3 `systems/ecosystem_engine.h` | 379 | FR-001, new-components table |
| Offline Node.js prototype first; spec encodes the proven rules | 375–377 | De-risk gate, *Prototype provenance* |
| Fixed-capacity agent table, ~24–48 agents/voice | 381 | FR-004, FR-006, FR-010, OQ-3 |
| Agent = {kind, energy, position, rule params} | 381–382 | FR-010, FR-011, FR-012 |
| Control-rate only: sense → apply rules → write outputs | 383 | FR-002, FR-081, FR-082 |
| Outputs are ordinary modulation values via the existing contract | 384–386 | FR-060, FR-061, D-1, OQ-4, Overview correction 4 |
| Resonator agent → peak gain; noise agent → source wake; feedback agent → coupling | 386–388 | FR-060 doxygen; existing-components table (consumer rows); the mapping itself is Phase 10 (Non-Goals) |
| Conservation constraint: budgeted and leaky, bounded regardless of rules | 389–390 | FR-023, FR-052, FR-054, FR-055, FR-056, FR-071, SC-001, SC-018, SC-020, SC-021 |
| Deterministic under seed for golden testing | 391 | FR-080, SC-006 |
| Boundedness under random rule fuzzing (1000 seeded configs × accelerated) | 393–394 | SC-001, SC-012 |
| Non-triviality: energy-distribution entropy keeps changing over 30 min | 394 | **Corrected** — FR-066 (reported, not gated), SC-002, SC-004, `FINDINGS.md:148-162` |
| No frozen fixed points | 394–395 | SC-002 (defaults gate: activity ≥ 0.30, frozen == 0), SC-003, SC-013 (verdict function), SC-017, SC-018 |
| No limit cycles shorter than N minutes | 395 | SC-005 (recurrence form) |
| Determinism harness | 395 | SC-006 |
| CPU ≤ 0.5 % per voice (control-rate math only) | 395–396 | FR-085 (per block, at every legal step interval), FR-086, SC-011 (a)+(b) |
| Reuse row: `modulation_engine` (routing), `envelope_follower` (energy sensing) | 123 | Existing-components table (both read; both **not** consumed, with reasons), D-1, D-3 |
| ODR sweep before any new class | 127–129 | New-components table (sweep run this session) |
| RT safety; pools sized at prepare | 530–531 | FR-003, FR-005, SC-007 |
| Boundedness is the theme-level FR; worst-case soak | 532–535 | SC-001 (1000 × 900 s + 50 × 1800 s), SC-003, SC-013, **SC-018 (8-hour overnight soak — the drone-realistic duration the other criteria do not reach)** |
| Layer discipline + ODR sweep | 536 | FR-001, SC-015 |
| CPU budgets are FRs, measured in tests | 536 | FR-085, SC-011 |
| Dormancy rule, naming Phase 8 agents | 537–545 | FR-070 (ramp on the control-step grid), FR-071, FR-072, SC-014 (b) (ramp stated in control steps), SC-014 (d) (dormant activity measured), OQ-5 |
| No bit-exact float goldens | 546 | FR-080, SC-006 (d), SC-015 |
| Portability; WSL probe; aligned-load lint | 547–548 | FR-083, SC-015 |
| Shared-component changes keep Seraphis green | 550–552 | FR-090, FR-091, SC-016 |
| OQ 2 — which agent kinds and rules survive | 557–558 | *Open Questions* (resolved), FR-011, FR-020 … FR-057 |

**Requirement → criterion coverage added by the review pass** (each of these was normative with no
criterion that could see it; the right-hand column is the criterion that now discriminates it):

| Requirement | Was gated by | Now gated by |
|---|---|---|
| FR-007 (unprepared object is a neutral no-op) | *nothing* | SC-007 (b) |
| FR-022 (refuge floor) | *nothing* — and removing it makes SC-002 **greener** | SC-017, with a `preyFloor = 0` control arm |
| FR-023 (Guard 2, guarded scale form) | SC-001, which is blind to a sign-reversed antisymmetric flow | SC-020 (probe: scale ∈ [0,1], no division at `want == 0`, applied flow keeps its sign) |
| FR-032 (crowding repulsion) | *nothing* — −2 % activity is within noise in 2-D | SC-002 (c), with a `crowding = 0` control arm |
| FR-043 (denormal cell snap returns to pool) | SC-001, fourteen decades too coarse | SC-021 |
| FR-061 / D-5 (output anchor invariance) | an Edge Cases bullet (prose) | SC-019 |
| FR-061 (clamp rail hides a constant output) | *nothing* | SC-002 (d), via `getAgentClampedStepFraction` |
| FR-071 (perturbAgent conserves) | *nothing* — SC-001 never called it | SC-001 (d) |
| FR-083 (non-finite containment + repair) | SC-009, against a counter that meant two things | SC-009 (b), against `getNonFiniteContainmentCount()`, plus the "outputs still change" clause |

---

## Appendix A — Configuration defaults (the encoded rule set)

Transcribed from `prototype/ecosystem-sim.js:70-152` (`defaultConfig()`), which `FINDINGS.md:245-253`
records as the round-2 best configuration: 30-minute run at seed `0xC0FFEE`, energy drift 2.9e-15,
late-window activity **0.44**, **0 of 32** frozen, late pairwise `|corr|` **0.18**, worst post-decay
autocorrelation 0.175 at 557 s, 5.7 M pair operations. Ranges are the hostile-fuzz box
(`run.js:302-332`) unless noted.

| Knob | Default | Range | FR | Lifetime |
|---|---|---|---|---|
| `agentCount` | 32 | [1, 48] | FR-004, FR-010 | **prepare** |
| `energyBudget` | 1.0 | [1e-3, 1e3] | FR-005, FR-055, FR-061 | **prepare** |
| `initialPoolFraction` | 0.5 | [0.1, 0.9] | FR-041, FR-006 | **prepare** |
| `kernelSigma` | 0.03 | [0.01, 0.35] | FR-012 | runtime |
| `exchangeRate` | 0.35 | [0, 3.0] | FR-020 | runtime |
| `predation` | 0.55 | [0, 1] | FR-020, FR-021 | runtime |
| `preyFloor` | **0.5 shares** (0.016 abs at defaults) | **[0, 1.6] shares** | FR-022, FR-008 | runtime |
| `capacity` | **32 shares** (1.0 abs at defaults) | **[0.32, 32] shares** | FR-055, FR-008 | runtime |
| `leakRate` | 0.06 | [0, 1.0] | FR-052 | runtime |
| `leakExponent` | 1.0 | [1.0, 2.5]; macros ≤ 1.3 | FR-052 | runtime |
| `moveRate` | 0.20 | [0, 0.5] | FR-034 | runtime |
| `maxSpeed` | 0.03 | [0.001, 0.05] | FR-034 | runtime |
| `forageRate` | 0.010 | [0, 0.05] | FR-033 | runtime |
| `crowding` | 0.05 | [0, 0.2] | FR-032 | runtime |
| `crowdingRadius` | 0.02 | [0.005, 0.05] | FR-032 | runtime |
| `syncRate` | **0.0 (off)** | [0, 0.5] | FR-035 | runtime |
| `resourceCells` | 64 | [1, 96] | FR-040 | **prepare** |
| `cellCapacity` | **3.2 cell-shares** (0.05 abs at `resourceCells = 64`) | **[0.32, 12.8] cell-shares** | FR-042, FR-008 | runtime |
| `regenRate` | 0.05 | [0, 1.0] | FR-053 | runtime |
| `grazeRate` | 0.75 | [0, 3.0] | FR-051 | runtime |
| `feedRate` | **0.0 (off)** | [0, 1.0] | FR-057 | runtime |
| `satiation` | **0.0 (off)** | **[0, 16] shares** when nonzero | FR-051 (hunger term), FR-008 | runtime |
| `appetiteDepth` | 0.8 | [0, 1]; macros ≥ 0.6 | FR-050 | runtime |
| `freqLo` / `freqHi` | 0.0015 / 0.018 Hz | [0.0005, 0.005] / [0.006, 0.05] | FR-013 | runtime |
| `freqDrift` | 0.00004 | [0, 0.0002] | FR-013 | runtime |
| `affinity[k][k]` / `affinity[i][j]` | −1.0 / +0.45 | [−2, +2] | FR-031 | runtime |
| `stepIntervalChunks` | 8 (= 512 samples) | [8, 64] (min 8 since the 2026-09-16 rulings) | FR-082 | **prepare** |

**Lifetime column:** *prepare* = FR-006's prepare-time set, no runtime setter; *runtime* = FR-064's
setter/getter pair. The five *prepare* knobs are candidates for SC-012's normative fuzz-coverage
exemptions, but only `energyBudget` and `stepIntervalChunks` are actually exempted —
`agentCount`, `resourceCells` and `initialPoolFraction` **are** varied by both boxes and are not
exempt.
**Share units (FR-008, Clarifications Q1):** `preyFloor`, `capacity` and `satiation` are multiples of
the mean per-agent share `energyBudget / agentCount`; `cellCapacity` is a multiple of the per-cell
share `energyBudget / resourceCells`. The "abs at defaults" figures are what the share value converts
to at the Appendix A defaults (`energyBudget = 1.0`, `agentCount = 32`, `resourceCells = 64`) and are
shown only as a cross-check against the prototype's originally-published absolute numbers.

**Satiation** (`ecosystem-sim.js:139-147`) scales demand by `max(0, 1 − e_i / satiation)`. It is a
knob at default 0 because measurement made it harmful at the round-2 defaults: **−31 % activity and
higher correlation in 2-D** (`FINDINGS.md:234-236`, ablation row `:266`).

---

## Assumptions

1. **The prototype's rules transfer to C++ unchanged.** Justified by the RNG being a bit-exact port
   of `core/random.h` (`FINDINGS.md:14-15`) and by every rule being expressed in per-second rates
   multiplied by `dt`. Falsifiable: SC-001–SC-005 re-measure every published figure in C++, and any
   divergence is a finding to surface, not to paper over.
2. **Single accelerated runs are cheap; multi-configuration sweeps are not, and the two are costed
   separately.** At the FR-082 default a 900 s run is 84 375 control steps, a 1800 s run 168 750 and
   SC-018's 8-hour soak 2 700 000 — each ~34 surviving pairs plus 2 048 cell visits at the defaults,
   which the prototype runs in JavaScript in seconds. **The sweeps are the cost**: the prototype's JS
   runner takes ~16.5 s per 900 s *hostile* config (measured: `node run.js fuzz 8` = 131.7 s wall
   clock), so SC-001's 1000 configs would be ~4.6 h in JavaScript and needs roughly an order of
   magnitude from the C++ port to meet its stated ≤ 30 min budget. That budget is part of SC-001, so
   a miss is a surfaced finding — never a silent shrink of the config count or the duration, which
   FR-085's inherited stop-and-surface rule forbids by name. *(Outcome: measured 32.9 min, ~30× the
   JavaScript; surfaced and amended to 60 min by ruling on 2026-09-16 — SC-001.)* The `[long]` tag is applied to the fuzz,
   the duration sweeps and the soak regardless (the `[long]` convention: toolchain-independent,
   > ~15 s).
3. **No consumer needs an output faster than one control step.** Supported by FR-062's evidence
   (`feedback_ecology.h:2292-2296`) and by the slowest agent process being 56 s.
4. **Phase 10 owns every mapping.** Nothing here assumes a particular agent-to-target assignment;
   SC-002/SC-004 are stated on the raw agent outputs so they hold under any mapping.

---

## Review notes

Every issue raised in the review pass was **accepted**; none was rejected. Three were resolved by a
different route than the one suggested, and the reasoning is recorded here so the choice is auditable
rather than silent.

1. **FR-085 / SC-011 — CPU budget over the whole `stepIntervalChunks` range.** The review offered
   three routes: (a) gate a second arm at `stepIntervalChunks = 1`, (b) narrow FR-082's minimum, or
   (c) restate the budget per *step*. **(a) was taken, and (c) is explicitly forbidden in FR-085.**
   Restating a per-block budget as a per-step budget multiplies the permitted per-block cost by 8 at
   the cheapest step interval, so it does not resolve the finding — it relabels it, and the
   instruction not to resolve by relaxing a threshold applies. (b) narrows the public API on the
   strength of a cost figure nobody has measured yet; it is retained as the **named escalation path**
   in FR-085 (put the measured table to the user with a proposal to narrow the minimum) rather than
   applied pre-emptively. *Outcome (2026-09-16):* the table was measured, (b) at 1 missed by 7.6×
   and could not fit at any lever, and the escalation was taken by ruling — minimum 4, plus the two
   exact levers E-1/E-2; re-measured, (b) at 4 still missed by 1.06–1.13× after E-3 as well, and a
   second ruling set the minimum at 8, the default (FR-085, SC-011 record).

2. **SC-014 (b) — the wake ramp.** The review offered (a) restating the criterion in control steps or
   (b) adding an FR that evaluates wake gain on the 64-sample chunk grid, exempt from FR-062.
   **(a) was taken.** (b) would have the component re-target a consumer's gate on every chunk, which
   is the precise hazard `feedback_ecology.h:2290-2296` documents and which FR-062 exists to prevent
   ("a Phase-8 agent writing one loop's wake per block would otherwise stretch every OTHER loop's ramp
   without bound"). Buying ±1 ms of ramp resolution at the cost of the publication-rate contract is a
   bad trade. FR-070 now states the quantisation, SC-014 (b) states the tolerance in control steps,
   and the `stepIntervalChunks = 64` case (one step longer than the whole ramp) is asserted
   explicitly.

3. **SC-001 — workload.** The review's measurement (`node run.js fuzz 8` = 131.7 s, i.e. 16.5 s per
   900 s hostile config) is adopted verbatim as the cost basis. The **config count was not reduced**:
   1000 is the roadmap's own figure (line 393). The *duration* was corrected from 1800 s to the 900 s
   the cited 500/500 reference was actually measured at (`run.js:401`), a 1800 s sub-batch of 50 was
   added so the longer horizon is still visited, and an explicit ≤ 30 min runtime budget plus the
   per-config step count are now on the record so a later shrink is a visible spec change rather than
   a quiet one.

Two further clarifications worth recording, both accepted as raised:

- **SC-002's two numbers.** 0.10 activity / ≤ 25 % frozen were the prototype's *classification*
  thresholds for bucketing arbitrary fuzz configurations (`ecosystem-sim.js:570-583`), and reusing
  them as the defaults gate would have let an implementation at activity 0.11 with a quarter of the
  population permanently frozen pass while contradicting roadmap line 394. They are now scoped to the
  **verdict function** (used by SC-003, SC-010, SC-013) and the defaults gate stands separately at
  activity ≥ 0.30 with **zero** frozen.
- **`energyBudget`.** Resolved as **prepare-time** (FR-006 option (i)), not runtime-settable, which
  simultaneously removes the FR-061 division-by-zero reachable through FR-064, removes the SC-001 (c)
  contradiction a mid-run change would create, and makes its SC-012 fuzz exemption a stated
  consequence of the lifetime rather than an unrecorded choice.

---

## Clarifications

### Session 2026-09-15

- **Q1** (energy-scale knob units) → `preyFloor`, `capacity` and `satiation` are expressed and set in
  units of the mean per-agent share `energyBudget / agentCount`; `cellCapacity` in units of the
  per-cell share `energyBudget / resourceCells`; converted to absolute energy once, at `prepare()`.
  Appendix A defaults restated: `preyFloor` 0.016 → 0.5 shares, `capacity` 1.0 → 32 shares,
  `cellCapacity` 0.05 → 3.2 cell-shares at 64 cells, `satiation` stays 0 = off. [FR-008, FR-022,
  FR-042, FR-051, FR-055, SC-019]
- **Q2** (order of operations within a step) → Fixed verbatim: non-finite guard → single pair pass
  (record exchange flow, affinity, crowding, sync) → exchange pass two (scaled by
  `min(scale_i, scale_j)` above the refuge floor) → appetite → proportional regrowth pool→cells →
  grazing + foraging cells→agents → global feed + leak → energy integrate with zero/capacity clamps →
  movement + torus wrap → frequency drift → phase integrate → pool update → publish outputs.
  [FR-087]
- **Q3** (cycle clause and which series it scans) → The liveness verdict function gains a cycle
  clause computed on per-agent **output** series, using SC-005's recurrence scan (post-decay
  autocorrelation peak > 0.8 = cycle); entropy never gates (FR-066 unchanged). SC-013's 83.0 %
  reference is annotated as having been produced by the prototype's entropy-based cycle clause, not
  this corrected one. [SC-002, SC-013]
- **Q4** (which rail the clamp counters count) → `getOutputClampEngagementCount()` and
  `getAgentClampedStepFraction()` count the **upper** rail only (published output == 1.0 from
  saturation); a zero output from dormancy, wake gating or the FR-063 epsilon snap is a designed
  state and is not counted. A `getAgentZeroOutputStepFraction()` may be added later, append-only; not
  built now. [FR-061, FR-065]
- **Q5** (initial energy partition and `initialPoolFraction`'s lifetime) → The prototype's three-way
  partition is encoded verbatim: agent energies drawn `U(0,1)` and normalised to sum to
  `(1 − initialPoolFraction) · energyBudget`; cells drawn `U(0,1) · cellCapacity` then scaled so their
  total is `min(resSum, remaining · 0.5)`; the pool takes the remainder. `initialPoolFraction` moves
  into FR-006's prepare-time set with no runtime setter. [FR-006, FR-041]
- **Q6** (i.i.d. vs. stratified kind assignment) → Kinds are stratified at `prepare()`: deal
  `floor(agentCount / kNumKinds)` of each kind, distribute the remainder by a seeded draw, then apply
  a seeded shuffle — guaranteeing at least one of every kind for `agentCount >= 5`. A
  `getAgentCountOfKind(Kind)` accessor is added. SC-002's defaults figures are flagged for
  re-measurement against the resulting histogram. [FR-011, FR-060, SC-002]
- **Q7** (SC-004 (b)'s within-run decorrelation floor) → Computed over the **full 900 s** run on the
  ~1 Hz grid, on agent **energies**, as the mean `|corr|` between different agents of the first run;
  the seed-blind gate is 1.5× that floor; the reference pair is the round-2 0.13 vs. 0.12 at the
  shipped defaults. The round-1 0.61 / 0.50 figures are struck from the criterion. [SC-004]
- **Q8** (prepare-time getters and counter clearing) → `getEnergyBudget()`, `getResourceCells()`,
  `getStepIntervalChunks()` and `getStepDurationSeconds()` are added, reporting the FR-005-clamped
  values; `prepare()`, `reset()` and `setSeed()` all clear every diagnostic counter (conservation
  violations, non-finite containments, output clamp engagements, per-agent clamped-step fractions), so
  a fuzz harness may reuse one instance. [FR-060, FR-065]
- **OQ-1** (resource geometry) → Ship the proven 1-D strip resource field (cells along x, grazing and
  foraging weighted by x separation) inside the 2-D torus, and record the limitation; a 2-D cell grid
  is untuned and out of scope. [FR-040]
- **OQ-2** (step interval default) → 8 control chunks (512 samples, the `dt` every prototype figure
  was measured at), range [1, 64] chunks. [FR-082]

### Session 2026-09-16 (build stage, T016 — the SC-011 ruling)

- **SC-011 / FR-085** (first measured table: (a) 55 275 ns/block, 1.04× over; (b) at
  `stepIntervalChunks = 1` 404 614 ns/block, 7.59× over, unfittable at any lever) → Take FR-085's
  named escalation: **FR-082's minimum becomes 4** (range [4, 64]; SC-011 (b) re-gated at 4), plus
  two **exact** cost levers — E-1, FR-040's cell weight by the Gaussian recurrence along the uniform
  cell grid; E-2, FR-035's and FR-050's sines from one per-agent sin/cos table — gated for exactness
  by the new **SC-011 (c)**. The L4/L5 lookup tables (approximations) were offered and declined; the
  ceiling is unchanged. [FR-082, FR-085, SC-011, Appendix A, OQ-2]
- **SC-011 / FR-085, second table** ((a) at 8: 27 224 ns/block; (b) at 4: 56 587–60 176 ns/block,
  1.06–1.13× over after E-1, E-2 and the bit-identical E-3 hoists; reciprocals and an agent-outer
  grazing walk measured at zero and at +25 % and reverted) → Take the escalation once more:
  **FR-082's minimum becomes 8**, the tuned default (range [8, 64]); SC-011 gates the floor (a) and
  the cheap end (b) at 64; SC-010's band moves to 8–32 chunks; SC-014 (b)'s arms are 8, 16 and 64.
  The pair-kernel LUT was declined again. [FR-082, FR-085, SC-010, SC-011, SC-014, Appendix A, OQ-2]
- **Cycle clause series** (T023's full-suite run: per-agent output recurrence flagged FR-050's own
  intrinsic oscillation — 3 of 32 agents at the defaults, worst 0.848 at 109 s; SC-003 red in 5 of 15
  cells on that clause alone; SC-013 at 52.8 % with 127 of 500 cycle deaths) → SC-005 and SC-002's
  verdict clause (iii) scan the **population-mean** `getAgentOutput` series; per-agent recurrence is
  reported, not gated. Still output, never entropy. [SC-002, SC-003, SC-005, SC-013]
- **SC-001 runtime budget** (measured 32.9 min alone, 43 min after a full suite, all clauses green)
  → **60 minutes**; the config count, durations and perturb schedule are unchanged. [SC-001]
- **OQ-3** (default population) → 32 agents, `kMaxAgents = 48`. [FR-004]
- **OQ-4** (ModulationSource adapter) → No adapter now; the indexed read surface (FR-060) is the
  contract; an adapter is append-only later if a consumer needs it. [FR-060]
- **OQ-5** (dormancy reading) → Adopt FR-072: dormancy gates the published output while the
  simulation keeps running (the only reading that preserves conservation). Recording this as a second
  stated exception in the roadmap's own Dormancy paragraph is a roadmap-file edit outside this spec's
  scope and is not made here. [FR-072]

---

