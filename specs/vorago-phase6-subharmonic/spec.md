# Feature Specification: Vorago Phase 6 — Subharmonic Engine

**Spec slug:** `vorago-phase6-subharmonic`
**Roadmap source:** `specs/Vorago-roadmap.md` → Part A → Phase 6 (lines 302–322); reuse-inventory row
`L10 Subharmonic Engine` (line 118); ODR note (lines 127–129); cross-cutting constraints
(lines 508–532), of which the Dormancy rule is lines 517–525 and the shared-component rule — which
names `SubOscillator extension` by name — is lines 530–532; roadmap Open Question 4 (lines 541–542),
the one decision the roadmap explicitly defers to *this* spec. Every roadmap line number in this
document was read and re-verified against `specs/Vorago-roadmap.md` this session.
**Layer:** one new Layer 3 component, `dsp/include/krate/dsp/systems/subharmonic_engine.h`
(roadmap line 307).
**Test target:** `dsp_systems_tests` — an **enumerated, not globbed** source list opening at
`add_executable(dsp_systems_tests` (`dsp/tests/CMakeLists.txt:324`) and closing after the four Vorago
Phase-5 TUs at `:446-449`; an unregistered TU silently drops out of the build and its cases never run
(the comment the list itself carries at `:422-423`). The `-fno-fast-math` block opens at
`dsp/tests/CMakeLists.txt:542` (`if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")`).
**Depends on:** nothing from Vorago Phases 2–5. This component is reachable directly from Phase 1's
completion; the roadmap's dependency graph (lines 490–500) puts Phase 6 on its own branch off
Phase 1, and even that edge is nominal — nothing here consumes `SlowEventScheduler`,
`PerlinNoiseSource` or the Aizawa attractor.
**Plugin work:** none. The Vorago plugin starts at Phase 11; phases 1–10 are KrateDSP-only.

## Overview

The Subharmonic Engine is Vorago's **weight** layer (roadmap line 305, "Impossible low frequencies —
cinematic weight"). It is a Layer 3 system that synthesises three sub-fundamental tones from the
**known** note pitch — `f/2`, `f/4` and `2f/3` (the fifth below) (roadmap lines 309–311) — gates their
combined level by an envelope follower riding the voice body so the subs "swell with the drone rather
than droning independently" (roadmap lines 312–313), and runs the sum through a gentle low-pass, a
low-drive saturator and a DC blocker (roadmap lines 314–315). Its output is **added** to the dry
signal, not crossfaded against it: subs augment mass, they do not replace the body.

There is **no pitch detection anywhere in this component**. The roadmap states the reason at line 309
("no pitch detection needed — this is a synth") and the reuse-inventory row repeats it at line 118
(`pitch_tracker`/`pitch_detector` — "not needed — pitch is known from the note"). Pitch enters through
one scalar setter and nothing else. This is what makes the phase small.

**The single most consequential finding of this session is that the roadmap's anticipated
`SubOscillator extension` (roadmap line 531) is not needed, and the design below is strictly
reuse-only.** The reasoning is arithmetic, and it is verified against the shipped header rather than
assumed:

- `SubOscillator` (`processors/sub_oscillator.h:111`) is a *flip-flop divider*, not an oscillator. It
  owns no phase source; its per-sample entry point is
  `[[nodiscard]] float process(bool masterPhaseWrapped, float masterPhaseIncrement) noexcept`
  (`:222`), and it derives everything — the flip-flop toggle at `:252`, the Sine/Triangle sub
  increment `masterInc / octaveFactor` at `:302` and `:316` — from those two arguments. Its
  `SubOctave` enum offers exactly `OneOctave` (÷2) and `TwoOctaves` (÷4) (`:48-51`).
- ÷2 and ÷4 are therefore direct: one synthetic master phase accumulator running at `f` drives two
  `SubOscillator` instances, one at each `SubOctave`.
- **The fifth below is the same component fed a different master.** `2f/3` is `(4f/3) / 2` — so a
  second synthetic master accumulator running at `4f/3`, driving a third `SubOscillator` at
  `SubOctave::OneOctave`, produces `2f/3` with no new oscillator code, no new waveform switch, and no
  amendment to a shipped, Seraphis-consumed component. `4f/3` is itself derived from `f`, so the tone
  is phase-locked to the note in exactly the sense the roadmap asks for (line 309, "phase-locked
  sub-oscillators"): the two accumulators realign every three cycles of `f`, and both are zeroed
  together by `reset()`.

Two further verified facts shape the design and are surfaced here rather than buried:

1. **A `MinBlepTable` is mandatory even when no tone uses the Square waveform.**
   `SubOscillator::prepare` sets `prepared_ = false` and returns early when
   `table_ == nullptr || !table_->isPrepared() || table_->length() > 64` (`:144-147`), and
   `process()` returns `0.0f` while `!prepared_` (`:224-226`). A Sine-only configuration built on a
   null table is therefore **silent**, not merely un-BLEPped. The engine owns one `MinBlepTable`
   (`primitives/minblep_table.h:50`), prepares it *before* the sub-oscillators, and shares it by
   pointer — the ownership model the header documents at `:78-81` ("Multiple SubOscillator instances
   can share one MinBlepTable").
2. **The Sine path re-zeroes its phase once per sub period.** `subPhase_.phase = 0.0` runs on every
   output flip-flop rising edge (`:274-278`), *after* which the Sine case advances by
   `masterInc / octaveFactor` (`:299-312`). Because the master's own wrap carries a fractional
   remainder, the accumulated sub phase at the reset instant is not exactly 1.0, so each sub period
   begins with a phase discontinuity bounded by `masterInc / octaveFactor` cycles. This is the
   dominant distortion term in the Sine tones and it is *pitch-dependent* — larger at higher
   fundamentals. SC-004 measures it rather than assuming it away, and D-4 records the rejected
   alternative (a private sine accumulator, which would abandon the reuse mandate).

**Boundedness here is trivial by construction and is not the phase's risk.** Nothing in this
component feeds back; every stage is feed-forward, every tone level is a bounded gain, and the
saturator is a contraction. The real risks are the three the roadmap's own success criteria name —
a free-running boom (a sub that sounds when the body does not), distortion products from the
dividers, and peak/mono damage at the bottom of the spectrum — plus one the roadmap does not name and
this spec adds: **infrasonic pumping**, because `f/4` of a low note lands below 16 Hz, where a single
10 Hz `DCBlocker` removes almost nothing. Per the Clarifications session below (Q2), the mitigation is
a steeper in-chain filter plus a far-below backstop gate, not a 16 Hz hard mute (D-5, FR-016).

## Clarifications

### Session 2026-09-13

- **Q1 (per-tone gain form)** — Is the per-tone gain one composite `LinearRamp`, or the shipped
  house three-factor form? → **The house three-factor form** (`noise_organism.h:937` `getSourceGain`,
  `:1842-1844` `updateBreathGain`): `levelRamp` (a `LinearRamp` retargeted only on a level write),
  `breathGain` (held per control step, not a ramp), and `gate` (a `LinearRamp` retargeted only at a
  floor/dormancy edge) multiply together; `getToneCurrentGain` reports the product. The 50 ms gate
  fade now has a defined arrival, so SC-020(c) is measurable as written. [FR-022, FR-023, SC-020]
- **Q2 (infrasonic handling)** — Hard mute, retune, or filter, for a tone whose frequency falls
  below the floor? → **Filter, not gate.** A fixed ~18 Hz 2nd-order Bessel high-pass (`DCBlocker2`,
  `dc_blocker.h:272`) replaces the chain's stage-3 blocker, plus a ~12 Hz far-below backstop gate
  (the `gate` of Q1) for genuinely near-DC tones. There is no behavioural cliff at 16 Hz any more:
  `Div4` stays alive down to `f ≈ 48 Hz`. The FR-013 default reverts to 55 Hz (A1) — the 82.5 Hz move
  is no longer needed to keep three tones reachable under a hard gate. [FR-013, FR-016, FR-042,
  FR-052, FR-055, SC-003, SC-004, SC-008, SC-013, SC-017, SC-020]
- **Q3 (default sub/body ratio)** — What is the intended default sub-to-body level? →
  **Lower the FR-020 tone defaults by ~12 dB** (−18 / −24 / −30 dB) so the default add sits ~9 dB
  under a −12 dBFS body, **and** add a criterion that measures and pins the default sub-to-body RMS
  ratio from a real render. Phase 10's Weight macro drives the level back up. [FR-020, SC-021]
- **Q4 (tracking reference)** — Is `kTrackReferenceRms` a fixed constant or a settable calibration?
  → **Settable.** Add `setTrackReferenceDb(float)` clamped `[−48, 0]`, default −18, plus a getter.
  This is what makes the placement-agnosticism claim in OQ-1 true; Phase 10 sets it once per
  placement. [FR-035, SC-002]
- **Q5 (dormancy granularity)** — At what granularity is FR-025's dormancy condition evaluated, and
  what keeps advancing while the chain is skipped? → **Control step, house form**
  (`feedback_ecology.h:1878-1905`): the dormancy skip flag and the sleep edge are both evaluated
  every 64-sample control step; a wake takes effect within ≤ 64 samples; FR-025/FR-026 are restated
  in control-step terms; the FR-032 tracking gain and the FR-051 wet gain — both per-sample
  `LinearRamp`s — keep advancing while the chain is skipped, so a wake never begins from a stale
  gain. [FR-025, FR-026, SC-020]
- **Q6 (clamp scope)** — Does FR-054's output clamp apply to the summed output (dry included) or to
  the sub only? → **The sub contribution only**, clamped before the add into the main output. The
  dry path is bit-transparent at every input level; `getClampEngagementCount()` is a pure statement
  about this component's own ladder; SC-006(a) and SC-017(d) keep their `== 0` assertions. [FR-050,
  FR-054]
- **Q7 (breathing depth convention)** — FR-021's `setDepth` passthrough, or the house affine span?
  → **The house affine span.** `BreathingModulator` depth is left at its library default of 1.0; the
  engine applies `1 + kBreathGainSpan * depth * b` with `kBreathGainSpan = 0.45`
  (`noise_organism.h:174`). Max swing is ±45%; `kMaxPreSaturationMagnitude` recomputes to ≈ 8.7; the
  depth parameter is now comparable to `NoiseOrganism`'s for the Phase 10 macros. [FR-021, FR-022,
  FR-052]
- **Q8 (sub-only routing)** — Does the sub need a separately routable output for Phases 9/10? →
  **Promote the tap to a supported output** and add a sub-to-main enable flag
  (`setSubToMainEnabled`, default enabled) so Phase 10 can route the sub around the Phase-9 space
  engine. The routing decision itself still belongs to Phase 10. [FR-050, FR-062, FR-064, SC-022]
- **OQ-1 (placement ruling)** — Unchanged: placement (global vs per-voice) is decided by the user
  from the FR-071 measured table at the end of the build stage; the build stage must present that
  table and stop for the ruling. It binds Phase 10's wiring, not this component's code. [FR-076,
  SC-018]

## Scope

In scope:

- One new Layer 3 component, `SubharmonicEngine`, at
  `dsp/include/krate/dsp/systems/subharmonic_engine.h` (roadmap line 307), header-only, stereo in /
  stereo out, sub content **added** to an otherwise untouched dry path.
- **Three sub tones** — `f/2`, `f/4`, `2f/3` — each realised by a shipped `SubOscillator` driven by a
  synthetic master phase accumulator, each with its own level and its own slow level-breathing
  (roadmap lines 309–311).
- **Envelope tracking** of the incoming voice body via `EnvelopeFollower` in `DetectionMode::RMS`,
  gating the whole sub sum (roadmap lines 312–313).
- **The shared chain**: sub sum → `TwoPoleLP` → `SaturationProcessor` at low drive → `DCBlocker2`
  (a ~18 Hz 2nd-order Bessel high-pass, Q2/FR-042) (roadmap lines 314–315).
- Output safety: a ~12 Hz far-below backstop gate per tone, a hard clamp on the sub contribution
  only with an engagement counter (Q6/FR-054), and a non-finite trap.
- A **supported sub tap** (FR-062, promoted per Q8) and a **sub-to-main routing flag**
  (`setSubToMainEnabled`, FR-064, default enabled) so a later phase can route the sub around the
  Phase-9 space engine without abusing a measurement-only entry point.
- The **placement probe** (FR-071) whose measured table decides roadmap Open Question 4 — global
  post-voice-sum versus per-voice (roadmap lines 316–317, 541–542). The component's contract is
  written to be **placement-agnostic** so the ruling is a Phase-10 wiring decision, not a redesign.
- Unit tests covering the roadmap's five Phase-6 success criteria (lines 319–321) plus the
  cross-cutting gates from lines 508–532 — each with a numbered criterion, none left in prose:
  seed determinism (SC-011), **sample-rate change and re-prepare (SC-019)**, block-partition
  invariance (SC-012), zero allocation after prepare (SC-010), layer/ODR lints and portability
  (SC-015), no bit-exact goldens (SC-011, SC-012), and the **worst-case** boundedness soak
  (SC-017 (B)).
- **The ruling on roadmap Open Question 4** as a phase deliverable, not a note: FR-076 and SC-018
  require the FR-071 placement arms to be transcribed and the global-versus-per-voice decision
  recorded before the phase is complete.

## Non-Goals (owned by later phases, or deliberately excluded)

- **Pitch detection or pitch tracking of any kind.** Roadmap line 309 and reuse row line 118 both
  exclude it. `pitch_tracker.h`, `pitch_detector.h`, `yin_pitch_detector.h` and
  `multi_pitch_detector.h` are not included and not consulted at runtime. The "per-voice pitch
  tracking of the lowest sounding voice" of roadmap line 316 means *the engine caller selects which
  voice's known note frequency to write* — it is a Phase-10 voice-allocator query, not a DSP
  analysis, and it is outside this component either way.
- **Deciding where the engine sits in the Vorago signal chain.** OQ-1 collects the measurement; the
  binding ruling belongs to the user and its consequences land in Phase 10 (`VoragoVoice` /
  `VoragoEngine`). This phase ships a component that is correct in both placements and a probe that
  prices them.
- **Amending `SubOscillator`.** Roadmap line 531 anticipated an extension; the Overview shows why
  none is required. FR-080 requires the header byte-unchanged and its shipped test TU
  (`dsp/tests/unit/processors/sub_oscillator_test.cpp`) green.
- **Amending `SaturationProcessor`, `EnvelopeFollower`, `TwoPoleLP`, `DCBlocker`/`DCBlocker2`,
  `BreathingModulator`, `MinBlepTable`.** All are shipped consumers of their own tests
  (FR-080, SC-016). `DCBlocker2` is the class this component actually calls (FR-042, Q2); the
  single-pole `DCBlocker` in the same header is read and not called.
- **`TapeSaturator`.** Named in the reuse-inventory row (line 118) and read this session and
  **rejected**: it is a Jiles–Atherton hysteresis model with a per-sample Newton/RK solver
  (`processors/tape_saturator.h:80`, JA parameters at `:95-99`, `langevin`/`langevinDerivative` at
  `:438`/`:450`). Roadmap line 314 asks for saturation "at low drive"; paying for a magnetics solver
  on a 30 Hz sine to get the first term of a `tanh` is not a trade this phase makes. Recorded in D-3.
- **`OnePoleLP` as the chain low-pass.** Also named at roadmap line 314, also rejected: 6 dB/octave
  from a 120 Hz corner leaves −6 dB at 240 Hz and −12 dB at 480 Hz, i.e. the "gentle low-pass" would
  still pass most of the fundamental it is meant to sit beneath. `TwoPoleLP` costs one extra biquad
  state and is Butterworth by construction. D-2.
- **Being a `ModulationSource`.** Following `NoiseOrganism` and `ResonanceDriftNetwork`, every
  externally driven target here is a plain scalar setter. No `ModSource` enumerator is added, and the
  `BreathingModulator` instances the engine owns are internal state, not a routing surface.
- **Stereo sub content or per-tone panning.** The sub path is **mono by construction** (FR-050) and
  that is the whole mechanism behind the roadmap's mono-compatibility criterion (line 321). A
  stereo-spread sub is a defect in this genre, not a feature.
- **Oversampling.** The saturator runs at base rate on a signal whose content is confined below the
  FR-040 low-pass corner; the aliasing products of a `tanh` on a ≤ 120 Hz band do not reach Nyquist at
  any accepted sample rate. FR-040 orders low-pass **before** saturation for exactly this reason.
- **A sub-only output bus as a separate physical output.** The tap (FR-062) is a **supported**
  output — not measurement-only, per Q8/FR-062 — but it is still a write into a caller-supplied
  buffer on the existing `processBlockTapped` entry point, not a new bus, a new port, or a new
  routing surface of its own. FR-063 requires the tapped path to produce a **bit-identical** main
  output to the untapped one. **Deciding how Phase 9/10 wires the tap or the FR-064 enable flag** —
  pre-space-engine sub injection, a mix bus, anything else — stays Phase 10's job; this phase ships
  the mechanism (tap + flag), not the wiring.

## Existing components (verified this session)

Every row was opened and read in this session; signatures are quoted from the file.

| Component | Header (verified) | What Phase 6 reuses / relies on |
|---|---|---|
| `SubOscillator` (L2) | `processors/sub_oscillator.h:111` | **All three tone generators** (FR-010). `explicit SubOscillator(const MinBlepTable* table = nullptr) noexcept :120`, `void prepare(double sampleRate) :142`, `void reset() :172`, `void setOctave(SubOctave) :186`, `void setWaveform(SubWaveform) :192`, `void setMix(float) :199`, `[[nodiscard]] float process(bool masterPhaseWrapped, float masterPhaseIncrement) :222`, `[[nodiscard]] float processMixed(float mainOutput, bool, float) :348`. Namespace-scope enums `SubOctave { OneOctave = 0, TwoOctaves = 1 }` (`:48`) and `SubWaveform { Square = 0, Sine = 1, Triangle = 2 }` (`:60`) — **reused, not re-declared** (FR-002). **Five load-bearing facts.** (a) It owns no phase source: `process()` takes the master's wrap flag and increment and derives the flip-flop toggle (`:252`) and, for Sine/Triangle, the sub increment `masterInc / octaveFactor` (`:302`, `:316`). That is what lets FR-011's synthetic master drive it and FR-012's `4f/3` master turn a ÷2 divider into a fifth-below. (b) `prepare()` **hard-fails to `prepared_ = false`** when the table pointer is null, unprepared, or `length() > 64` (`:144-147`), and `process()` then returns `0.0f` forever (`:224-226`) — so FR-004's prepare ordering is a correctness requirement, not tidiness. (c) The Sine/Triangle sub phase is **re-zeroed on every output flip-flop rising edge** (`:274-278`), before the increment is applied — the periodic discontinuity SC-004 measures. (d) `process()` sanitises its return to `[-2, 2]` and maps NaN to `0.0f` through `detail::opaqueFloatBits` (`sanitize()` at `:362-372`) — a fast-math-immune bit test, so the tone generators are self-healing on output. It does **not** guard its *arguments*: a non-finite `masterPhaseIncrement` poisons `masterPhaseEstimate_` (`:229`), which is why FR-009 rejects non-finite setter arguments at the boundary. (e) `setMix`/`processMixed` implement an equal-power blend against a *main* oscillator that does not exist here; FR-010 uses `process()` only and leaves `mix_` at its constructed default. Not modified. |
| `MinBlepTable` (L1) | `primitives/minblep_table.h:50` | **Owned by the engine, one instance, shared by all three sub-oscillators** (FR-004). `inline void prepare(size_t oversamplingFactor = 64, size_t zeroCrossings = 8) :76` (**not** real-time safe — allocates), `[[nodiscard]] inline size_t length() const :373`, `[[nodiscard]] inline bool isPrepared() const :378`, nested `struct Residual` `:400` with `explicit Residual(const MinBlepTable&) :403`. **Non-copyable, movable** (`:55-59`) — so the engine holds it by value and hands out `&table_`. Footprint: `table_.resize(length_ * oversamplingFactor_)` (`:216-217`) and `blampTable_.resize(tableSize)` (`:243`), i.e. two vectors of `16 * 64 = 1024` floats = 8 192 bytes at the default `prepare(64, 8)`; each `Residual` allocates `buffer_(table.length())` = 16 floats (`:405`). `length() = zeroCrossings * 2 = 16 <= 64`, so the `SubOscillator` guard at `:146` passes. Not modified. |
| `PhaseAccumulator` (L0) | `core/phase_utils.h:154` | **The two synthetic masters** (FR-011). A public-member value type: `double phase = 0.0` (`:155`), `double increment = 0.0` (`:156`), `[[nodiscard]] bool advance() noexcept :161`, `void reset() noexcept :170` (resets phase, **preserves** increment), `void setFrequency(float frequency, float sampleRate) noexcept :177`. `advance()` returns exactly the `masterPhaseWrapped` boolean `SubOscillator::process` wants, and `increment` is exactly its `masterPhaseIncrement` — the two components fit together with no adapter. Not modified. |
| `EnvelopeFollower` (L2) | `processors/envelope_follower.h:82` | **The body tracker** (FR-030), named by roadmap line 312. `void prepare(double sampleRate, size_t maxBlockSize) :106`, `void reset() :128`, `[[nodiscard]] float processSample(float input) :164`, `[[nodiscard]] float getCurrentValue() const :192`, `void setMode(DetectionMode) :202`, `void setAttackTime(float ms) :220`, `void setReleaseTime(float ms) :227`, `void setSidechainEnabled(bool) :234`. `DetectionMode { Amplitude = 0, RMS = 1, Peak = 2 }` at `:41-45`. **Three facts.** (a) `prepare()` **ignores `maxBlockSize`** (`:107`) and allocates nothing — it adds zero to FR-073's footprint. (b) `processSample` documents "**Does NOT validate input** — caller must ensure no NaN/Inf for maximum speed" (`:163`); one non-finite sample poisons `squaredEnvelope_` permanently, which is why FR-055's non-finite trap resets it. (c) Ranges: `kMinAttackMs = 0.1f` / `kMaxAttackMs = 500.0f` (`:88-89`), `kMinReleaseMs = 1.0f` / `kMaxReleaseMs = 5000.0f` (`:90-91`). The sidechain high-pass stays **disabled** (FR-030): its range is `[kMinSidechainHz = 20, kMaxSidechainHz = 500]` (`:94-95`) and enabling it would blind the follower to precisely the low body content the subs are supposed to follow. Not modified. |
| `TwoPoleLP` (L1) | `primitives/two_pole_lp.h:49` | **The chain low-pass** (FR-040), the `TwoPoleLP` half of roadmap line 314. `void prepare(double sampleRate) :58`, `void setCutoff(float hz) :67`, `[[nodiscard]] float getCutoff() const :76`, `[[nodiscard]] float process(float input) :84`, `void processBlock(float*, size_t) :94`, `void reset() :103`. It is a `Biquad` configured Butterworth low-pass (`updateCoefficients()` at `:109-118`, `kButterworthQ`), so it inherits `Biquad::process`'s **reset-and-return-0 on a non-finite input** and its denormal flush — the second self-healing stage in the chain. `setCutoff` calls `configure()`, i.e. trigonometry, so FR-040 confines cutoff writes to the control surface and never to the per-sample path. Not modified. |
| `SaturationProcessor` (L2) | `processors/saturation_processor.h:98` | **The chain saturator** (FR-041), named by roadmap line 314. `void prepare(double sampleRate, size_t maxBlockSize) :122`, `void reset() :149`, `void process(float* buffer, size_t numSamples) :175`, `[[nodiscard]] float processSample(float input) :228`, `void setType(SaturationType) :264`, `void setInputGain(float gainDb) :273`, `void setOutputGain(float gainDb) :283`, `void setMix(float) :294`, plus getters `:304-319`. `SaturationType { Tape, Tube, Transistor, Digital, Diode }` at `:54-60`; `kMinGainDb = -24.0f` / `kMaxGainDb = +24.0f` (`:104-105`). **Three facts.** (a) `processSample` explicitly "**Does NOT apply DC blocking** (no state for single sample)" (`:225-227`) — which is exactly why roadmap line 315 puts a separate `DCBlocker` after it, and why FR-041 uses the per-sample entry point and FR-042 supplies the blocker. (b) `processSample` early-exits to dry when the smoothed `mix < 0.0001f` (`:238-240`); FR-041 leaves `mix_` at its constructed `1.0f` default (`:414`). (c) `prepare()` allocates `dryBuffer_.resize(maxBlockSize)` (`:136`) for the **block** path only; the engine never calls `process(float*, size_t)`, so that vector is a dead heap term which FR-073 declares rather than hides. `saturateTape` is `Sigmoid::tanh` (`:343-347`) — symmetric, odd-harmonic, DC-free, the reason D-1 fixes the type to `Tape`. Not modified. |
| `DCBlocker2` (L1) | `primitives/dc_blocker.h:272` | **The chain's final stage** (FR-042, revised per Q2/Clarifications), a 2nd-order **Bessel** high-pass — the header says so itself at `:262-264` and again at `:356`, "Bessel Q for 2nd order (maximally flat group delay)", `:363` `Q = 1/sqrt(3) ≈ 0.577` (Butterworth would be `1/sqrt(2)`, which is what `TwoPoleLP` uses via `kButterworthQ`, `two_pole_lp.h:109`). `void prepare(double sampleRate, float cutoffHz = 10.0f) :298`, `void reset() :307`, `void setCutoff(float) :313`, `[[nodiscard]] float process(float x) :328`, `void processBlock(float*, size_t) :346`. Biquad law `y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2` (`:334`). **Two facts.** (a) `process()` has no finiteness branch — a NaN sticks in `y1_`/`y2_` forever and `detail::flushDenormal` does not clear it. This is why FR-055's trap exists and why the blocker sits **after** two self-healing stages rather than first (unchanged from the earlier single-pole choice). (b) At Q ≤ `1/sqrt(2)` the biquad's magnitude response has **no resonant peak**: `|H(jw)|` rises monotonically from 0 at DC to 1 at high frequency, so its peak gain is exactly `1.0` — simpler than the single-pole `DCBlocker`'s `2/(1+R)` figure, and the number FR-052's `kMaxPreClampMagnitude` now uses. Configured at `~18 Hz` (`kInfrasonicFilterHz`), it attenuates a 13.75 Hz tone by ≈ −7.5 dB (analog-prototype estimate: `Omega = 13.75/18 = 0.764`, `|H|^2 = Omega^4 / ((1-Omega^2)^2 + (Omega/Q)^2) ≈ 0.177`), materially more than the single-pole `DCBlocker`'s ≈ −2 dB at the same frequency from a 10 Hz corner — the reason Q2 replaces it. The single-pole `DCBlocker` (`:94`) is **read and no longer used** by this component. Not modified. |
| `BreathingModulator` (L2) | `processors/breathing_modulator.h:105` | **Per-tone slow level breathing** (FR-021), roadmap line 311. `void prepare(double sampleRate) :144`, `void reset() :152`, `void setSeed(std::uint32_t) :164`, `void setRate(float hz) :170`, `void setDepth(float normalized) :177`, `void setIrregularity(float normalized) :184`, getters `:188-190`, `void process() :197`, `void processBlock(size_t numSamples) :209`, `[[nodiscard]] float getCurrentValue() const override :222`, `[[nodiscard]] std::pair<float,float> getSourceRange() const override :227` → **`{-1, +1}`, bipolar and fixed independent of depth** (the doc comment at `:226` says so explicitly). **That is the declared *range*, not the output.** The output is already depth-scaled: `shapeOutput()` returns `std::clamp(depth_ * bipolar, -1.0f, 1.0f)` (`:286`) with `float depth_ = kDefaultDepth;` (`:294`) and `kDefaultDepth = 1.0f`, and `getCurrentValue()` (`:222-224`) returns the smoothed form of that. FR-022 therefore applies **no second depth factor** — doing so would square the depth and make FR-021's defaults 3–8× shallower than written. Constants: `kMinRate = 0.01f` / `kMaxRate = 0.5f` / `kDefaultRate = 0.1f` (`:108-111`) — 2 s to 100 s per breath, the "slow" of roadmap line 311 — `kOutputSmoothMs = 20.0f` (`:117`), asymmetric inhale/exhale shaping (`:120-124`), `kDefaultBreathSeed = 0xB2EAu` (`:131`). `processBlock(n)` is the O(1) control-rate advance FR-024 uses; `processBlock(0)` is a documented no-op (`:211-213`). It derives from `ModulationSource`, so `getCurrentValue()` is a **virtual** call — FR-024 reads it once per 64-sample control chunk, never per sample. Not modified. |
| `Xorshift32` / `deriveStreamSeed` (L0) | `core/random.h:41` / `:102` | Per-lane seed derivation. `[[nodiscard]] constexpr float nextFloat() :59` (bipolar), `nextUnipolar() :67`; `[[nodiscard]] constexpr std::uint32_t deriveStreamSeed(std::uint32_t base, std::size_t salt) noexcept :102`. Load-bearing because `Xorshift32::seed()` substitutes its default for 0, so two lanes hashing to 0 would collapse onto one stream — FR-072's salt table uses `deriveStreamSeed` for exactly this reason. |
| `LinearRamp` / `OnePoleSmoother` (L1) | `primitives/smoother.h:305` / `:134` | Control smoothing. `LinearRamp`: `void configure(float rampTimeMs, float sampleRate) :329`, `setTarget :342`, `[[nodiscard]] float process() :370`, `snapToTarget :414`, `snapTo(float) :421`. `LinearRamp` is the Phase-2/3 choice for gain gates and is what FR-022's 50 ms tone gates and FR-032's tracking gain use. |
| `detail::isNaN` / `isInf` / `isFinite` / `flushDenormal`, `dbToGain` / `gainToDb` (L0) | `core/db_utils.h:99` / `:260` / `:118` / `:245`, `:293` / `:317` | The `-ffast-math`-proof finiteness tests (bit pattern behind an opaque barrier) and the dB conversions. FR-008 forbids `std::isnan`/`std::isinf`/`std::isfinite`; `tools/lint-nonfinite-symbols.js` enforces it. |
| `kPi`, `kTwoPi` (L0) | `core/math_constants.h:28`, `:32` | Frequency/phase arithmetic. |
| Convention source: `ResonanceDriftNetwork`, `NoiseOrganism`, `FeedbackEcology` (L3) | `systems/resonance_drift_network.h:128,135-136,143-144,265,281,297-306,771,904,912`; `systems/noise_organism.h:150,178,180,190,844,999`; `systems/feedback_ecology.h:914,944` | **Read, and by design not included** — there is nothing here to consume. The Vorago house style this component matches item by item: `kControlChunkSamples = 64` with the `static_assert` pinning it to the shared library-wide grid (`resonance_drift_network.h:135-136`); `kGainRampMs = 50.0f` (`noise_organism.h:178`); `kOutputClamp = 4.0f` (`noise_organism.h:180`); `kMinUsableSampleRate = 8000.0` (`resonance_drift_network.h:281`) and the `std::clamp` inverted-bounds UB argument behind it (`:255-281`); the nested designated-initialiser-only `PrepareConfig` (`resonance_drift_network.h:297-306`, `noise_organism.h:190-195`); the normative argument contract — out-of-range index is a silent no-op, non-finite float is a **rejection** with the previous value standing, out-of-range float is clamped and the getter reports the clamp; `getClampEngagementCount()` (`resonance_drift_network.h:904`); `getAllocatedBytes()` (`:912`, `noise_organism.h:999`); the append-only salt table with overlap `static_assert`s (`:929-947`); and the two-entry-point tapped-render shape (`feedback_ecology.h:914`, `:944`). Not modified. |
| Test helpers | `tests/test_helpers/` | `render_fingerprint.h:58 kSampleTolerance = 5.0e-4f`, `:61 kMetricTolerance = 2.5e-4`, `:63 struct RenderFingerprint`, `:122 compareFingerprints` for SC-011/SC-012; `allocation_detector.h:48 AllocationDetector` / `:111 AllocationScope` for SC-010; `signal_metrics.h:111 calculateTHD(const float*, size_t, float fundamentalHz, float sampleRate, int maxHarmonic = 10)` — **read, and rejected for SC-004**: it caps its transform at 8192 points (`while (fftSize < n && fftSize < 8192) fftSize *= 2;`), i.e. 5.86 Hz bins at 48 kHz, and picks both the fundamental and each harmonic as the max over a **±2-bin** window. For a 20–110 Hz sub those windows overlap (Div4 at `f = 55` (the FR-013 default): sub 13.75 Hz = bin 2.35, H2 27.5 Hz = bin 4.69 — the windows (each ±2 bins) meet between bins 4.35 and 4.69, inside the fundamental's 4-bin Hann main lobe), so the figure it returns is window leakage, not divider distortion; and it returns `0.0f` outright when the fundamental magnitude is `< 1e-10f`, i.e. a silent tone reports 0 % and passes. SC-004 uses the new `measureLowFrequencyThdPercent` helper instead (A-4). `:326 calculateSpectralFlatness`, `:222 calculateCrestFactorDb`; `artifact_detection.h:38 ClickDetectorConfig` / `:72 ClickDetection` for SC-008; `spectral_analysis.h:40 frequencyToBin`, `:149 getHarmonicBins`, `:194 toDb`, `:207 sumBinPower` for SC-005; `statistical_utils.h:41 computeMean` / `:76 computeStdDev`. **Four metrics are missing from the tree and are added by this phase** (the Phase-4/5 precedent for a missing metric), all four declared in A-4: (i) a true-peak measurement — nothing in `tests/test_helpers/` measures dBTP, so SC-006 adds `measureTruePeakDb(const float* l, const float* r, size_t n)` built on the shipped `Oversampler` (`primitives/oversampler.h:226`, the same 4× basis `TruePeakLimiter::processChunk` uses at `true_peak_limiter.h:125-146`); (ii) a pointer/length correlation — the only correlation helper is `buffer_comparison.h:201`, `template <size_t N> float calculateCorrelation(const std::array<float, N>&, const std::array<float, N>&)`, a compile-time-sized form unusable on a multi-minute heap render, so SC-007 adds a pointer/length overload beside it; (iii) a **sub-bin peak-frequency estimator** — `spectral_analysis.h` exports only `frequencyToBin`, `getHarmonicBins`, `sumBinPower` and `toDb`, and **no interpolation of any kind exists anywhere under `tests/test_helpers/`**, so a raw peak bin cannot resolve SC-003's tolerances (see SC-003); SC-003 adds `estimatePeakFrequencyHz`; (iv) a **low-frequency THD** measurement, because `calculateTHD` cannot resolve this band (row above); SC-004 adds `measureLowFrequencyThdPercent`. |
| Perf-test idiom | `dsp/tests/unit/systems/resonance_drift_network_perf_test.cpp:1-90`; `dsp/tests/unit/systems/atmosphere_engine_perf_test.cpp` | The measurement basis SC-013 inherits: **nanoseconds per 512-sample block at 48 kHz** (`:66-76` — "A percent-of-core figure is not reproducible across dev machines or CI runners"), best-of-25 × 500 blocks after 400 warm-up blocks (`:78-84`), tagged `[.perf]` so the per-push CI filter `~[performance]~[perf]~[benchmark]~[!benchmark]~[long]` excludes it, with `static_assert`s on the checked-in baselines so the absolute ceiling is still evaluated on every CI leg. One 512-sample block period at 48 kHz is **10 666 667 ns**, so the roadmap's 0.5 % is **53 333 ns/block**. The **stop-and-surface rule** at `:57-64` is inherited verbatim by FR-071. |

## New components

ODR sweep run **this session**, verbatim, from the repo root:

```
$ grep -rn "class SubharmonicEngine"    dsp/ plugins/  -> 0 hits
$ grep -rn "class SubEngine"            dsp/ plugins/  -> 0 hits
$ grep -rn "class SubharmonicGenerator" dsp/ plugins/  -> 0 hits
$ grep -rn "class SubharmonicBank"      dsp/ plugins/  -> 0 hits
$ grep -rn "class SubOscillatorBank"    dsp/ plugins/  -> 0 hits
$ grep -rn "class SubDivider"           dsp/ plugins/  -> 0 hits
$ grep -rn "class SubVoice"             dsp/ plugins/  -> 0 hits
$ grep -rn "class SubTone"              dsp/ plugins/  -> 0 hits
$ grep -rn "struct SubTone"             dsp/ plugins/  -> 0 hits
$ grep -rn "class SubChain"             dsp/ plugins/  -> 0 hits
$ grep -rn "class SubStack"             dsp/ plugins/  -> 0 hits
$ grep -rn "class FifthBelowOscillator" dsp/ plugins/  -> 0 hits
$ grep -rn "class WeightEngine"         dsp/ plugins/  -> 0 hits
$ grep -rn "\bSubharmonicEngine\b|\bSubEngine\b|\bSubDivider\b|\bSubTone\b|\bSubVoice\b" dsp/ plugins/ tools/ -> 0 hits
$ grep -rni "subharmonic" dsp/ plugins/  -> 85 hits, in 13 files, ALL of them
      dsp/include/krate/dsp/processors/subharmonic_validator.h   (SubharmonicValidator)
      dsp/tests/CMakeLists.txt, dsp/tests/unit/processors/subharmonic_validator_tests.cpp
      plugins/innexus/{src/dsp/live_analysis_pipeline.{h,cpp}, src/dsp/sample_analyzer.cpp,
                       tests/integration/sample_load_e2e_tests.cpp, CHANGELOG.md, docs/*.md}
      plugins/membrum/preset-retune/{archetypes/*.md, recipes.json}
$ ls dsp/include/krate/dsp/systems/subharmonic_engine.h -> No such file or directory
```

| Class / symbol | Layer | Header path | ODR sweep result |
|---|---|---|---|
| `SubharmonicEngine` | 3 | `dsp/include/krate/dsp/systems/subharmonic_engine.h` (new) | **0 hits.** The one near-name in the tree is `SubharmonicValidator` (`processors/subharmonic_validator.h:47`), read this session and **entirely unrelated**: it is a Hermes-style subharmonic-summation octave-error corrector for YIN F0 estimates (`:39-46`, reference at `:20-22`), consumed only by Innexus's analysis pipeline. It shares a word, not a concept, not a namespace member, not a symbol. Other near-names that exist and are not shadowed: `SubOscillator` (`processors/sub_oscillator.h:111`), `SubOctave` / `SubWaveform` (`:48` / `:60`). None collide. |
| `SubharmonicEngine::PrepareConfig` (nested struct) | 3 | same header | Nested, following `NoiseOrganism::PrepareConfig` (`noise_organism.h:190`) and `ResonanceDriftNetwork::PrepareConfig` (`resonance_drift_network.h:299`). Several unrelated nested `PrepareConfig` structs already coexist, so nesting is the established, collision-free form. |
| `SubharmonicEngine::Tone` (nested enum class) | 3 | same header | Nested. `{ Div2 = 0, Div4 = 1, FifthBelow = 2 }`. **APPEND ONLY** — it becomes a persisted plugin parameter at Phase 12. Deliberately nested rather than namespace-scope: `SubOctave` and `SubWaveform` (`sub_oscillator.h:48`, `:60`) are already at namespace scope in a header this component includes, and a third `Sub*` enum out there is a future ODR liability for no gain. `EnvelopeFilter`'s nested `FilterType` (`processors/envelope_filter.h:89`) is the precedent for exactly this collision-avoidance move. |
| `SubharmonicEngine::ToneState` (nested private struct) | 3 | same header | Nested and **private**, following `ResonanceDriftNetwork::Peak` and `NoiseOrganism::DustGrain` (`noise_organism.h:196-200`). Holds one `SubOscillator`, one `BreathingModulator`, one `LinearRamp` gate and the tone's scalar configuration. Names swept clean at namespace scope (`SubTone`, `SubVoice`, `SubDivider`: 0 hits each) and **still rejected** as top-level names — a namespace-scope type for a private implementation detail is a liability for no gain. |

**No new namespace-scope enum type, no new enumerator on an existing enum, no new free function, no
new `ModSource` value.** `SubOctave` and `SubWaveform` are reused verbatim from
`sub_oscillator.h:48,60`. `tools/lint-odr.js` and `tools/lint-layers.js` must pass on the result
(SC-015).

## Functional Requirements

### FR-001 series — Component contract and lifecycle

- **FR-001** — `SubharmonicEngine` is a Layer 3 class in `namespace Krate::DSP`, declared in
  `dsp/include/krate/dsp/systems/subharmonic_engine.h`, header-only. Its includes reach **down only**:
  `core/db_utils.h`, `core/math_constants.h`, `core/phase_utils.h`, `core/random.h`,
  `primitives/smoother.h`, `primitives/minblep_table.h`, `primitives/two_pole_lp.h`,
  `primitives/dc_blocker.h`, `processors/sub_oscillator.h`, `processors/breathing_modulator.h`,
  `processors/envelope_follower.h`, `processors/saturation_processor.h`, plus
  `<algorithm> <array> <cmath> <cstddef> <cstdint>`. It includes **no** Layer 3 or Layer 4 header.
  `tools/lint-layers.js` must pass.
- **FR-002** — The component declares **no new enum at namespace scope**. `SubOctave` and
  `SubWaveform` (`sub_oscillator.h:48`, `:60`) are used as-is. `Tone` is nested (see New components).
- **FR-003** — Construction is **trivial and allocation-free** and initialises every configuration
  scalar to the same defaults `prepare()` restores in step (6), so every getter reports a sane value
  on a default-constructed object. **Audio state is not usable until `prepare()` has run** — it
  cannot be, because `prepare()` is the only method that prepares the `MinBlepTable`, without which
  every `SubOscillator` hard-fails to `prepared_ = false` and returns `0.0f` forever
  (`sub_oscillator.h:144-147`, `:224-226`). The pre-prepare audio contract is FR-050's passthrough
  guard, not silence-by-accident.
  `void prepare(double sampleRate, const PrepareConfig& config) noexcept` is the **only** allocating
  method; every other public method is `noexcept` and allocation-free. `PrepareConfig` fields:
  `std::size_t maxBlockSamples = 2048`, clamped `[64, 8192]`, retained and reported. Callers **must**
  use designated initialisers so no narrowing conversion hides in a positional brace init (Clang
  errors where MSVC does not).
- **FR-004** — `prepare()` runs in this order, and the order is a correctness requirement, not
  tidiness: (1) sanitise and floor the sample rate at `kMinUsableSampleRate = 8000.0`; (2)
  `blepTable_.prepare(64, 8)` — **before** any sub-oscillator, because `SubOscillator::prepare`
  hard-fails to a permanently silent state on an unprepared table (`sub_oscillator.h:144-147`);
  (3) assign each `SubOscillator` from `SubOscillator(&blepTable_)` and call its `prepare(sampleRate)`;
  (4) prepare the three `BreathingModulator`s, the `EnvelopeFollower`, the `TwoPoleLP`, the
  `SaturationProcessor` (with `config.maxBlockSamples`) and the `DCBlocker2` at `kInfrasonicFilterHz`
  (FR-042); (5) configure every `LinearRamp` at `kGainRampMs` (`levelRamp_i`, `gate_i`, the FR-032
  tracking ramp and the FR-051 wet-gain ramp); (6) restore **every** default table and scalar
  (FR-013, FR-020, FR-021, FR-030, FR-035, FR-040, FR-041, FR-050, FR-064); (7) re-derive
  rate-dependent state and snap every ramp to its target so the object is at steady state with no
  ramp in flight; (8) `reset()`.
- **FR-005** — Every call to `prepare()`, including a re-prepare on a live object, restores every
  default in step (6). `reset()` does **not** touch configuration: it zeroes both master phase
  accumulators, resets all three `SubOscillator`s and `BreathingModulator`s, the follower, the
  low-pass, the saturator and the DC blocker, snaps every ramp to its target, and clears the clamp
  engagement counter. A host sample-rate change therefore requires re-pushing the full parameter set
  after `prepare()` — a note that carries forward to Phase 11.
- **FR-006** — `kMinUsableSampleRate = 8000.0`. `prepare()` floors the requested rate at it and
  reports the applied rate through `getSampleRate()`. Rationale is the same one
  `resonance_drift_network.h:255-281` records: several clamp ranges in this component are built from
  a `[floor, ratio * fs]` pair, and `std::clamp` has the precondition `!(hi < lo)` — violating it is
  UB and MSVC's `<algorithm>` traps it.
- **FR-007** — `kControlChunkSamples = 64`, with
  `static_assert(kControlChunkSamples == 64, "shared 64-sample control grid")`. This is the shared
  library-wide control clock (`harmonic_cloud.h:144`, `continuous_body.h:97`, `noise_organism.h:150`,
  `resonance_drift_network.h:135`); a component that drifted off it would decorrelate the per-voice
  modulation grid at Phase 10. The control phase is carried as an **absolute** residue across calls,
  not block-relative, so a 36 + 28 split runs the same number of control steps as an unsplit 64.
- **FR-008** — Finiteness is tested with `detail::isNaN` / `detail::isInf` / `detail::isFinite`
  (`core/db_utils.h:99`, `:260`, `:118`) only. `std::isnan`, `std::isinf` and `std::isfinite` are
  forbidden: the macOS leg builds with `-ffast-math`, under which they fold. `tools/lint-nonfinite-symbols.js`
  must pass.
- **FR-009** — Normative argument contract, identical to Phases 2, 3 and 5: an out-of-range tone
  index is a **silent no-op** on a setter and returns a documented neutral from a getter; a
  **non-finite** float argument is **rejected** and the previous value stands; an out-of-range finite
  float is **clamped**, and the corresponding getter reports the clamped value, not the request.

### FR-010 series — The three tones and their synthetic masters (roadmap lines 309–311)

- **FR-010** — The engine owns exactly three `SubOscillator` instances, indexed by
  `Tone { Div2 = 0, Div4 = 1, FifthBelow = 2 }`, `kNumTones = 3`. Each is driven through
  `process(bool masterPhaseWrapped, float masterPhaseIncrement)` (`sub_oscillator.h:222`);
  `processMixed` is never called and `setMix` is never called, so `mix_` stays at its constructed
  `0.0f` and has no effect on `process()`.
- **FR-011** — The engine owns exactly **two** `PhaseAccumulator`s (`core/phase_utils.h:154`), both
  advanced once per sample by `advance()`:
  - `masterUnison_` at the fundamental `f`, driving `Div2` (`SubOctave::OneOctave` → `f/2`) and
    `Div4` (`SubOctave::TwoOctaves` → `f/4`). One accumulator drives both, because
    `SubOscillator::process` takes the wrap flag and increment as *arguments* and keeps its own
    divider state.
  - `masterFifth_` at `4f/3`, driving `FifthBelow` (`SubOctave::OneOctave` → `(4f/3)/2 = 2f/3`).
- **FR-012** — The `4f/3` master is the entire mechanism by which the fifth below is produced, and it
  is why **no `SubOscillator` extension is required** (roadmap line 531 anticipated one). The
  resulting tone frequencies, reported by `getToneFrequencyHz(tone)`, are exactly
  `{f/2, f/4, 2f/3}`.
- **FR-013** — `void setFundamentalHz(float hz) noexcept`. Clamped to
  `[kMinFundamentalHz = 8.0f, min(kMaxFundamentalHz = 4186.0f, kMasterNyquistRatio * fs)]` where
  `kMasterNyquistRatio = 0.3f` — the ratio is applied to the **`4f/3` master**, not to `f`, so the
  highest master increment is `0.4 * fs`, safely inside the `PhaseAccumulator` contract at every
  accepted rate. Non-finite is rejected (FR-009). **Default `kDefaultFundamentalHz = 55.0f`** (A1,
  the standard low-drone reference pitch). `getFundamentalHz()` reports the clamped value.
  **Why 55 Hz and not the 82.5 Hz of an earlier draft:** that draft raised the default because
  FR-016's then-16 Hz hard mute silenced `Div4` (13.75 Hz) at `f = 55`. Per the Clarifications
  session (Q2), FR-016 no longer hard-mutes at 16 Hz: it replaces the chain's DC-blocking stage with
  an 18 Hz 2nd-order Bessel high-pass and keeps only a far-below **backstop** gate at `kMinToneHz =
  12.0f`. At `f = 55 Hz`, `Div4 = 13.75 Hz` clears that backstop (`13.75 > 12`), so all three tones
  are awake at the 55 Hz default (`f/4 = 13.75`, `f/2 = 27.5`, `2f/3 = 36.67` Hz) without needing to
  raise the fundamental — "at defaults, all three tones awake" (SC-013, SC-017) is reachable at the
  instrument's own standard drone pitch.
- **FR-014** — A fundamental write updates both accumulators' `increment` and **does not** touch
  their `phase`, so a pitch change is phase-continuous (`PhaseAccumulator::setFrequency` writes
  `increment` only, `phase_utils.h:177`). `reset()` zeroes both phases together, which is what makes
  the two masters' relative alignment deterministic: they realign every three cycles of `f`.
- **FR-015** — `void setToneWaveform(std::size_t tone, SubWaveform waveform) noexcept`. Defaults:
  `Div2 = SubWaveform::Sine`, `Div4 = SubWaveform::Sine`, `FifthBelow = SubWaveform::Sine`. Rationale
  in D-6; the in-scope-versus-not argument (against D-1's rejection of a saturation-type selector) is
  D-10. `Square` and `Triangle` remain reachable, SC-005 measures the Square path's spectrum, and
  SC-013 (b) **gates** the all-`Square` CPU arm at the same ceiling as the default arm — so no
  reachable waveform configuration is shipped unmeasured or unbudgeted.
- **FR-016** — **Infrasonic handling: filter first, backstop gate second** (revised per the
  Clarifications session, Q2 — the earlier draft's 16 Hz hard mute is withdrawn). Two independent
  mechanisms, in the order the roadmap's concern (pumping / excursion hazard from a near-DC tone)
  actually requires:
  1. **The chain's stage-3 blocker is `DCBlocker2` (`primitives/dc_blocker.h:272`), a 2nd-order
     Bessel high-pass at `kInfrasonicFilterHz = 18.0f`**, in place of the single-pole `DCBlocker` an
     earlier draft used (FR-042 carries the full chain change). At Bessel `Q = 1/sqrt(3) ≈ 0.577`
     the filter's magnitude response has no resonant peak (`Q <= 1/sqrt(2)`), so it only ever
     attenuates below its corner and only ever approaches unity gain above it. At 13.75 Hz (`Div4`
     at the FR-013 default), the analog-prototype estimate is ≈ −7.5 dB (`Omega = 13.75/18 =
     0.764`, `|H|^2 = Omega^4 / ((1 - Omega^2)^2 + (Omega/Q)^2) ~= 0.177`) — materially more
     attenuation than the single-pole blocker's ≈ −2 dB at 10 Hz gave the same tone, and it applies
     continuously across the whole low register rather than as a step. This mechanism, not a gate,
     is what addresses ordinary pumping in the register the instrument is actually played in.
  2. **A far-below backstop gate, `kMinToneHz = 12.0f`.** A tone whose FR-012 frequency is below
     `kMinToneHz` has its per-tone `gate` (the `LinearRamp` of Q1/FR-022's three-factor form) target
     forced to `0.0f`, retargeted at that edge and then ramped over `kGainRampMs = 50.0f` like every
     other gate transition, so a crossing is click-free; `isToneInfrasonicFloored(tone)` reports the
     state. This backstop exists for the case the filter alone does not solve — a tone close enough
     to DC that "attenuated" is not the same as "safe" — and is deliberately set far below the
     register the filter already protects. It is a fixed constant, not a parameter: no
     configurability is added that was not requested.
  **The backstop's exact reach, stated so no fixture collides with it by accident.** Solving
  `toneHz >= kMinToneHz` per tone: `Div4` (`f/4`) floors below **`f = 48 Hz`**; `Div2` (`f/2`) below
  **`f = 24 Hz`**; `FifthBelow` (`2f/3`) below **`f = 18 Hz`**. At the FR-013 default of `55 Hz`
  **no tone is floored** (`Div4 = 13.75 > 12`), which is what makes "at defaults, all three tones
  awake" (SC-013, SC-017) reachable at the instrument's own standard drone pitch, with `Div4`
  additionally carrying the filter's ≈ −7.5 dB attenuation rather than silence. Every spectral
  criterion (SC-003, SC-004, SC-005) picks its fundamentals **per tone** from a range that clears
  this backstop, and SC-020 is the criterion that measures both mechanisms.

### FR-020 series — Per-tone level, breathing and the tone gate (roadmap line 311)

- **FR-020** — `void setToneLevelDb(std::size_t tone, float db) noexcept`, clamped
  `[kMinToneLevelDb = -60.0f, kMaxToneLevelDb = +6.0f]`. A level at exactly `kMinToneLevelDb` maps to
  a linear gain of **exactly 0.0f** (a fader bottom, not −60 dB), which is what makes FR-023's
  dormancy test an exact `== 0.0f` comparison rather than an epsilon. **Defaults (revised per the
  Clarifications session, Q3):** `Div2 = -18.0f`, `Div4 = -24.0f`, `FifthBelow = -30.0f` dB — 12 dB
  lower than an earlier draft's `-6 / -12 / -18`. The earlier defaults, combined with
  `kDefaultWetGainDb = 0`, `kDefaultDriveDb = 3` and `trackingAmount = 1` against a −12 dBFS body,
  put the sub *louder* than the body it is meant to sit beneath — a defect in a "weight under the
  drone" layer. The new defaults are chosen to sit the default sub add roughly 9 dB under a
  −12 dBFS body; SC-021 measures and pins the resulting ratio from a real render so a regression
  cannot silently re-introduce a sub-dominant default. `getToneLevelDb(tone)` reports the clamp.
- **FR-021** — Each tone owns one `BreathingModulator` (`processors/breathing_modulator.h:105`).
  `void setToneBreathRate(std::size_t tone, float hz) noexcept` clamps to the modulator's own
  `[kMinRate = 0.01f, kMaxRate = 0.5f]` (`:108-110`);
  `void setToneBreathDepth(std::size_t tone, float normalized) noexcept` clamps to `[0, 1]` and
  stores it as the engine's own `depth_i` — **it is not forwarded to `BreathingModulator::setDepth`**
  (revised per the Clarifications session, Q7). The modulator is left at its library default depth
  of `1.0f`, so `getCurrentValue()` (`:222`) returns the raw bipolar breathing value `b_i` in its
  documented `{-1, +1}` range (`getSourceRange()`, `:226-227`) with no per-tone scaling applied
  inside the modulator; the engine applies depth itself, once, in FR-022's `breathGain_i`. This is
  the house affine-span convention `NoiseOrganism` uses (`noise_organism.h:174`, `updateBreathGain`
  at `:1842-1844`), not FR-021's own `setDepth` passthrough as an earlier draft had it. Under that
  earlier passthrough, FR-022's bracket spanned `[0, 2]` and a tone at depth 1.0 (SC-017 (B)'s
  worst-case arm) muted completely once per breath cycle — tremolo, not "slow level-breathing".
  `getToneBreathDepth` reports the clamped `depth_i` the engine holds.
  Defaults — deliberately different per tone so the three never breathe in lockstep:
  rates `{0.037, 0.023, 0.014}` Hz (27 s, 43 s, 71 s per breath), depths `{0.35, 0.25, 0.45}`. Under
  FR-022's revised affine map (`kBreathGainSpan = 0.45`) these give per-tone swings of
  approximately `Div2: +1.27 / -1.49 dB`, `Div4: +0.93 / -1.04 dB`, `FifthBelow: +1.60 / -1.96 dB` —
  audible motion, never a mute, at every default depth.
- **FR-022** — **The house three-factor gain form** (revised per the Clarifications session, Q1,
  following `noise_organism.h:937` `getSourceGain` / `:1842-1844` `updateBreathGain`), replacing an
  earlier draft's single composite ramp. The **effective tone gain** is
  `g_i = levelRamp_i.getCurrentValue() * breathGain_i * gate_i.getCurrentValue()`, where:
  - `levelRamp_i` is a per-tone `LinearRamp` at `kGainRampMs = 50.0f` whose **target is retargeted
    only when `setToneLevelDb` writes a new value** for that tone, target `= dbToGain(level_i)`
    (FR-020).
  - `breathGain_i = 1.0f + kBreathGainSpan * depth_i * b_i`, `kBreathGainSpan = 0.45f`
    (FR-021/Q7), recomputed **once per 64-sample control chunk** and held constant across the
    chunk's samples — it is not a ramp, following the house convention that breathing's own motion
    is slow enough (kMinRate/kMaxRate, FR-021) that a control-step staircase is inaudible. Because
    `b_i` (`BreathingModulator::getCurrentValue()`) is bounded to `[-1, +1]` and `depth_i` to
    `[0, 1]`, `breathGain_i` is bounded to `[1 - kBreathGainSpan, 1 + kBreathGainSpan] = [0.55, 1.45]`
    by construction — no separate clamp is needed, and a tone can never mute or invert polarity
    through breathing alone.
  - `gate_i` is a per-tone `LinearRamp` at `kGainRampMs = 50.0f` whose **target is retargeted only at
    a floor/dormancy edge** — specifically, the FR-016 backstop transition. Its `process()` is
    called every sample regardless of what retargets it, which is what makes the 50 ms fade a fact:
    unlike a ramp retargeted every control chunk (whose `LinearRamp::setTarget` recomputes from the
    *remaining* distance, `smoother.h:342-354`, and so never truly arrives), a ramp retargeted only
    at edges reaches its target and stays there, and SC-020 (c)'s "reaches its new target within
    50 ms ± (one control step) and is monotonic" is measurable exactly as written.
  `getToneCurrentGain(tone)` reports the product `g_i`.
- **FR-023** — A tone is **dormant** when its `levelRamp_i` target and current value are both exactly
  `0.0f` (this is the FR-020 fader-bottom case, not the FR-016 backstop — a backstop-gated tone at a
  non-zero level is silenced via `gate_i` but is not "dormant" in this sense, since raising its
  fundamental above the backstop revives it with no level write at all). `isToneDormant(tone)`
  reports it. "Dormant" and "awake at zero gain" are behaviourally identical and differ only on the
  read surface, exactly as roadmap lines 519–520 require.
- **FR-024** — The three `BreathingModulator`s are advanced by `processBlock(chunk)`
  (`breathing_modulator.h:209`) once per control chunk, and `getCurrentValue()` — a **virtual** call
  (`:222`) — is read once per control chunk per tone, never per sample.
- **FR-025** — **Dormancy, evaluated at control-step granularity, and this phase's stated deviation
  from it** (granularity revised per the Clarifications session, Q5, following the house form at
  `feedback_ecology.h:1878-1905`). The engine-wide dormancy condition — every tone's `levelRamp_i`
  target and current value exactly `0.0f` — is evaluated **once per 64-sample control chunk**
  (`kControlChunkSamples`, FR-007), not per sample and not per host block, so a `numSamples`
  partition that splits a control chunk across two `processBlock` calls still evaluates the
  condition on the same schedule (FR-007's absolute control-phase residue is what makes this
  partition-invariant, SC-012). While the condition holds, the **shared chain** (FR-040 low-pass,
  FR-041 saturator, FR-032's per-sample tracking multiply, FR-042 filter, FR-050 wet gain and sum)
  is **skipped** for that chunk's samples, the FR-062 tap is written with zeros, and the output is
  the input, bit-identical (SC-014 (a)). A tone write that ends dormancy takes effect in the audio
  within **at most one control chunk (≤ 64 samples, ≤ 1.333 ms at 48 kHz)** of the write, since the
  condition is only re-checked at the next chunk boundary.
  **Two per-sample ramps keep advancing while the chain is skipped**, so neither begins a wake from
  a stale value: FR-032's `trackGain` ramp and FR-051's wet-gain ramp. Together with the
  `PhaseAccumulator`s, the three `SubOscillator`s, the three `BreathingModulator`s and the
  `EnvelopeFollower` (which also keep advancing) — they are the component's generators, sensor and
  the two ramps a wake reads from — this is what roadmap line 518 requires.
  **The deviation:** an individual tone at zero gain does **not** have its `SubOscillator::process`
  call skipped. The roadmap demands a justification in terms of what the listener would hear
  (line 521), and it is this: a `SubOscillator`'s waveform position is state — the flip-flops at
  `sub_oscillator.h:252,261` and the sub-phase accumulator at `:302` — so freezing it means a wake
  re-enters mid-waveform at a phase unrelated to where the master now is. At these frequencies the
  50 ms fade cannot mask that: 50 ms is exactly one period of a 20 Hz tone and *shorter* than one
  period of every tone the FR-016 backstop admits below 20 Hz, so the fade envelope and the
  discontinuity occupy the same time scale and the listener hears a thump at the very bottom of the
  spectrum — the one artefact this phase exists to avoid. The tone generator costs three flops and one
  `std::sin`; the chain it protects is the expensive part, and that *is* skipped.
- **FR-026** — **Sleep edge, evaluated at control-step granularity** (revised per Q5). On the
  64-sample control chunk at which the engine-wide dormancy condition of FR-025 first holds for the
  whole chunk, the chain's audio state is cleared at the start of that chunk: `TwoPoleLP::reset()`,
  `SaturationProcessor::reset()`, `DCBlocker2::reset()`. Without this, a stale tail sits in the
  filter's `y1_`/`y2_` and replays on wake. The `EnvelopeFollower` is **not** reset — it is the
  sensor and must keep tracking the body while the subs sleep.

### FR-030 series — Envelope tracking of the voice body (roadmap lines 312–313)

- **FR-030** — The engine owns one `EnvelopeFollower` (`processors/envelope_follower.h:82`) in
  `DetectionMode::RMS`, fed the **mono sum** `0.5f * (inL[n] + inR[n])` once per sample. The
  sidechain high-pass stays **disabled** (`setSidechainEnabled(false)`): its range starts at 20 Hz
  (`:94`) and enabling it would blind the follower to the low body content the subs exist to follow.
- **FR-031** — `void setFollowerAttackMs(float ms)` / `void setFollowerReleaseMs(float ms)`, clamped
  to the follower's own `[0.1, 500]` / `[1, 5000]` (`:88-91`). Defaults
  `kDefaultFollowerAttackMs = 120.0f`, `kDefaultFollowerReleaseMs = 800.0f` — a drone's body moves on
  the scale of seconds, and a fast follower would make the subs chatter.
- **FR-032** — **The tracking law.** Once per control chunk the engine computes
  `envNorm = clamp(follower.getCurrentValue() / trackReferenceRms, 0.0f, 1.0f)` with
  `trackReferenceRms = dbToGain(trackReferenceDb)`, **`trackReferenceDb` now a settable calibration
  (FR-035, revised per the Clarifications session, Q4) rather than a fixed constant** — default
  `kDefaultTrackReferenceDb = -18.0f` (`trackReferenceRms ≈ 0.12589` at the default), then
  `trackGain = (1 - trackingAmount) + trackingAmount * envNorm`, written as the target of a
  `LinearRamp` at `kGainRampMs`, whose `process()` runs per sample. This is a **linear-in-amplitude**
  law by design: no `std::log` on any path, and the sub amplitude is proportional to the body
  amplitude below −18 dBFS RMS and constant above it. SC-002 measures exactly that (unity slope in
  dB below the reference, flat above).
  **Where the per-sample `trackGain` is multiplied in, normatively:** it multiplies the chain signal
  **after FR-041's saturator and before FR-042's DC blocker** — chain stage 2.5 of four, as FR-040
  spells out. Two consequences, both intended and neither incidental:
  (a) the saturator sees a **level-independent drive**, so FR-041's shaping and SC-004's THD figure
  do not move with the body level (had `trackGain` multiplied the sub sum *before* the low-pass, a
  quiet body would also mean a clean sub and a loud body a dirty one — a coupling no roadmap line
  asks for and SC-004 could not pin);
  (b) it is **inside** FR-062's tap, which is the only reason SC-002 can measure the tracking law at
  all. `trackGain` is **not** part of the wet gain and is **not** applied to the dry path.
- **FR-033** — `void setTrackingAmount(float normalized) noexcept`, clamped `[0, 1]`, default
  `kDefaultTrackingAmount = 1.0f`. At 1.0 the subs are fully gated by the body — the roadmap's
  "no free-running boom" (line 319). At 0.0 they are free-running, which is a legitimate patch and
  the reason the parameter exists (in-scope argument: D-10); SC-002 (c) asserts the free-running arm
  is still bounded and silent-input-safe only in the sense that it is *level-stable*, not silent, and
  SC-017 (B) soaks it for ten minutes at every other control's worst-case setting.
- **FR-034** — `getTrackedEnvelope()` reports the last computed `envNorm` and
  `getTrackingGain()` reports the ramp's current value, so SC-002 can assert the law rather than
  infer it from audio alone.
- **FR-035** — **Settable tracking reference (new, Q4).** `void setTrackReferenceDb(float db) noexcept`,
  clamped `[-48.0f, 0.0f]`, default `kDefaultTrackReferenceDb = -18.0f`; non-finite is rejected
  (FR-009). `getTrackReferenceDb()` reports the clamped value. This is what makes OQ-1's
  placement-agnosticism claim true rather than assumed: a fixed `-18 dBFS` reference implicitly
  assumes the global placement (post-voice-sum, where the body routinely sits near or above
  `-18 dBFS` with tracking pinned at 1.0), while one voice of an eight-voice chord in the per-voice
  placement sits 15–25 dB quieter, so the same patch would sound materially different depending on
  how OQ-1 is ruled unless the reference can be recalibrated. Phase 10 sets it once per placement;
  this phase ships the knob and SC-002 (e) proves it is load-bearing.

### FR-040 series — The shared chain (roadmap lines 314–315)

**The chain, in full and in order** (this is the normative signal path; each stage's FR follows):

```
subMonoSum = Σ_i g_i * subOsc_i.process(...)        (FR-022 per-tone gains)
  → stage 1: TwoPoleLP                              (FR-040)
  → stage 2: SaturationProcessor::processSample     (FR-041)
  → stage 2.5: × trackGain (per-sample LinearRamp)  (FR-032)
  → stage 3: DCBlocker2 (~18 Hz Bessel HP)           (FR-042, revised Q2)
  = subChain                                        ← FR-062's tap point (supported output, Q8)
  → clampedSub = clamp(subChain * wetGain, ±kOutputClamp)   (FR-050, FR-051, FR-054, revised Q6)
  → out = in + (subToMainEnabled ? clampedSub : 0.0f)       (FR-050, FR-064, new Q8)
```

- **FR-040** — Stage 1 is one `TwoPoleLP` (`primitives/two_pole_lp.h:49`) on the mono sub sum.
  `void setLowpassCutoffHz(float hz) noexcept` clamps to
  `[kMinLowpassHz = 40.0f, min(kMaxLowpassHz = 2000.0f, 0.45f * fs)]`, default
  `kDefaultLowpassHz = 120.0f`. Cutoff writes are **control-surface only** — never on the per-sample
  path — because `TwoPoleLP::setCutoff` runs `Biquad::configure`, i.e. trigonometry (`:109-118`).
  The low-pass is placed **before** the saturator so the nonlinearity sees a band-limited input and
  its aliasing products stay far below Nyquist; this is why no oversampling is specified.
- **FR-041** — Stage 2 is one `SaturationProcessor` (`processors/saturation_processor.h:98`), type
  fixed to `SaturationType::Tape` (D-1: `saturateTape` is `Sigmoid::tanh`, `:343-347` — symmetric,
  odd-harmonic, DC-free, and therefore the only one of the five that does not fight FR-042 and
  SC-007), driven through `processSample` (`:228`) — **not** the block path, whose `dryBuffer_`
  the engine never uses. `void setDriveDb(float db) noexcept` clamps to
  `[kMinDriveDb = 0.0f, kMaxDriveDb = 12.0f]`, default `kDefaultDriveDb = 3.0f`, and writes
  `setInputGain(db)` together with the complementary `setOutputGain(-db)` so drive is a **shape**
  control at approximately constant level. `mix_` stays at its constructed `1.0f` (`:414`), above the
  `< 0.0001f` dry early-exit at `:238-240`.
  **The saturator's drive is level-independent by construction**, because FR-032's `trackGain` is
  applied *after* this stage: the shaper always sees the same signal for a given tone
  configuration, whatever the body is doing. That is what makes SC-004's THD figure a property of
  the dividers and the shaper rather than of the test's input level.
  Note that `mix_ = 1.0f` means the `Sigmoid::tanh` shaper is **always in circuit, including at
  `kMinDriveDb = 0`**; its own third-harmonic contribution for a sine of amplitude `A` is ≈ `A²/24`
  relative, which is why SC-004 pins the tone level under test rather than leaving it at default.
- **FR-042** — **Stage 3 is one `DCBlocker2` (`primitives/dc_blocker.h:272`)** — a 2nd-order Bessel
  high-pass, `Q = 1/sqrt(3) ≈ 0.577` — **configured at `kInfrasonicFilterHz = 18.0f`, per-sample**
  (revised per the Clarifications session, Q2; an earlier draft used the single-pole `DCBlocker` at
  10 Hz). It is last in the chain because the two stages before it self-heal on a non-finite input
  while it does not (`process()` has no finiteness branch, the same propagation risk the single-pole
  form had), and because it must sit after the FR-032 tracking multiply so a moving `trackGain`
  cannot leave a DC step in the output. Moving to `DCBlocker2` at 18 Hz is what does the work FR-016
  describes: at 13.75 Hz (`Div4` at the FR-013 default) the attenuation is ≈ −7.5 dB, versus the
  single-pole blocker's ≈ −2 dB at 10 Hz for the same tone — enough to turn "amplitude-modulating
  pulse train" into "quiet, filtered tone" across the register the instrument is actually played in,
  with the FR-016 backstop gate catching what the filter alone does not.
- **FR-043** — The chain is **mono**: one low-pass, one saturator, one tracking multiply, one
  blocker, one signal. There is
  no per-channel state anywhere in the sub path. This is the structural mechanism behind SC-007, not
  a saving.

### FR-050 series — Output, mix and the safety ladder

- **FR-050** — `void processBlock(const float* inL, const float* inR, float* outL, float* outR, std::size_t numSamples) noexcept`
  writes `out = in + (subToMainEnabled ? clampedSub : 0.0f)` on both channels — an **add**, not a
  crossfade — where `subChain` is the full FR-040 → FR-041 → FR-032 → FR-042 chain output defined in
  the block diagram above (the tracking gain is already in it), `clampedSub =
  clamp(subChain * wetGain, -kOutputClamp, +kOutputClamp)` is **FR-054's clamp applied to the sub
  contribution only** (revised per the Clarifications session, Q6 — an earlier draft clamped the
  summed output, dry included), and `subToMainEnabled` is the FR-064 routing flag (new, Q8),
  default `true`. The same scalar sequence is added to both channels. `outL`/`outR` may alias
  `inL`/`inR` (in-place is supported and tested). A null input or output pointer, or
  `numSamples == 0`, is a silent no-op.
  **The dry path is never touched by anything this component computes on the sub**: whatever `in` is
  — silent, full-scale, non-finite — the clamp and the FR-055 non-finite trap apply only to
  `subChain`/`clampedSub`, never to `in` itself, so `getClampEngagementCount()` is a pure statement
  about this component's own ladder and never about the level of a host's audio passing through it.
  **Before `prepare()` has completed** (FR-003), `processBlock` and `processBlockTapped` copy input
  to output unmodified, advance no state, write nothing beyond `numSamples`, and write zeros to
  `subTap` when it is non-null. This is a contract, not a consequence of the sub-oscillators being
  unprepared.
- **FR-051** — `void setWetGainDb(float db) noexcept`, clamped
  `[kMinWetGainDb = -60.0f, kMaxWetGainDb = +6.0f]`, default `kDefaultWetGainDb = 0.0f`; exactly
  `kMinWetGainDb` maps to a linear gain of exactly `0.0f`. Smoothed by a `LinearRamp` at
  `kGainRampMs`. When the wet gain is exactly zero and steady **and** FR-025's dormancy condition
  holds, the output is **bit-identical** to the input (SC-014 (a)).
- **FR-052** — Rung 1 of the safety ladder is structural: every tone gain, the tracking gain and the
  wet gain are bounded above, and the low-pass is non-expansive. Two distinct bounds, named
  separately because an earlier draft conflated them, **both recomputed this session** (Q2, Q7):
  - `kMaxPreSaturationMagnitude = kNumTones * dbToGain(kMaxToneLevelDb) * (1.0f + kBreathGainSpan)`
    ≈ `3 * 1.9953 * 1.45` ≈ **8.68** — the worst-case magnitude *entering* the saturator (three
    tones, each at `+6 dB`, each with FR-022's `breathGain_i` at its `1 + kBreathGainSpan = 1.45`
    ceiling). Down from an earlier draft's ≈ 11.97, which used the withdrawn `(1 + breath)` bracket
    at its `[0, 2]` ceiling (Q7): the house affine span bounds `breathGain_i` to `[0.55, 1.45]`
    instead, so the worst case is smaller by construction. This is the number that says the
    saturator is always the stage that catches the peak, never the clamp.
  - `kMaxPreClampMagnitude = kSaturatorOutputBound * kInfrasonicFilterPeakGain * dbToGain(kMaxWetGainDb)`
    ≈ `1.0 * 1.0 * 1.9953` ≈ **2.00** — the worst-case magnitude *reaching* the FR-054 clamp (applied
    to the sub contribution only, Q6), where `kSaturatorOutputBound = 1.0f` is FR-053's `tanh` range,
    `kInfrasonicFilterPeakGain = 1.0f` is `DCBlocker2`'s peak `|H|` (Q2: at Bessel `Q = 1/sqrt(3) <=
    1/sqrt(2)` the 2nd-order high-pass has **no resonant peak** — its magnitude rises monotonically
    from 0 at DC toward 1.0 at high frequency and never exceeds it, simpler than the single-pole
    blocker's `2/(1+R)` figure an earlier draft used), and the FR-032 tracking gain is `≤ 1` and so
    drops out of the bound.
  Both are **real `static_assert`s on named `constexpr` constants** in the header — not a comment —
  and `static_assert(kMaxPreClampMagnitude < kOutputClamp)` is the one that states, checkably, that
  the clamp is a backstop and not a shaping stage. SC-006 (a) is the measurement that holds this
  claim to the audio.
- **FR-053** — Rung 2 is the saturator's `tanh`, which bounds the chain output to `(-1, +1)` before
  the tracking multiply and the wet gain, regardless of drive.
- **FR-054** — **Rung 3 is a hard clamp on the sub contribution only** (`subChain * wetGain`, before
  the add into `in`), at `kOutputClamp = 4.0f` (`noise_organism.h:180`), on the one mono scalar
  sequence added to both channels (revised per the Clarifications session, Q6 — an earlier draft
  clamped the summed output, dry included, which would have counted a clamp engagement caused
  entirely by a loud dry input and would have clipped a dry signal above `+12 dBFS`, contradicting
  FR-055's own principle that a host's audio is not sanitised behind its back).
  `getClampEngagementCount()` returns a `std::uint32_t` count of samples at which it engaged
  (`resonance_drift_network.h:904`). The counter is cleared by `prepare()` and `reset()` and
  saturates rather than wraps. **The counter counts only finite excursions:** a sample increments it
  when the pre-clamp sub value is finite and outside `[-kOutputClamp, +kOutputClamp]`. A non-finite
  sample is FR-055's business, not the clamp's, and must not be recorded as a clamp engagement —
  otherwise the counter stops being a usable signal about level and SC-009 (c) cannot assert on it.
  Because the clamp is sub-only, the counter is now unconditionally a **pure statement about this
  component's own ladder**: no dry input, at any level, can move it.
- **FR-055** — Rung 4 is the non-finite trap. If the post-chain sub sample is non-finite (tested with
  `detail::isFinite`, FR-008), the engine substitutes `0.0f` for that sample and resets the
  `EnvelopeFollower`, `TwoPoleLP`, `SaturationProcessor` and `DCBlocker2`. **FR-062's tap receives the
  substituted (post-trap) value**, so `subTap` is finite at every sample under every input — which is
  what SC-009 (c) measures. The dry path is *not* sanitised: FR-050 is an add, so a non-finite input
  sample yields a non-finite output sample by contract, and SC-009 (c) is written against that
  contract rather than against a promise this component does not make. This rung is **defence in
  depth and is not reachable through the public API** — FR-009 rejects non-finite setter arguments,
  `SubOscillator::process` sanitises its own output (`:362-372`), and `TwoPoleLP`/`Biquad` reset on a
  non-finite input — so it is exercised through a declared, test-only fault-injection probe, the
  shipped `SeraphisEngine` pattern (SC-009). It exists because two of the composed primitives do not
  self-heal: `DCBlocker2::process` has no finiteness branch and propagates a NaN into its `y1_`/`y2_`
  state (`dc_blocker.h:328-341`; the same risk the earlier single-pole `DCBlocker` carried) and
  `EnvelopeFollower::processSample` "does NOT validate input" (`envelope_follower.h:163`); one
  non-finite sample poisons either for the life of the object, and `detail::flushDenormal` does not
  clear a NaN.

### FR-060 series — Control and read surface

- **FR-060** — Setters: `setFundamentalHz`, `setToneLevelDb`, `setToneWaveform`, `setToneBreathRate`,
  `setToneBreathDepth`, `setTrackingAmount`, `setTrackReferenceDb` (new, FR-035/Q4),
  `setFollowerAttackMs`, `setFollowerReleaseMs`, `setLowpassCutoffHz`, `setDriveDb`, `setWetGainDb`,
  `setSubToMainEnabled` (new, FR-064/Q8), `setSeed`. All `noexcept`, all allocation-free, all obeying
  FR-009.
- **FR-061** — Getters, each reporting the **applied** (clamped) value, not the request:
  `getSampleRate`, `getMaxBlockSamples`, `getFundamentalHz`, `getToneFrequencyHz(tone)`,
  `getToneLevelDb(tone)`, `getToneWaveform(tone)`, `getToneBreathRate(tone)`,
  `getToneBreathDepth(tone)`, `getToneBreathValue(tone)` — **the tone's last-computed raw
  `BreathingModulator::getCurrentValue()`, i.e. the bipolar `b_i` in `BreathingModulator`'s own
  `{-1, +1}` range (revised per Q7: the modulator's own depth stays at its library default of `1.0`,
  so this is no longer depth-scaled by the modulator; the engine's `depth_i` scaling happens only in
  FR-022's `breathGain_i`, which this getter does not report)** — the
  read surface SC-011 (b) and SC-014 (d) assert on, because the composite
  `getToneCurrentGain` cannot distinguish a breath difference from a level or gate difference,
  `getToneCurrentGain(tone)` (the FR-022 product `g_i`),
  `isToneDormant(tone)`, `isToneInfrasonicFloored(tone)`, `getTrackingAmount`, `getTrackedEnvelope`,
  `getTrackingGain`, `getTrackReferenceDb` (new, FR-035/Q4), `getFollowerAttackMs`,
  `getFollowerReleaseMs`, `getLowpassCutoffHz`, `getDriveDb`, `getWetGainDb`,
  `getSubToMainEnabled` (new, FR-064/Q8), `getSeed`, `getClampEngagementCount`, `getAllocatedBytes`.
  Out-of-range tone index returns a documented neutral (`0.0f`, `false`, `SubWaveform::Sine`).
- **FR-062** — **A second entry point, now a supported render output** (revised per the
  Clarifications session, Q8 — an earlier draft called this tap measurement-only),
  `void processBlockTapped(const float* inL, const float* inR, float* outL, float* outR, float* subTap, std::size_t numSamples) noexcept`,
  additionally writes the **post-chain, post-tracking-gain, pre-wet-gain, pre-clamp** mono sub
  signal — the `subChain` of FR-050's block diagram, *before* FR-054's clamp and independent of the
  FR-064 `subToMainEnabled` flag — into `subTap` when it is non-null, one value per sample.
  Post-tracking is load-bearing, not incidental: SC-002 measures the FR-032 law through this tap, and
  a tap taken before the tracking multiply would read a flat line at every body level and pass for a
  broken implementation. `subTap == nullptr` makes it identical to `processBlock`. Under FR-025
  dormancy the chain is skipped and the tap is written with zeros. Before `prepare()` the tap is
  written with zeros (FR-050). Because the tap is unconditional on `subToMainEnabled`, a caller can
  disable the sub's contribution to the main output (FR-064) while still consuming the full sub
  signal from `subTap` — this is the mechanism Phase 10 uses to route the sub around the Phase-9
  space engine.
- **FR-063** — The tapped path must produce a **bit-identical** main output to the untapped one for
  the same input and configuration (SC-012 (c)). The tap is a write, never a change of arithmetic.
- **FR-064** — **Sub-to-main routing flag (new, Q8).** `void setSubToMainEnabled(bool enabled) noexcept`,
  default `true`; `getSubToMainEnabled()` reports it. When `true`, `processBlock`/`processBlockTapped`
  behave exactly as FR-050 specifies (the sub is added into the main output). When `false`, the main
  output equals the dry input, bit-identical, on both entry points, **while `subTap` continues to
  report the full, non-zero `subChain`** (FR-062) — the flag routes the sub *out of the main output*,
  it does not silence the engine or change FR-025's dormancy bookkeeping (dormancy is defined purely
  by the FR-020 tone gains, independent of this flag). SC-022 is the criterion.

### FR-070 series — Determinism, footprint, budget, probe

- **FR-070** — `void setSeed(std::uint32_t seed) noexcept`. Seed 0 is legal:
  `deriveStreamSeed` (`core/random.h:102`) substitutes a guaranteed-non-zero derived value. Two
  instances prepared identically and given the same seed and the same setter sequence render
  identically within `render_fingerprint.h` tolerances (SC-011).
- **FR-071** — **Stage-cost probe and CPU budget.** A `[.perf]`-tagged probe measures, in
  nanoseconds per 512-sample block at 48 kHz (best-of-25 × 500 blocks after 400 warm-up blocks), each
  of: (a) the two `PhaseAccumulator`s alone; (b) one `SubOscillator` at `SubWaveform::Sine`; (c) one
  at `SubWaveform::Square`; (d) three tones summed; (e) the `EnvelopeFollower`; (f) the
  `TwoPoleLP`; (g) the `SaturationProcessor::processSample`; (h) the `DCBlocker2`; (i) the whole
  engine at defaults; (j) the whole engine with all three tones on `Square`; (k) the whole engine
  dormant (FR-025's skip); and (n) the whole engine with `setFundamentalHz(40.0f)`, i.e. the
  two-tone case the revised FR-016 backstop produces below 48 Hz (`Div4` at 10 Hz is floored,
  `Div2` at 20 Hz and `FifthBelow` at 26.67 Hz are awake), so the cost of an ordinary low setting is
  on the record next to the three-tone default at 55 Hz. It also measures the **placement arms**
  OQ-1 needs:
  (l) one instance, (m) eight instances, both at defaults. The probe itself **REQUIREs only that
  every figure is finite and strictly positive**; the gates live in SC-013, and the *transcription
  and ruling* obligation lives in FR-076.
  *** STOP-AND-SURFACE RULE (inherited verbatim from `resonance_drift_network_perf_test.cpp:57-64`)
  — NON-NEGOTIABLE *** No implementing agent may lower `kNumTones`, raise the budget, relax a
  threshold, or shrink a workload to make a figure fit. Reduce cost, never move the line. If the
  engine total exceeds SC-013's ceiling, the build stops and surfaces the measured per-stage table.
- **FR-072** — Seed derivation uses an **append-only** salt table with overlap `static_assert`s
  (`resonance_drift_network.h:929-947` pattern): `kSaltBreath = 0` (+ tone index, three entries),
  `kSaltNextFree = 8`. Each `BreathingModulator` gets
  `deriveStreamSeed(seed_, kSaltBreath + toneIndex)` followed by the mandatory per-lane `reset()`.
- **FR-073** — `getAllocatedBytes()` returns the true sum of the component's heap term: the
  `MinBlepTable`'s two polyphase vectors (`1024` floats each at the default `prepare(64, 8)`), the
  three `MinBlepTable::Residual` buffers (`length() = 16` floats each,
  `minblep_table.h:405`), and the `SaturationProcessor`'s `dryBuffer_` of `maxBlockSamples` floats
  (`saturation_processor.h:136`). The last of those is **dead weight the engine never reads** — it is
  declared here rather than hidden, because the alternative (preparing a shipped component with a
  false block size) is a trap for any later phase that reaches for its block entry point.
- **FR-074** — Zero allocation after `prepare()`. Every `processBlock` call, every setter and
  `reset()` allocate nothing (SC-010).
- **FR-075** — Block-partition invariance: rendering `N` samples in one call and in any partition of
  arbitrary chunk sizes produces the same output within `render_fingerprint.h` tolerances (SC-012).
  This is what FR-007's absolute control-phase residue exists to guarantee.
- **FR-076** — **The Phase-6 ruling on roadmap Open Question 4 is a deliverable of this phase, not of
  a later one.** The phase's compliance record must transcribe FR-071 arms **(k)** (dormant),
  **(l)** (one instance) and **(m)** (eight instances) as measured **ns per 512-sample block at
  48 kHz**, show the arithmetic that turns them into a per-voice and a global cost at the roadmap's
  voice counts (4, 6, 8 — roadmap Open Question 5), and record the resulting **ruling** on roadmap
  Open Question 4 (global post-voice-sum versus per-voice, roadmap lines 316–317, 541–542) together
  with the reasoning. The ruling itself is the user's, taken from that table (OQ-1); the phase is
  **not complete** until the table and the ruling are both in the compliance record. SC-018 is the
  criterion whose evidence is that transcription.
  This FR exists because the roadmap files Open Question 4 under "resolve in the relevant spec, not
  before" and this is that spec: a decision that lives only in prose has no gate, passes every
  check, and silently fails to be produced.

### FR-080 series — Shared components stay untouched

- **FR-080** — `sub_oscillator.h`, `minblep_table.h`, `envelope_follower.h`, `two_pole_lp.h`,
  `saturation_processor.h`, `dc_blocker.h`, `breathing_modulator.h`, `smoother.h`, `phase_utils.h`
  and `random.h` are **byte-unchanged** by this phase. Roadmap line 531 anticipated a `SubOscillator`
  extension; FR-012 removes the need for one. Their shipped test TUs — in particular
  `dsp/tests/unit/processors/sub_oscillator_test.cpp` — must stay green (SC-016).
- **FR-081** — The four new test TUs are registered by name in the **enumerated** `dsp_systems_tests`
  list (`dsp/tests/CMakeLists.txt`, list opens at `:324`), appended after the Phase-5 block that ends
  at `:449`, with the same comment header the Phase-3 and Phase-5 blocks carry mapping each TU to the
  criteria it covers. The non-finite TU is additionally registered in the `-fno-fast-math` block
  (`:542`); the `[.perf]` TU must stay **out** of that block, because `-fno-fast-math` would change
  the figures its baselines are pinned to.

## Success Criteria

Every criterion names the metric, the threshold and the test that measures it. Test names are
sketches; the plan fixes them.

**The isolated-sub fixture (SC-002 through SC-005 and SC-020 all use it, so it is stated once).**
Sample rate 48 kHz. The engine is prepared with `PrepareConfig{.maxBlockSamples = 512}` and then
configured: `setTrackingAmount(0.0f)` unless the criterion says otherwise — so `trackGain == 1`
exactly and the sub is present without needing a body to hold it up; `setWetGainDb(0.0f)`;
`setLowpassCutoffHz(kMaxLowpassHz)` (2000 Hz at 48 kHz); `setDriveDb(kMinDriveDb)`; **exactly one
tone enabled**, at `setToneLevelDb(-20.0f)`, with the other two at `kMinToneLevelDb` (exact zero
gain, FR-020) and therefore contributing nothing; `setToneBreathDepth(tone, 0.0f)` on the enabled
tone so the measured level is stationary. The input is **silent** on both channels. The measured
signal is `subTap` from `processBlockTapped`, discarding the first 2 s so every ramp has settled and
the 50 ms gates are at target. `-20 dBFS` is the tone level for a reason: FR-041's `tanh` is in
circuit even at drive 0, and its own relative third harmonic is ≈ `A²/24` — `4.2e-4` (0.042 %) at
`A = 0.1`, i.e. two orders below SC-004's ceiling, where the FR-020 default of `-6 dB`
(`A = 0.5`, ≈ 1.0 %) would consume half the budget and make the criterion a measurement of the
shaper rather than of the dividers.
**Before any spectral figure is accepted**, the analysed `subTap` RMS must be **above −60 dBFS**.
This gate is not decoration: a silent tone would otherwise report a peak of noise and a THD of zero
and pass every criterion below while measuring nothing.

- **SC-001 — The subs track the body, and do not boom on their own.** *(roadmap line 319)*
  `SubharmonicEngine_TrackingSuppressesFreeRunning`, in `subharmonic_engine_test.cpp`.
  With `trackingAmount = 1.0` (the default), all three tone levels at `kMaxToneLevelDb`, wet gain at
  `kMaxWetGainDb`, the fundamental at its default `55 Hz`, and a **silent** stereo input, the
  output over 60 s at 48 kHz never exceeds **−80 dBFS** peak.
  (b) The same configuration, run for 10 s silent and then woken by a step to a −12 dBFS 55 Hz
  body, reaches at least **50 % of its steady-state sub-band (`< 200 Hz`) RMS within 400 ms** of the
  step — steady state being the same quantity measured over the last second of a 10 s post-step
  render — and no sample exceeds `kOutputClamp`. The 50 %-in-400 ms figure is a floor on
  responsiveness, set below what `kDefaultFollowerAttackMs = 120.0f` predicts (≈ 96 % at 400 ms) so
  the criterion tests the tracking path rather than the follower's exact time constant.
  (c) After the body is removed, the sub falls below −80 dBFS within 5 s (release-bounded).
- **SC-002 — The tracking law is the one FR-032 specifies.** `SubharmonicEngine_TrackingLaw`,
  in `subharmonic_engine_spectral_test.cpp`. The isolated-sub fixture above, **except**:
  `trackingAmount = 1.0` (the quantity under test), all three tones at their FR-020 defaults with
  breath depths at 0, the fundamental at its default `55 Hz`, and the input a mono-identical
  **55 Hz sine body** held for 8 s per step, of which the last 4 s are measured. Sweep the body RMS
  over −60, −48, −36, −24, −18, −12, −6 dBFS. Measure the `subTap` RMS through
  `processBlockTapped`, which by FR-062 is post-tracking-gain and is therefore the point at which
  the law is observable at all.
  (a) Between −60 and −18 dBFS the sub RMS rises with **unity slope in dB**, within **±1.0 dB** at
  every point. (b) Between −18 and −6 dBFS the sub RMS is **flat within ±0.5 dB** (the
  `trackReferenceRms` clamp, at its default −18 dBFS). (c) With `trackingAmount = 0` and everything
  else identical, the sub RMS is flat within **±0.5 dB** across the whole sweep — free-running by
  design, and the criterion records that it is stable, not silent. (d) `getTrackingGain()` sampled
  at the end of each step agrees with `(1 - trackingAmount) + trackingAmount * envNorm` computed
  from `getTrackedEnvelope()` within `1e-4`, so the audio measurement and the read surface
  corroborate each other rather than either standing alone.
  (e) **The reference is load-bearing, not cosmetic (new, Q4/FR-035).** Repeating (a)/(b) with
  `setTrackReferenceDb(-30.0f)` moves the boundary between the rising region and the flat region
  from −18 dBFS to **−30 dBFS, within ±1.0 dB** — the −24 dBFS point, flat at the default reference,
  now falls in the rising region, and the −18 dBFS point, previously the top of the rising region,
  is now flat. This is the criterion that would fail if `setTrackReferenceDb` reached its getter but
  not `envNorm`'s denominator.
- **SC-003 — The dividers are at the right frequencies.**
  `SubharmonicEngine_DividerFrequencyAccuracy`, in `subharmonic_engine_spectral_test.cpp`.
  The isolated-sub fixture, `SubWaveform::Sine`, **one tone enabled at a time with a per-tone
  fundamental sweep chosen so the tone clears the FR-016 floor at every point** (a floored tone is
  silent, and a criterion measured on silence is not a criterion):
  | Tone | target | fundamentals swept | resulting tone frequencies |
  |---|---|---|---|
  | `Div2` | `f/2` | 55, 110, 220 Hz | 27.5, 55, 110 Hz |
  | `Div4` | `f/4` | 110, 220, 440 Hz | 27.5, 55, 110 Hz |
  | `FifthBelow` | `2f/3` | 55, 110, 220 Hz | 36.67, 73.33, 146.67 Hz |

  **Estimator.** A raw FFT peak bin cannot resolve these tolerances and the criterion does not
  pretend otherwise: a 65 536-point transform at 48 kHz has 0.7324 Hz bins, i.e. ±0.366 Hz of
  quantisation, where ±0.5 % of 27.5 Hz is ±0.1375 Hz and ±2 cents of 36.67 Hz is ±0.042 Hz. The
  measurement therefore uses the new helper `estimatePeakFrequencyHz(const float* x, size_t n,
  float sampleRate)` (A-4): a **262 144-point Hann-windowed** frame (0.183 Hz bins) followed by
  **parabolic interpolation of the log magnitude across the peak bin and its two neighbours**, the
  standard estimator for a Hann-windowed sinusoid, whose residual bias on a stationary tone is well
  under 0.01 bin (≈ 0.002 Hz) — two orders below the tightest tolerance asserted.
  (a) The estimated frequency is within **±0.5 %** of the tone's target, and `getToneFrequencyHz(tone)`
  agrees with the estimate within the same tolerance. (b) The `FifthBelow` tone's estimated frequency
  is within **±2 cents** of `f * 2/3` at every swept fundamental — this is the criterion that proves
  the `4f/3`-master construction of FR-012, not merely that *some* low tone appeared.
  (c) The helper is validated before use, in the same TU, against synthetic sines at 27.5, 36.67 and
  110 Hz: the estimate must land within 0.005 Hz of truth, so a helper bug fails as a helper bug
  rather than as an engine defect.
- **SC-004 — Harmonic purity of the dividers.** *(roadmap line 319–320, "THD measured")*
  `SubharmonicEngine_DividerTHD`, in `subharmonic_engine_spectral_test.cpp`. The isolated-sub
  fixture, `SubWaveform::Sine`, the same **per-tone** fundamental sweeps as SC-003 — for the same
  reason: at `f = 55` the `Div4` tone is floored to silence by FR-016, and `calculateTHD` would
  report `0.0f` on it and pass.
  **Measurement.** Not `signal_metrics.h:111 calculateTHD` — its 8192-point cap gives 5.86 Hz bins
  at 48 kHz and its ±2-bin harmonic windows overlap at these frequencies, so what it returns is Hann
  leakage (first sidelobe −31 dB ≈ 2.8 %, i.e. above the ceiling this criterion asserts) rather than
  divider distortion. SC-004 uses the new helper
  `measureLowFrequencyThdPercent(const float* x, size_t n, float fundamentalHz, float sampleRate,
  int maxHarmonic = 10)` (A-4): a **262 144-point Hann-windowed** transform (0.183 Hz bins), power
  summed over the **peak bin ±2** (the Hann main lobe) at the fundamental and at each harmonic, with
  `THD = sqrt(Σ_{k≥2} P_k) / sqrt(P_1) * 100`. At the lowest tone measured (27.5 Hz) the harmonic
  spacing is 150 bins, so the main lobes never touch and the Hann sidelobe envelope at that distance
  is below **−100 dB** (0.001 %) — a **leakage floor more than 60 dB below the 2 % ceiling**, which
  is the property `calculateTHD` lacks and the reason the helper exists. The helper returns a
  negative sentinel if the fundamental's summed power is below −60 dBFS, so a silent tone fails.
  (a) THD is **≤ 2.0 %** for every tone at every fundamental in its sweep, and the measured values
  are transcribed into the compliance record. The 2 % ceiling is derived, not guessed, and the
  derivation now names every term: the dominant one is the once-per-sub-period phase reset at
  `sub_oscillator.h:274-278`, whose discontinuity is bounded by `masterInc / octaveFactor` cycles —
  at `f = 220 Hz`, 48 kHz, `Div2` that is `2.3e-3` cycles, an amplitude step of ≈ `1.4e-2`,
  ≈ −37 dB relative (≈ 1.4 %), spread across harmonics; plus FR-041's always-in-circuit `tanh` at
  ≈ `A²/24` = 0.042 % for the fixture's `A = 0.1`; plus the helper's ≤ 0.001 % leakage floor. If
  measurement disagrees, FR-071's stop-and-surface rule applies: surface the number, do not relax
  the line.
  (b) For **each tone separately**, THD is **monotonically non-increasing** as that tone's own
  fundamental sweep descends (220 → 110 → 55 Hz for `Div2` and `FifthBelow`; 440 → 220 → 110 Hz for
  `Div4`) — the signature of the phase-reset mechanism, and the check that distinguishes it from a
  coefficient bug. The trend is asserted within each tone's sweep, never across tones, because the
  three tones sit at different divider ratios and are not comparable point-for-point.
- **SC-005 — The Square path produces no inharmonic content.** `SubharmonicEngine_SquareSpectrum`,
  in `subharmonic_engine_spectral_test.cpp`. The isolated-sub fixture with **`Div2` alone** enabled
  (single tone, named explicitly: with three tones sounding, 55, 27.5 and 73.33 Hz have no common
  integer fundamental and the criterion would fail on a correct implementation), `SubWaveform::Square`,
  `f = 110 Hz` so the sub fundamental is **55 Hz**, drive at `kMinDriveDb`, low-pass at
  `kMaxLowpassHz`. Analysis: a **262 144-point Hann-windowed** transform at 48 kHz (0.183 Hz bins),
  the same frame SC-003 and SC-004 use.
  (a) Every spectral peak more than **−40 dB** relative to the 55 Hz sub fundamental lies within
  **±1 bin** of an integer multiple of 55 Hz. (b) The Square-path sub RMS is **above −40 dBFS** —
  this is the assertion that proves the minBLEP path is actually engaged and the shared
  `MinBlepTable` is prepared, because a null or unprepared table makes the tone **silent** rather
  than un-BLEPped (`sub_oscillator.h:144-147, 224-226`). (c) The `tanh` of FR-041 is in circuit even
  at drive 0, but on a **single** tone it generates only odd harmonics of that tone, which are
  integer multiples and so are admitted by (a) by construction; the criterion notes this rather than
  attributing the harmonics to the divider.
- **SC-006 — True-peak safety with subs at max.** *(roadmap line 320, "headroom test")*
  `SubharmonicEngine_TruePeakHeadroom`, in `subharmonic_engine_test.cpp`, using the new
  `measureTruePeakDb` helper (4× oversampled, the `TruePeakLimiter::processChunk` basis,
  `true_peak_limiter.h:125-146`). Input: a −6 dBFS 55 Hz body plus a −12 dBFS pink bed. Fundamental
  at its default `55 Hz` (all three tones awake, FR-016), all tone levels at `kMaxToneLevelDb`,
  wet gain at `kMaxWetGainDb`, drive at `kMaxDriveDb`, `trackingAmount` at its default 1.0.
  (a) `getClampEngagementCount() == 0` **and** the engine's output true peak is **≤ +9.5 dBTP**, with
  the measured value transcribed. The threshold is the criterion's teeth, and `≤ kOutputClamp
  (+12.04 dBFS)` deliberately is not: FR-054's clamp makes that outcome nearly unfalsifiable, and as
  a *true-peak* figure it would not even be guaranteed, since inter-sample peaks of a clipped
  waveform exceed the clip level. `+9.5 dBTP` is set from FR-052's `kMaxPreClampMagnitude ≈ 2.00`
  (+6.0 dBFS) plus the ≈ 1.0 peak of this input (+0.0 dBFS) — a worst case of ≈ +9.5 dBFS if sub and
  body peaked coherently, which they do not — so a pass means the ladder behaved as FR-052 claims
  and a regression that pushes the sub past the saturator's bound fails. The `== 0` clamp count is
  the direct statement that the clamp is a backstop and not a shaping stage.
  (b) Passing that output through a default
  `TruePeakLimiter` (ceiling `kDefaultCeilingDb = -1.0f`, `true_peak_limiter.h:46`) yields a measured
  true peak **≤ −0.9 dBTP**. (c) At **default** tone levels and default wet gain, the limiter's
  minimum gain over the render stays **above −6 dB** — i.e. the shipped defaults do not force
  pathological limiting downstream. (c) is the criterion that makes the defaults a claim rather than
  a guess.
- **SC-007 — Mono compatibility.** *(roadmap line 320–321, "`midside` correlation")*
  `SubharmonicEngine_MonoCompatibility`, in `subharmonic_engine_test.cpp`, using the new
  pointer/length correlation overload. With a **decorrelated** stereo input (independent pink noise
  per channel plus a common 55 Hz body) and subs at default levels:
  (a) The **side energy of the sub contribution** is ≤ **−100 dB** relative to its mid energy, where
  the sub contribution is `d = out - in` per channel, `mid = 0.5*(dL + dR)` and `side = 0.5*(dL - dR)`.
  An L/R *correlation* of `d` is **not** used here and the earlier draft's `≥ 0.9999` is withdrawn:
  FR-043 makes `d` the same scalar sequence on both channels, so its correlation is exactly 1.0 for
  any implementation that satisfies FR-043 including a broken one, and the subclause could not fail.
  The mid/side form can fail — it catches an accidental per-channel chain state, a per-channel gain
  ramp, or an FR-054 clamp that engages on one channel and not the other. (a2) The full-output L/R
  correlation with the subs active is **not lower** than the correlation of the same render with the
  subs muted (`setWetGainDb(kMinWetGainDb)`), within `1e-6`: adding a common mono signal can only
  raise correlation, so any drop is a per-channel divergence.
  (b) On the **sub contribution** — stated on `d`, exactly as (a) is, because on the full output the
  decorrelated pink bed below 200 Hz sums to roughly half the per-channel mean power and the figure
  would land near 50–70 % for reasons that have nothing to do with the subs — the mono sum
  `0.5*(dL + dR)` retains **≥ 99 %** of the `< 200 Hz` energy of `d` (measured with
  `spectral_analysis.h:207 sumBinPower`) relative to the mean of `dL` and `dR` — i.e. no
  cancellation. (c) The DC offset of each output channel over a 30 s render is **≤ 1e-4** in
  magnitude, which is what FR-042's blocker and D-1's symmetric saturator exist to deliver.
- **SC-008 — No clicks anywhere on the control surface.** `SubharmonicEngine_ClickFreedom`, in
  `subharmonic_engine_test.cpp`, using `artifact_detection.h:38,72`. Over a 60 s render with a steady
  −12 dBFS 55 Hz body, step each of: every tone level from `kMinToneLevelDb` to `kMaxToneLevelDb`
  and back; the wet gain across the same range; `trackingAmount` 0 ↔ 1; the low-pass cutoff across
  its full range; and **the fundamental across the FR-016 backstop, on sweeps that actually cross
  it** (frequencies revised per the Clarifications session, Q2: `kMinToneHz` moved from 16 Hz to
  12 Hz, so the earlier draft's crossing points no longer cross anything — at the new threshold,
  55 Hz→`Div4` = 13.75 Hz is already **above** 12 Hz and does not float):
  - `50 Hz ↔ 44 Hz`, which takes `Div4` from 12.5 Hz (awake) to 11 Hz (floored) and back, crossing
    the `f = 48 Hz` boundary; `Div2` (25 → 22 Hz) and `FifthBelow` (33.3 → 29.3 Hz) stay awake
    throughout.
  - `26 Hz ↔ 20 Hz`, which takes `Div2` from 13 Hz (awake) to 10 Hz (floored) and back, crossing the
    `f = 24 Hz` boundary, with `FifthBelow` staying awake (17.3 → 13.3 Hz) so the render is never
    silent.

  Each floor arm additionally asserts that `isToneInfrasonicFloored(tone)` **actually flipped** in
  both directions during the arm. Without that assertion a future change to `kMinToneHz` could
  silently re-void the test, which is exactly how an earlier draft's arm became vacuous.
  No click is detected. **Excluded by name**, because they are declared
  stepped and are rare control events, not audio-rate moves: `setToneWaveform` and `prepare()`.
- **SC-009 — The non-finite trap works and is unreachable.** `SubharmonicEngine_NonFinite`, the sole
  content of `subharmonic_engine_nonfinite_test.cpp` (the `-fno-fast-math` TU; non-finite values
  built from bit patterns through a volatile sink, never `std::numeric_limits`).
  (a) Every setter rejects NaN and ±Inf with the previous value standing, verified through the
  getter. (b) A non-finite sample injected through the declared test-only probe produces a finite
  output within one sample and leaves the engine producing correct audio thereafter — the follower,
  low-pass, saturator and blocker are all recovered.
  (c) **A non-finite input buffer cannot poison the engine.** A 30 s adversarial sweep over the whole
  parameter space is rendered through `processBlockTapped` with a *non-finite* input buffer for its
  middle 10 s and a finite one either side. Assertions: (c1) `subTap` is **finite at every sample of
  the whole render**, including the non-finite window — FR-055 substitutes `0.0f` into the tap, so
  this is the criterion that actually exercises the trap from the outside; (c2) from the **first**
  sample after the input returns to finite, the main output is finite again, and over the final 10 s
  the output matches a reference render (identical setter sequence, finite input throughout) within
  `render_fingerprint.h` tolerances — i.e. no state stayed poisoned; (c3)
  `getClampEngagementCount() == 0` across the whole render, which is meaningful only because FR-054
  counts finite excursions only.
  What (c) deliberately does **not** assert is a finite *output* during the non-finite window. FR-050
  is a straight add (`out = in + subChain * wetGain`) and FR-055 sanitises only the sub, so a
  non-finite input yields a non-finite output **by contract**; asserting otherwise would fail a
  correct implementation and would silently demand a dry-path sanitiser that no FR specifies and that
  this component deliberately does not have (sanitising a host's audio behind its back hides the
  host's bug). The earlier draft's "`getClampEngagementCount()` is finite" clause is withdrawn as
  vacuous — the counter is a `std::uint32_t`.
- **SC-010 — Zero allocation after prepare.** `SubharmonicEngine_NoAllocation`, using
  `allocation_detector.h:48,111`. Inside an `AllocationScope`: 10 000 `processBlock` calls of mixed
  sizes, every setter exercised, `reset()`, and `processBlockTapped` — **zero** allocations.
  `getAllocatedBytes()` is > 0, identical across two `prepare()` calls with the same `PrepareConfig`,
  and unchanged by `reset()`.
- **SC-011 — Seed determinism.** `SubharmonicEngine_Determinism`, using `render_fingerprint.h:122`.
  Two instances prepared identically, given the same seed and the same setter sequence, render
  identically over 120 s within `kSampleTolerance = 5.0e-4f` / `kMetricTolerance = 2.5e-4`.
  (b) Two instances with **different** seeds diverge: the fingerprint comparison fails, **and** at
  `t = 60 s` at least **two of the three** tones' `getToneBreathValue(tone)` (FR-061) differ between
  the two instances by more than **0.01** in absolute value. The earlier draft asserted on "breath
  phases", which no getter exposes, against "the tolerance", which named nothing —
  `render_fingerprint.h`'s `kSampleTolerance` is a render-sample bound, not a gain-difference bound.
  `getToneBreathValue` is asserted rather than `getToneCurrentGain` because the latter is the
  composite `levelGain * bracket * gate` and cannot distinguish a breath difference from a level or
  gate difference. **No bit-exact float golden is used anywhere** (roadmap line 526).
- **SC-012 — Block-partition and aliasing invariance.** `SubharmonicEngine_BlockInvariance`.
  (a) A 30 s render in 512-sample blocks and the same render in a pseudo-random partition of chunk
  sizes in `[1, 1024]` agree within `render_fingerprint.h` tolerances. (b) In-place rendering
  (`outL == inL`, `outR == inR`) agrees with out-of-place within the same tolerances. (c) The
  `processBlockTapped` main output is **bit-identical** to the `processBlock` output (FR-063).
- **SC-013 — CPU budget.** *(roadmap line 321, "CPU ≤ 0.5%")* `SubharmonicEngine_CpuBudget`,
  `[.perf]`, in `subharmonic_engine_perf_test.cpp`, run alone via
  `node tools/run-cpu-tests.js dsp_systems_tests`. Measurement basis: **ns per 512-sample block at
  48 kHz**, best-of-25 × 500 blocks after 400 warm-up blocks. One block period is 10 666 667 ns, so
  the ceiling is **53 333 ns/block**.
  **Fixture, stated so it is reachable:** one instance at defaults — the FR-013 default fundamental
  of **55 Hz**, at which **all three tones clear the FR-016 backstop and are genuinely awake**
  (`Div4 = 13.75 Hz > kMinToneHz = 12 Hz`, per the Clarifications session, Q2) — with a steady
  −12 dBFS 55 Hz body present so the tracking path is live. (An earlier draft, under a 16 Hz hard
  mute, moved the default to 82.5 Hz because `Div4` was floored and dormant at 55 Hz, making
  "defaults, all three tones awake" unreachable at 55 Hz; Q2's filter-plus-backstop redesign lowers
  the backstop to 12 Hz, so 55 Hz clears it and the default reverts, per FR-013.)
  (a) The default arm is **≤ 53 333 ns/block**. The percent figure is reported, never asserted. The
  baseline is carried as a checked-in `static_assert` so the absolute ceiling is evaluated on every
  CI leg. Because the roadmap's per-voice/global split (line 516) puts Phase 6 in the per-voice band
  while roadmap line 316 leaves the placement open, **one instance ≤ 53 333 ns/block satisfies the
  budget in both placements** — which is exactly why OQ-1 can be answered from measurement without
  re-specifying.
  (b) The all-`Square` arm is **gated at the same 53 333 ns/block ceiling**, not merely reported.
  FR-015 makes `Square` reachable on all three tones, so it is a shipped configuration, and roadmap
  line 321's budget is unqualified; exempting a shipped configuration would leave FR-071's
  stop-and-surface rule with nothing to trigger against. If the arm misses, that rule applies and the
  *waveform option* is reconsidered — never the budget.
  (c) The dormant arm (FR-025) is at least **15 % cheaper** than the awake arm, and the absolute
  saving in ns is transcribed. The margin is not decoration: the perf idiom this criterion inherits
  records ~14 % session-to-session drift and whole-trial E-core migration as its dominant noise terms
  (`resonance_drift_network_perf_test.cpp:78-90`), so a bare "strictly cheaper" between two wall-clock
  figures is a coin flip. If the measured saving is real but below 15 %, FR-071's stop-and-surface
  rule applies — surface it and reconsider what FR-025 skips; do not lower the margin.
  (d) The two-tone arm (FR-071 (n), `setFundamentalHz(40.0f)`) is measured and reported, so the cost
  of an ordinary low setting is on the record; it is not separately gated, being strictly cheaper
  than (a) by construction.
- **SC-014 — Dormancy behaves as FR-025/FR-026 specify.** `SubharmonicEngine_Dormancy`.
  (a) With every tone at `kMinToneLevelDb` and the ramps settled, the output is **bit-identical** to
  the input over 10 s, on both channels, for a decorrelated stereo input.
  (b) `isToneDormant(tone)` is true for all three, and becomes false on the first sample after a
  level write above the floor.
  (c) **The sleep-edge clear (FR-026), with the precondition that makes it observable.** Sequence:
  (1) fundamental at its default 55 Hz, `trackingAmount = 0`, all three tones at their FR-020
  defaults, wet gain 0 dB, and a −12 dBFS 55 Hz body rendered for **≥ 2 s** — this is the step the
  earlier draft omitted, and without it the chain is never charged, so there is no stale tail for
  FR-026 to clear and nothing for the criterion to detect; (2) all three tone levels driven to
  `kMinToneLevelDb` and held **dormant for 10 s** with the body removed; (3) all three levels
  restored, waking into a **silent input**, with `trackingAmount` still **0** so a stale tail would
  be at full level and plainly audible. Assertion: the output is **≤ −80 dBFS for the first 500 ms**
  after the wake. `trackingAmount = 0` is load-bearing: at the FR-033 default of 1.0 with a silent
  input, FR-032 gives `envNorm = 0` → `trackGain = 0` → the sub is zero for the whole window whether
  or not FR-026's resets happened, and the subclause would pass with FR-026 deleted.
  (c2) **Mutation check**, run once during implementation and recorded in the compliance note: with
  the FR-026 resets removed, (c) must **fail**. A sleep-edge criterion that passes both with and
  without the mechanism it names is not a criterion.
  (d) **Generators advance while dormant (FR-025), measured differentially.** Two instances are
  prepared identically with the same seed and the same setter sequence; instance A is made dormant
  for **37 s** (deliberately not a whole multiple of any FR-021 default breath period — 27 s, 43 s,
  71 s — so the breath value at the wake is guaranteed to differ from the value at the sleep edge)
  while instance B is never made dormant. Assertions: (d1) immediately after A's wake, the two
  instances' `getToneBreathValue(tone)` agree within **1e-3** for all three tones, i.e. A's breathers
  really kept running; (d2) A's own `getToneBreathValue(tone)` at the wake differs from its value at
  the sleep edge by more than **0.05** for at least two of the three tones, i.e. the test is not
  passing on a frozen value that happens to match; (d3) both master `PhaseAccumulator`s advanced,
  shown by A and B agreeing within `render_fingerprint.h` tolerances on a 5 s render taken after the
  wake with all tones restored. The earlier draft's "consistent with elapsed time" named no
  reference, no threshold and no method, and could not be evaluated.
- **SC-015 — Lints and portability.** `tools/lint-layers.js`, `tools/lint-odr.js`,
  `tools/lint-nonfinite-symbols.js`, `tools/lint-float-bit-goldens.js` and
  `node tools/check-portability.js` all pass on the new header and the four new TUs. No `std::isnan`
  / `std::isinf` / `std::isfinite`. No narrowing in any brace init. No SIMD is introduced, so the
  aligned-load lint is vacuous but must still pass.
- **SC-016 — Shared components stay green and unchanged.** `git diff --stat` over the ten headers
  named in FR-080 is empty, and `dsp_processors_tests`, `dsp_primitives_tests` and `dsp_core_tests`
  pass unchanged — in particular `sub_oscillator_test.cpp`. Seraphis's suites
  (`seraphis_*` cases inside `dsp_systems_tests`) also pass, per roadmap lines 530–532.
- **SC-017 — Long-render stationarity, at defaults and at worst case.**
  `SubharmonicEngine_LongRenderStationarity`, `[long]`, in `subharmonic_engine_spectral_test.cpp`.
  **Two arms**, both 10 minutes at 48 kHz, because the roadmap's cross-cutting rule (lines 512–514)
  asks for a **worst-case** soak and Key Design Decision 3 (lines 93–95) for the overnight
  bounded/not-dead rule — the default arm alone is the reference, not the worst case.
  - **(A) Default arm.** Steady −12 dBFS 55 Hz body, every control at its default (fundamental
    55 Hz, so all three tones are awake — see SC-013's fixture note).
  - **(B) Worst-case arm.** The same 10-minute render with the free-running boom the roadmap's own
    Phase-6 criterion (line 319) is aimed at, assembled from the reachable extremes of every control
    the spec exposes: `trackingAmount = 0` (FR-033, free-running), all three tone levels at
    `kMaxToneLevelDb` (FR-020), drive at `kMaxDriveDb` (FR-041), wet gain at `kMaxWetGainDb`
    (FR-051), all three breath depths at 1.0 (FR-021), low-pass at `kMaxLowpassHz` (FR-040), and the
    same body. SC-002 (c) only sweeps this configuration briefly; this is the arm that soaks it.

  Both arms assert, on their own render: (a) the per-minute sub-band RMS (`< 200 Hz`) varies by
  **≤ 3 dB** peak-to-peak across the ten minutes — the breathing is audible motion, not level creep;
  (b) the linear trend of that RMS over the ten minutes has magnitude **≤ 0.3 dB/minute**; (c) **no
  sample is non-finite** at any point; (d) `getClampEngagementCount()` is **0** on arm (A) and is
  **transcribed** on arm (B) — arm (B) sits at FR-052's `kMaxPreClampMagnitude`, so a non-zero count
  there is information about the ladder rather than a failure, but an unreported one would hide it.
  Both arms are tagged `[long]`. Together they are the roadmap's cross-cutting boundedness gate
  (lines 512–514) in the form this feed-forward component admits.
- **SC-018 — The Open Question 4 ruling exists, with the numbers behind it.** *(roadmap lines
  316–317, 541–542; FR-076)* Not an executable test: the criterion's evidence is the **compliance
  record** for this phase, which must contain (a) FR-071 arms **(k)**, **(l)** and **(m)** transcribed
  as measured ns per 512-sample block at 48 kHz, taken from a `node tools/run-cpu-tests.js
  dsp_systems_tests` run in isolation; (b) the arithmetic projecting those figures onto the global
  placement (one instance) and the per-voice placement at 4, 6 and 8 voices, expressed as a
  percentage of the 10 666 667 ns block period and set beside the per-voice envelope the roadmap
  allows (line 92) net of what phases 2, 3 and 5 have already spent; (c) the **recorded ruling** on
  roadmap Open Question 4 and the reasoning for it. The phase is not complete without (c). This
  criterion exists because the deliverable is a *decision*, and a decision with no criterion is a
  decision that can silently fail to be produced.
- **SC-019 — Sample-rate and re-prepare invariance.** `SubharmonicEngine_RateAndReprepare`, in
  `subharmonic_engine_test.cpp`. These three behaviours were previously stated only in Edge Cases
  prose, which carries no criterion id and appears in no traceability row; a downstream stage
  consuming the FR/SC lists could ship without them.
  (a) `prepare(96000.0, …)` after `prepare(44100.0, …)` on the same object, with the same setter
  sequence re-pushed after each: `getToneFrequencyHz(tone)` is unchanged in Hz for all three tones
  (within `1e-3` Hz); the FR-013 fundamental ceiling and the FR-040 low-pass ceiling both move with
  the rate, verified through `getFundamentalHz()` and `getLowpassCutoffHz()` after writing a value
  above each ceiling; and a 10 s render at each rate has sub-band (`< 200 Hz`) RMS agreeing within
  **1 dB**.
  (b) `prepare()` called twice on a fully configured object leaves **every** getter in FR-061 equal
  to a freshly prepared instance's — the FR-005 restore-every-default promise, asserted getter by
  getter rather than assumed.
  (c) `prepare(4000.0, …)` reports `getSampleRate() == 8000.0` (FR-006), and every clamp range
  derived from the rate is well-ordered afterwards (no inverted `std::clamp` bounds), demonstrated by
  writing both ends of the FR-013 and FR-040 ranges and reading them back.
- **SC-020 — The infrasonic handling (filter + backstop) is real and the backstop is ramped.**
  `SubharmonicEngine_InfrasonicFloor`, in `subharmonic_engine_test.cpp`. FR-016 is this spec's own
  addition (D-5) and, before this criterion, nothing measured it: SC-003 and SC-004 collided with it
  rather than testing it, an earlier draft's SC-008 floor arm did not cross it, and a build that
  ignored `kMinToneHz` or `kInfrasonicFilterHz` entirely would have passed every criterion except by
  accident. Revised per the Clarifications session (Q2): `kMinToneHz` moved from 16 Hz to 12 Hz and
  the stage-3 filter moved from a 10 Hz single-pole `DCBlocker` to an 18 Hz `DCBlocker2`.
  (a) With `f = 40 Hz` (so `Div4` is 10 Hz, below the `kMinToneHz = 12 Hz` backstop), `Div4` alone
  enabled at `kMaxToneLevelDb`, `trackingAmount = 0`, wet gain 0 dB: the isolated sub (`subTap`) is
  **≤ −80 dBFS** over a 10 s render and `isToneInfrasonicFloored(Div4)` is **true**.
  (b) With `f = 55 Hz` (the FR-013 default, so `Div4` is 13.75 Hz — above the backstop but still
  inside the 18 Hz filter's stop-band) and everything else identical: the isolated sub RMS is
  **≥ −20 dBFS** (the filter's ≈ −7.5 dB attenuation of a `+6 dB`-level tone leaves it well above
  this floor) and `isToneInfrasonicFloored(Div4)` is **false** — this is the pair that proves the
  shipped default is genuinely "attenuated, not silent, not hard-muted".
  (c) Stepping between the two mid-render (`f = 55 Hz ↔ 40 Hz`), in both directions, is
  **click-free** (`artifact_detection.h:38,72`) and the transition occupies the `gate_i` ramp of
  FR-022/Q1: `getToneCurrentGain(Div4)` reaches its new target within **52 ms of the triggering
  write** (the `kGainRampMs = 50.0f` fade plus at most one 64-sample control-step retarget latency,
  `≤ 1.333 ms` at 48 kHz per Q5, plus 1 ms measurement tolerance) and is monotonic once the ramp
  begins moving.
  (d) The per-tone thresholds FR-016 states are checked directly through the read surface:
  `isToneInfrasonicFloored` is true for `Div4` below `f = 48 Hz` and false at or above it, for
  `Div2` below `f = 24 Hz`, and for `FifthBelow` below `f = 18 Hz`, each probed one Hz either side of
  its boundary.
- **SC-021 — The default sub-to-body level ratio is pinned (new, Q3/FR-020).**
  `SubharmonicEngine_DefaultSubToBodyRatio`, in `subharmonic_engine_test.cpp`. The engine is prepared
  and left entirely at its shipped defaults (fundamental 55 Hz, FR-020 tone levels
  `-18 / -24 / -30 dB`, `kDefaultWetGainDb`, `kDefaultDriveDb`, `kDefaultTrackingAmount = 1.0`,
  `kDefaultTrackReferenceDb`). The input is a steady −12 dBFS 55 Hz sine on both channels, rendered
  for 10 s with the first 2 s discarded so every ramp has settled. The body RMS (the input) and the
  isolated sub RMS (`subTap` via `processBlockTapped`) are each measured over the final 5 s, and the
  ratio `subRmsDb - bodyRmsDb` is **transcribed into the compliance record**.
  (a) The ratio is between **−12 dB and −6 dB** — the sub sits 6–12 dB under the body, centred on
  the ≈ −9 dB Q3 target — so a regression cannot silently reintroduce a sub-dominant default (an
  earlier draft's `-6 / -12 / -18` defaults measured the sub *louder* than the body). (b) The same
  measurement repeated with `setToneLevelDb` restored to the earlier draft's `-6 / -12 / -18 dB` is
  **recorded as a mutation check**, expected to land above `0 dB` (sub louder than body), confirming
  the criterion actually distinguishes the two default sets rather than passing on both.
- **SC-022 — Sub-to-main routing (new, Q8/FR-064).** `SubharmonicEngine_SubToMainRouting`, in
  `subharmonic_engine_test.cpp`. A body and all three tones at their defaults, rendered once with
  `setSubToMainEnabled(true)` (the default) and once with `setSubToMainEnabled(false)`, same seed and
  setter sequence otherwise.
  (a) With the flag `true`, the main output (`processBlock` and `processBlockTapped`) is
  bit-identical to a render of the same configuration taken before FR-064 existed (i.e. identical to
  FR-050's formula with `subToMainEnabled` omitted) — the flag's default is a no-op, not a behaviour
  change, for every existing criterion that does not set it explicitly.
  (b) With the flag `false`: the main output is **bit-identical to the dry input** on both channels
  and both entry points, while `subTap` from `processBlockTapped` is **bit-identical to the `subTap`
  produced with the flag `true`** — i.e. disabling the flag changes only what reaches `out`, never
  what reaches `subTap`, which is the entire point of promoting the tap (Q8). (c) Toggling the flag
  mid-render is **click-free** (`artifact_detection.h:38,72`) — the transition is a plain conditional
  add, not a ramp, so this asserts the toggle itself introduces no discontinuity beyond what the
  FR-022/FR-051 ramps already smooth. (d) `isToneDormant`, `isToneInfrasonicFloored` and
  `getClampEngagementCount()` are **unaffected** by the flag's state (dormancy and the clamp are
  defined on `subChain`/`clampedSub`, computed identically regardless of routing).

## Edge Cases

- **`processBlock` before `prepare()`** — every pointer valid, `numSamples > 0`. Contract: the
  output is the input, unmodified, nothing is written beyond `numSamples`, and `subTap` (if non-null)
  is zeroed. This is **normative in FR-050**, not merely an edge-case note. The engine is not
  prepared, so `SubOscillator::process` would return `0.0f` anyway (`sub_oscillator.h:224-226`); the
  explicit guard makes the behaviour a promise rather than a coincidence, which matters because
  FR-003 no longer claims construction reproduces a prepared object's state (it cannot — `prepare()`
  is the only method that prepares the `MinBlepTable`).
- **`numSamples == 0`, or any null pointer** — silent no-op, no state advance. Tested for all six
  pointer positions on the tapped entry point.
- **`prepare()` twice on a live object** — every default is restored (FR-005). **SC-019 (b)** asserts
  it getter by getter against a freshly prepared instance.
- **Sample-rate change** — `prepare(96000.0, …)` after `prepare(44100.0, …)`: tone frequencies are
  unchanged in Hz, the FR-013 fundamental clamp ceiling moves with the rate, the FR-040 low-pass
  ceiling moves with the rate, and a 10 s render at each rate has sub-band RMS agreeing within
  **1 dB**. The `MinBlepTable` is rate-independent (its `prepare` takes no rate) and is re-prepared
  anyway by FR-004's ordering. **Gated by SC-019 (a)** — this behaviour was previously prose only,
  with no criterion and no traceability row.
- **Sample rate below `kMinUsableSampleRate`** — `prepare(4000.0, …)` is floored to 8000.0 and
  `getSampleRate()` reports 8000.0. Nothing downstream sees an inverted clamp range (FR-006).
  **Gated by SC-019 (c)**.
- **Fundamental at both clamp ends** — `setFundamentalHz(0.0f)` clamps to `kMinFundamentalHz = 8.0`,
  at which **all three tones are below `kMinToneHz = 12 Hz` and are floored** by the backstop
  (FR-016; the per-tone thresholds are 48 / 24 / 18 Hz and 8 Hz is below all three); the engine is
  therefore silent-but-correct and `isToneInfrasonicFloored` is true for all three — **asserted by
  SC-020 (d)**. `setFundamentalHz(20000.0f)` clamps to `min(4186.0, 0.3 * fs)`; at 8 kHz that is
  2400 Hz, and the `4f/3` master increment is `0.4`, inside the accumulator's contract.
- **Fundamental swept across the FR-016 backstop during a render** — covered by **SC-008** (on
  sweeps that actually cross it: `50 ↔ 44 Hz` for `Div4`, `26 ↔ 20 Hz` for `Div2`, each asserting
  that `isToneInfrasonicFloored` flipped) and by **SC-020 (c)** for the ramp shape. The `gate_i` ramp
  moves over 50 ms in both directions once retargeted (Q1), retargeted within one 64-sample control
  step of the crossing (Q5); the tone's *oscillator* keeps running throughout, so there is no phase
  discontinuity to mask (FR-025).
- **All three tones at `kMinToneLevelDb`, wet gain at `kMaxWetGainDb`** — dormancy still engages
  (FR-025 keys on the tone gates, not the wet gain) and the output is bit-identical to the input.
- **Wet gain at `kMinWetGainDb` with tones awake** — the chain still runs (the tones are not dormant)
  but contributes exactly zero; the output is bit-identical to the input. The asymmetry with the
  previous case is deliberate and is what FR-025's "generators keep running" means in practice; both
  arms are asserted by SC-014 (a).
- **Tracking at 0 with a silent input** — the subs free-run at full level. This is **not** a defect:
  FR-033 makes it a reachable patch (in-scope argument: D-10), SC-002 (c) pins its behaviour and
  SC-017 (B) soaks it for ten minutes at every other control's worst-case setting. The default is
  1.0, where the roadmap's "no free-running boom" holds.
- **Non-finite input buffer** — SC-009 (c). The **sub** path self-heals or traps and the `subTap` is
  finite at every sample; the **dry** path is an add and is not sanitised, so the output is
  non-finite exactly while the input is, and finite again from the first finite input sample. No
  configuration turns a transient non-finite input into a *persistent* non-finite output.
- **A dry input above `+12 dBFS`** (reachable in a pre-limiter voice sum) — revised per the
  Clarifications session, Q6: this component's clamp applies to the sub contribution only, so a loud
  dry input passes through unclamped and untouched by this component (as FR-055's own principle
  already claims) and `getClampEngagementCount()` does not move. An earlier draft's clamp on the
  summed output would have clipped it and miscounted it as a clamp engagement; this is exactly the
  case Q6 exists to fix.
- **`setSubToMainEnabled(false)`** (new, Q8/FR-064) — the main output equals the dry input,
  bit-identical, while `subTap` continues to report the full sub signal; dormancy (FR-025) and the
  clamp counter (FR-054) are computed exactly as when the flag is `true`, since both are defined on
  `subChain`, upstream of the flag. SC-022 is the criterion.
- **Denormal input** — a body at 1e-30 for 60 s. The output stays finite, the follower does not
  stall, and CPU does not rise (FTZ/DAZ is set by the shared `dsp_test_main.cpp`; the composed
  primitives flush their own states).
- **Extreme block sizes** — `numSamples = 1` for 100 000 calls, and `numSamples = 8192` (the
  `PrepareConfig` ceiling) in one call. Both agree with the reference partition (SC-012); FR-007's
  absolute control-phase residue is what makes the `numSamples = 1` case run the same number of
  control steps.
- **`maxBlockSamples` smaller than the block actually rendered** — `processBlock` accepts any
  `numSamples` whatever `PrepareConfig` said. `maxBlockSamples` sizes only the `SaturationProcessor`
  vector the engine never reads (FR-073), so the mismatch is harmless — but it is stated, because
  silently depending on it would be.
- **Seed 0** — legal (FR-070); `deriveStreamSeed` substitutes a non-zero derived value, and the three
  breath lanes still differ from one another because their salts differ.
- **`setToneWaveform` mid-render** — declared **stepped** and excluded from SC-008 by name. The
  `SubOscillator` continues consuming any pending minBLEP residual after a switch away from `Square`
  (`sub_oscillator.h:310`, `:329`), so the switch is bounded, not click-free.
- **Aliased in/out pointers** — supported and tested (SC-012 (b)). Partial overlap (e.g.
  `outL == inL + 1`) is **not** supported and is documented as UB, the same contract every other
  Vorago Layer-3 component carries.

## Decisions taken where the roadmap is silent

- **D-1 — Saturation type is fixed to `SaturationType::Tape`, not exposed.** Roadmap line 314 says
  "`SaturationProcessor` at low drive" and names no type. `Tape` is `Sigmoid::tanh`
  (`saturation_processor.h:343-347`): symmetric, odd-harmonic, and therefore DC-free. `Tube` and
  `Diode` are explicitly asymmetric (`:54-59`) and would inject the DC that FR-042 then has to
  remove and SC-007 (c) then has to measure; `Digital` is a hard clip and `Transistor` a hard-knee
  clip (`:357-380`), neither of which is "low drive". A type selector was rejected as
  configurability that was not requested. **The same test is applied to the two controls the spec
  does expose beyond the roadmap's literal text — `setToneWaveform` (FR-015) and `setTrackingAmount`
  (FR-033) — and they clear it for reasons D-10 spells out. The principle is applied once, not
  case-by-case.**
- **D-2 — `TwoPoleLP`, not `OnePoleLP`.** Roadmap line 314 offers both. A 6 dB/octave corner at
  120 Hz leaves −6 dB at 240 Hz, so the "gentle low-pass" would still pass most of the fundamental it
  sits beneath. `TwoPoleLP` (`two_pole_lp.h:49`) is a Butterworth biquad, costs one extra state, and
  inherits `Biquad`'s non-finite self-heal.
- **D-3 — `TapeSaturator` rejected.** Named in the reuse row (line 118); it is a Jiles–Atherton
  hysteresis model with a per-sample Newton/RK solve (`tape_saturator.h:80,95-99,438,450`). The cost
  is not proportionate to shaping a ≤ 120 Hz band at ≤ 12 dB of drive.
- **D-4 — The Sine tones use `SubOscillator`'s own phase accumulator, discontinuity and all.** The
  alternative — a private `PhaseAccumulator` plus `std::sin` per tone — would remove the
  once-per-period reset at `sub_oscillator.h:274-278` and lower SC-004's THD, at the cost of
  abandoning the roadmap's reuse mandate (line 310, "extend/reuse `SubOscillator`") and duplicating
  a waveform switch that already exists. The reset's contribution is **bounded and measured**
  (SC-004), and the alternative stays on the record for the plan's probe to price if SC-004 misses.
- **D-5 — Infrasonic handling is this spec's addition; revised to filter-plus-backstop in the
  Clarifications session (Q2).** The roadmap does not mention it. An earlier draft added a single
  16 Hz hard-mute gate because `f/4` of any note below 64 Hz lands under 16 Hz and the chain's 10 Hz
  `DCBlocker` attenuates 13.75 Hz by only ≈ 2 dB, producing amplitude pumping and a real excursion
  hazard rather than "impossible low frequencies" — but a hard mute at 16 Hz meant most of the
  register a subterranean drone is actually played in (32–65 Hz) shipped `Div4` permanently silent.
  Q2 replaces the single mechanism with two: `DCBlocker2`, an 18 Hz 2nd-order Bessel high-pass, as
  the chain's stage-3 filter (FR-042), which attenuates 13.75 Hz by ≈ −7.5 dB continuously rather
  than as a step; and a far-below backstop gate at `kMinToneHz = 12 Hz` (FR-016) for tones close
  enough to DC that attenuation is not the same as safety. Both are fixed constants with no
  parameter, and `isToneInfrasonicFloored(tone)` still makes the backstop's engagement visible
  instead of mysterious.
- **D-6 — Default waveform is `Sine` for all three tones.** `Square` is the classic analog sub, but
  a square at 13.75–110 Hz through a 120 Hz low-pass is mostly its own fundamental plus a pumping
  third harmonic, and its minBLEP path costs more (measured by FR-071 (c)). `Sine` is the lowest-THD,
  lowest-cost default; `Square` and `Triangle` stay reachable and SC-005 measures the `Square`
  spectrum so the option is not shipped unmeasured.
- **D-7 — The sub is added, not crossfaded.** Roadmap line 305 calls this "cinematic weight"; a
  crossfade would attenuate the body as subs come in, which is the opposite. Phases 3 and 5 both
  crossfade because their wet paths *replace* character; this one *adds mass*. The consequence — the
  engine can raise the output level, which a crossfade cannot — is carried by FR-052–FR-054 and
  measured by SC-006.
- **D-8 — One `MinBlepTable` for the whole engine, not one per tone.** The header documents shared
  read-only use (`minblep_table.h:78-81`) and the type is non-copyable but movable (`:55-59`).
  Three tables would triple an 8 KB allocation for nothing.
- **D-9 — Tracking is linear-in-amplitude against a fixed reference, not a dB-domain knee.** A
  dB-domain map would need a `log` per control step and an extra pair of constants; the linear law of
  FR-032 produces the same audible behaviour (sub swells with body, then holds), needs no transcendental,
  and states its own test (SC-002 (a) is literally "unity slope in dB").
- **D-10 — Why `setToneWaveform` and `setTrackingAmount` are in scope while a saturation-type
  selector is not.** D-1 rejects a saturation-type selector as "configurability that was not
  requested"; the same sentence has to survive contact with FR-015 and FR-033 or the principle is
  being applied case-by-case. The test this spec uses is: **does the control expose an existing,
  shipped degree of freedom of a component the roadmap named, at a place the roadmap left open — or
  does it re-open a choice the roadmap already closed?**
  - **`setToneWaveform` (FR-015): in.** Roadmap line 310 says "extend/reuse `SubOscillator`", and
    `SubWaveform { Square, Sine, Triangle }` (`sub_oscillator.h:60`) is a control surface that
    component already ships and already tests. Exposing it adds a `switch`-free pass-through and no
    new machinery. The roadmap also calls the square sub the classic form of this effect in
    everything but name, and D-6 chooses `Sine` as the *default* precisely because the option's cost
    and spectrum were measured (SC-005, SC-013 (b) gated, FR-071 arms (c)/(j)) rather than assumed.
    The cost of the option is therefore priced and gated, not hidden.
  - **`setTrackingAmount` (FR-033): in.** Roadmap line 312–313 and success criterion line 319 make
    envelope tracking a *requirement*, and a requirement expressed as a fixed internal 1.0 cannot be
    measured against its own law: SC-002 (a)'s "unity slope in dB" and (b)'s "flat above the
    reference" both need the depth to be a variable to distinguish the tracking path from a constant
    gain. The default is exactly the roadmap's requirement (1.0, fully gated). Its `0.0` end reaches
    a configuration the roadmap's criterion warns about ("no free-running boom") — which is why this
    spec does not leave it unmeasured: SC-002 (c) pins it and SC-017 (B) soaks it for ten minutes at
    the worst case of every other control. An unreachable-by-design boom would be a claim; a
    reachable, bounded, soaked one is a measurement.
  - **A saturation-type selector: out.** Roadmap line 314 closed that choice — "`SaturationProcessor`
    at low drive" — and four of the five types (`Tube`, `Diode`, `Digital`, `Transistor`) are either
    asymmetric (injecting the DC FR-042 then removes and SC-007 (c) then measures) or hard clippers,
    i.e. they contradict the line that named them. Exposing them would add a control that can only
    be set to a value the roadmap excludes.
- **D-11 — Default tone levels sit ~9 dB under the body, not louder than it (Q3).** An earlier
  draft's `-6 / -12 / -18 dB` defaults, combined with the other defaults, put the sub louder than a
  −12 dBFS body — a defect in a "weight under the drone" layer that no FR or SC caught because
  nothing pinned the ratio. FR-020's defaults move to `-18 / -24 / -30 dB` and SC-021 pins the
  resulting ratio from a real render so the relationship is a checked fact, not an accident of three
  independently-chosen numbers.
- **D-12 — The output clamp binds the sub contribution, not the summed output (Q6).** An earlier
  draft's clamp on `out` (dry included) contradicted FR-055's own stated principle that a host's
  audio is not sanitised behind its back, and would have let a loud dry input pollute
  `getClampEngagementCount()` — a counter several success criteria (SC-006 (a), SC-009 (c3),
  SC-017 (d)) read as a pure statement about this component's ladder. Clamping `subChain * wetGain`
  before the add (FR-050, FR-054) is also what FR-052's `kMaxPreClampMagnitude` derivation already
  assumed, so this decision corrects the code to match math that was already written correctly.
- **D-13 — Breathing depth uses the house affine span, not `BreathingModulator`'s own `setDepth`
  (Q7).** Forwarding depth into the modulator (an earlier draft's choice) let a tone at depth 1.0
  mute completely once per breath cycle — tremolo, not "slow level-breathing" (roadmap line 311).
  The house form (`noise_organism.h:1842-1844`) leaves the modulator at its library default and
  applies `1 + kBreathGainSpan * depth * b` in the engine, bounding the swing to ±45% by
  construction and making the depth parameter comparable across `NoiseOrganism` and this engine for
  Phase 10's macros.
- **D-14 — The sub tap is a supported output with a routing flag, not measurement-only (Q8).** An
  earlier draft's Non-Goals explicitly excluded a sub-only bus and called FR-062's tap
  measurement-only; under that shape, Phase 9's cavern engine would receive full, unfiltered sub
  content with no way to route around it short of abusing a measurement entry point as product code.
  `setSubToMainEnabled` (FR-064) plus the already-existing tap gives Phase 10 a real routing
  mechanism at the cost of one flag; which way Phase 10 actually wires it remains that phase's
  decision, not this one's.

## Open Questions

Exactly one, and it is the one the roadmap explicitly defers to this spec.

- **OQ-1 (roadmap Open Question 4, lines 541–542; roadmap line 316–317) — Is the Subharmonic Engine
  global (post-voice-sum, tracking the lowest sounding voice's pitch) or per-voice?**
  The roadmap says "decide in spec after CPU measurement". This spec **cannot decide it before the
  measurement exists**, so it does three things instead of guessing:
  1. It makes the component **placement-agnostic**. Pitch enters through one scalar
     (`setFundamentalHz`, FR-013) and the body enters as ordinary stereo audio (FR-050). Nothing in
     the contract assumes one instance or eight, and nothing changes if the ruling flips.
  2. It specifies the deciding measurement: **FR-071 arms (l) and (m)** — one instance and eight
     instances at defaults, in ns per 512-sample block at 48 kHz — together with the dormant arm (k),
     which is what makes the per-voice option cheap when most voices are quiet.
  3. It notes the tradeoff so the ruling is informed rather than arithmetic. **Global** costs one
     instance regardless of polyphony but produces one sub pitch for the whole chord — the correct
     behaviour for a drone played as one or two held notes (roadmap line 92: "A drone instrument is
     played with one or two held notes"), and wrong for a held cluster, where the sub would track
     only the lowest note. **Per-voice** is musically exact and costs up to 8× — which SC-013's
     53 333 ns/block ceiling makes affordable only if arm (m) lands inside the 4–5 % per-voice
     envelope the roadmap allows (line 92) once phases 2, 3 and 5 have already spent
     1.75 % + 0.75 % + 1.5 % of it.
  4. **It gives the ruling a gate.** Deferring the *decision* to the measurement is legitimate
     sequencing; deferring it to nobody is not. **FR-076** makes the transcription of arms (k), (l)
     and (m), the projection arithmetic, and the recorded ruling a completion condition of this
     phase, and **SC-018** is the criterion whose evidence is that record. Without them the roadmap's
     Phase-6 decision would live only in this paragraph — and a decision that lives only in prose
     has no gate, passes every check, and can silently fail to be produced.
  **The ruling is the user's**, taken from the FR-071 table at the end of the build stage, and it
  binds Phase 10's wiring, not this component's code — but per FR-076 the phase is not complete until
  it is taken and written down.

## Traceability

| Roadmap statement (line) | Requirement / criterion |
|---|---|
| New L3 component `systems/subharmonic_engine.h` (307) | FR-001, New components table |
| Driven by known voice pitch, no detection (309, 118) | FR-013, Non-Goals |
| Phase-locked sub-oscillators at f/2, f/4 (309–310) | FR-010, FR-011, FR-014, SC-003 |
| 2f/3 fifth below (310) | FR-012, SC-003 (b) |
| "extend/reuse `SubOscillator`" (310) | FR-010, FR-012, FR-080 — **reuse only, no extension** |
| Each with its own level and slow level-breathing (310–311) | FR-020 (defaults revised, Q3), FR-021 (house affine span, Q7), FR-022 (house three-factor form, Q1), FR-024 |
| Amplitude-follows the voice body via `EnvelopeFollower` (312–313) | FR-030–FR-035 (FR-032 names the multiply point: post-saturator, pre-filter, inside the FR-062 tap; FR-035 makes the reference settable, Q4), SC-001, SC-002 |
| Chain: sub sum → LP → saturation → `DCBlocker` (314–315) | FR-040 block diagram, FR-041, FR-042 (`DCBlocker2`, revised Q2), FR-043, D-1, D-2, D-3 |
| Global vs per-voice, decide in spec after CPU measurement (316–317, 541–542) | OQ-1 (unchanged), **FR-076**, FR-071 (k)(l)(m), **SC-018**, SC-013 |
| SC: sub level tracks input envelope, no free-running boom (319) | SC-001, SC-002 (incl. (e), Q4), SC-017 (B) |
| SC: harmonic purity of dividers, THD measured (319–320) | SC-003, SC-004 (per-tone sweeps clearing the FR-016 backstop; `measureLowFrequencyThdPercent`, not `calculateTHD`), SC-005 |
| SC: true-peak safety with subs at max (320) | SC-006 |
| SC: mono-compatibility, `midside` correlation (320–321) | FR-043, SC-007 (mid/side of the sub contribution) |
| SC: CPU ≤ 0.5 % (321) | FR-071, SC-013 (a) default arm **and** (b) all-`Square` arm, both gated |
| ODR sweep before any new class name (127–129) | New components table (sweep transcribed) |
| RT safety, pools sized at prepare (510–511) | FR-003, FR-004, FR-074, SC-010 |
| Boundedness soak test, **worst case** (512–514), overnight bounded/not-dead (93–95) | FR-052–FR-055 (clamp scoped to the sub only, Q6), SC-017 **(A) default and (B) worst case**, SC-006, SC-009 |
| Sample-rate change / re-prepare (cross-cutting, Edge Cases) | FR-005, FR-006, **SC-019** |
| Infrasonic handling: filter + backstop (this spec's addition, D-5, revised Q2) | FR-016, FR-042, **SC-020**, SC-008 floor arms |
| Layer discipline + ODR (515) | FR-001, FR-002, SC-015 |
| CPU budgets are FRs, per-voice for phases 2–8 (516) | FR-071, SC-013 |
| Dormancy rule (517–525) | FR-025 (control-step granularity, Q5, with its stated deviation and justification), FR-026 (Q5), SC-014 |
| No bit-exact float goldens (526) | FR-075, SC-011, SC-012 |
| Portability, WSL probe, aligned-load lint (527–528) | FR-008, SC-015 |
| Naming (529) | Naming throughout; no plugin parameters in this phase |
| Shared-component changes keep Seraphis green (530–532) | FR-080, SC-016 |
| Default sub-to-body level ratio (not a roadmap line; Clarifications Q3) | FR-020, D-11, **SC-021** |
| Output clamp scoped to the sub contribution (not a roadmap line; Clarifications Q6) | FR-050, FR-054, D-12 |
| Sub tap promoted to a supported output, routing flag (not a roadmap line; Clarifications Q8) | FR-050, FR-062, FR-064, D-14, **SC-022** |

## Assumptions

- **A-1** — `getCurrentValue()` on `BreathingModulator` is virtual (`breathing_modulator.h:222`,
  overriding `ModulationSource`). FR-024 reads it three times per 64-sample control chunk, i.e. 3/64
  virtual calls per sample; the assumption that this is negligible is **measured**, not asserted, by
  FR-071 arm (i) against arms (a)–(h).
- **A-2** — `SaturationProcessor::processSample` advances three `LinearRamp`s per call
  (`:233-235`) even when no parameter is moving. The assumption that three ramp advances per sample
  are affordable inside a 53 333 ns/block budget is measured by FR-071 arm (g). If it is not, the
  pre-authorised lever is to bypass the processor's smoothers by writing gains only on change — a
  change to *this* component's call pattern, never to the shipped processor.
- **A-3** — The four new TUs are appended to the `dsp_systems_tests` enumerated list rather than
  given their own executable, matching Phases 2, 3 and 5. If the systems suite's link time becomes
  the binding constraint, that is a build-infrastructure decision outside this phase.
- **A-4** — **Four** test helpers are added to `tests/test_helpers/` and are usable by later phases.
  Adding a test helper is not a shared-DSP change and does not fall under FR-080. Each is validated
  against a synthetic signal of known truth in the same TU that consumes it, so a helper bug fails as
  a helper bug:
  - `measureTruePeakDb(const float* l, const float* r, size_t n)` — SC-006; built on the shipped
    `Oversampler` (`primitives/oversampler.h:226`), the 4× basis `TruePeakLimiter::processChunk`
    uses (`true_peak_limiter.h:125-146`).
  - a pointer/length `calculateCorrelation` overload beside `buffer_comparison.h:201` — SC-007; the
    shipped form is `template <size_t N>` over `std::array` and is unusable on a multi-minute heap
    render.
  - `estimatePeakFrequencyHz(const float* x, size_t n, float sampleRate)` — SC-003; a
    262 144-point Hann-windowed transform plus parabolic log-magnitude interpolation across the peak
    bin and its two neighbours. **Nothing under `tests/test_helpers/` interpolates a peak today** —
    `spectral_analysis.h` exports `frequencyToBin`, `getHarmonicBins`, `sumBinPower` and `toDb` and
    nothing else — so without it SC-003's tolerances are finer than its own measurement's resolution
    and the criterion cannot pass on a correct implementation.
  - `measureLowFrequencyThdPercent(const float* x, size_t n, float fundamentalHz, float sampleRate,
    int maxHarmonic = 10)` — SC-004; a 262 144-point Hann-windowed transform with main-lobe
    (peak-bin ±2) power summation at the fundamental and each harmonic, a documented ≤ −100 dB
    leakage floor at the frequencies SC-004 measures, and a negative sentinel when the fundamental is
    below −60 dBFS. `signal_metrics.h:111 calculateTHD` cannot serve: its 8192-point cap and ±2-bin
    windows overlap at 20–110 Hz, and it silently returns `0.0f` on a silent input.
- **A-5** — The FR-013 fundamental ceiling of 4186 Hz (C8) is a synth-range convention, not a
  measured limit. Nothing in the component breaks above it; the clamp exists so the `4f/3` master
  cannot be pushed toward Nyquist by a caller bug.

## Review notes

Every issue raised in the review of this document was **accepted**; nothing was resolved by relaxing
a threshold. Where an issue offered a choice of remedies, this section records which branch was taken
and why, so the plan does not have to re-derive it.

- **The FR-013 default moved (55 → 82.5 Hz), then moved back to 55 Hz in the Clarifications session
  (Q2).** An earlier review round raised two issues (the FR-016/SC-003/SC-004/SC-013 blocker and the
  SC-013-fixture major) that each offered "raise the default above 64 Hz" *or* "state that one tone
  is permanently floored at the default"; raising the default to 82.5 Hz was taken at the time
  because the annotation branch left the shipped product with a two-tone engine wearing a three-tone
  contract — dead level, breath and waveform defaults on `Div4`, an unmeasurable third tone, and a
  permanent asterisk on every "at defaults" fixture. Q2 later removed the premise: replacing the
  16 Hz hard mute with an 18 Hz filter plus a 12 Hz backstop means `Div4` (13.75 Hz at 55 Hz) clears
  the floor on its own, so the default reverts to 55 Hz (FR-013) with no annotation needed and no
  tone permanently dead. 40 Hz is now the two-tone demonstration fundamental (FR-071 arm (n),
  SC-013 (d)), and 40 Hz is the fixture for SC-020 (a).
- **The tracking gain is applied post-saturator, pre-filter (the FR-042 stage, `DCBlocker2` since
  Q2)** (FR-032, FR-040's block diagram),
  not pre-chain as one of the two suggested remedies proposed. Both remedies make SC-002 executable;
  post-saturator additionally keeps FR-041's drive level-independent, which is what lets SC-004
  measure the dividers rather than a level-dependent shaper. The pre-chain branch would have needed
  FR-041 to acknowledge that body level modulates distortion — a coupling no roadmap line asks for.
- **FR-015 (waveform) and FR-033 (tracking amount) were kept, with the consistency argument written
  down** (D-10, cross-referenced from D-1), rather than dropped. The issue explicitly allowed either.
  Keeping them costs one gated perf arm (SC-013 (b) is now gated, not merely reported) and one
  spectral criterion (SC-005); dropping `setTrackingAmount` would have made SC-002 (a)/(b)
  unmeasurable, since a fixed internal 1.0 cannot be distinguished from a constant gain.
- **SC-007 (a) was replaced with a mid/side measurement plus a muted-reference correlation
  comparison**, and the `≥ 0.9999` correlation clause withdrawn as unfalsifiable under FR-043. The
  issue's suggested "and/or" is taken as "and": the mid/side form catches per-channel chain or clamp
  divergence in the sub itself, and the muted-reference comparison catches it in the summed output.
- **SC-013 (c)'s margin is 15 %, tied to a cited number** — the ~14 % session-to-session drift the
  perf idiom records at `resonance_drift_network_perf_test.cpp:78-90` — rather than the issue's
  fallback of demoting the arm to reported-only. A dormancy skip that cannot beat measurement noise
  is not worth the branch, so the margin is the honest gate; if it misses, FR-071's stop-and-surface
  rule reconsiders what FR-025 skips.
- **SC-009 (c) was restated against the contract the component actually makes**, not against a
  finite output under a non-finite input. The issue's alternative — adding a dry-path sanitiser —
  was rejected: silently repairing a host's non-finite audio hides the host's bug, and no roadmap
  line asks for it. The `subTap`-is-finite assertion gives the trap a direct, falsifiable external
  measurement, which the withdrawn clause did not.
- **Three criteria were added rather than folded into existing ones** — SC-018 (the OQ-4 ruling),
  SC-019 (sample-rate and re-prepare), SC-020 (the infrasonic floor) — because each covers a
  requirement that previously had no criterion at all, and a criterion that is a sub-clause of an
  unrelated one is easy to drop silently at task-breakdown time.
- **Two test helpers were added beyond the two the earlier draft declared** (`estimatePeakFrequencyHz`,
  `measureLowFrequencyThdPercent`; A-4). The alternative offered for SC-003 — relaxing the tolerance
  to ≥ 1.5 bins and dropping the ±2-cent clause — was rejected under the no-relaxation rule: the
  ±2-cent clause is the only thing that distinguishes FR-012's `4f/3`-master construction from
  "some low tone appeared", and the roadmap's THD criterion (line 319–320) is unmeasurable at these
  frequencies with the shipped `calculateTHD`. The measurement was fixed, not the line.
- **One factual correction to the Existing-components table**: `DCBlocker2` is a 2nd-order **Bessel**
  high-pass (`dc_blocker.h:262-264`, `:356`, `Q = 1/sqrt(3)`), not Butterworth. The line number cited
  was right; the filter-family label was wrong. At the time this was written `DCBlocker2` was read
  and not used; the Clarifications session (Q2) later makes it the chain's stage-3 filter (FR-042),
  so the correction now describes a component this phase actually calls.
- **Two further criteria were added in the Clarifications session, beyond the twenty already
  present** — SC-021 (the Q3 default sub-to-body ratio) and SC-022 (the Q8 sub-to-main routing
  flag) — for the same reason the three criteria above were added rather than folded in: each covers
  a requirement with no prior criterion, and folding either into an existing one would make it a
  sub-clause easy to drop at task-breakdown time.

