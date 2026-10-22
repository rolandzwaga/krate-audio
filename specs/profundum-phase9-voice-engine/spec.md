# Feature Specification: Profundum Phase 9 — Voice & Engine

**Spec slug:** `profundum-phase9-voice-engine`
**Status:** Draft (2026-10-19), written on branch `synth/profundum` at `0ae33b87`. The requirements are complete. Any bar or
behaviour marked *drafted* is a proposal for the clarify / plan stages to rule. Once it is ruled, the ruling goes into
its FR / SC row first, then into Clarifications. The roadmap defers several decisions to this spec. They are listed under
Open Questions, and none is pre-decided here.
**Roadmap source:** `specs/Profundum-roadmap.md` → Part A → **Phase 9** (lines 647–697). This spec also traces to:
- **Key Design Decision 2** (lines 125–131): the Anchor is always centred; SOLID / SPREAD / WIDE; "Mono-sum phase
  cancellation is checked by test at every stereo-touching stage".
- **Key Design Decision 3** (lines 132–136): "MONO: one active voice, a new note replaces the previous (legato,
  retrigger, glide, note priority). POLY: each note its own voice, **4 voices** to start (raisable), steal the oldest
  *released* voice first, then the oldest active. Every voice owns its oscillator state, Anchor, amplitude and pitch
  envelopes and voice-level processing — the Anchor is per voice, never one shared oscillator. Mono/stereo image and
  mono/poly are independent axes."
- **Key Design Decision 4** (lines 137–144): "Morph is one continuous, automatable performance control affecting all
  voices together".
- **Reuse Inventory**, rows "Modulation / macros" (line 168) and "Voice / engine" (line 170): "🆕
  `ProfundumMacroMatrix` (L3, pattern copy)", "🔶 pattern only. 🆕 `ProfundumVoice`, `ProfundumEngine` (L3)". The
  "Output safety" row (line 171) reads "✅ direct reuse". ODR note and hazard list: lines 174–176.
- **Upstream handoffs** that earlier phases wrote into this phase:
  - Phase 2: line 280, the split hump, a listening item.
  - Phase 3: lines 313–330 and its spec's FR-066 / D-7.
  - Phase 4: lines 361–379 and 696–697.
  - Phase 5: lines 425–434.
  - Phase 6: lines 481–494, and `specs/profundum-phase6-mass-dynamics/evidence/phase9-handoff.txt`.
  - Phase 7: lines 533–542, and `specs/profundum-phase7-stereo-field/evidence/phase-handoffs.txt` §2.
  - Phase 8: lines 595–605, and `specs/profundum-phase8-bass-morph/evidence/phase-handoffs.txt` §9.1–9.13.
- **Cross-Cutting Constraints** (lines 969–992), the theme-level FRs for audibility, mono compatibility and
  anti-aliasing (973–980), and "CPU budgets are FRs … whole engine 9 at both MONO and 4-voice operating points"
  (981–982).
- **Open Questions 7 and 8** (lines 1008–1011). OQ-7 is POLY placement and FOLLOW per voice. OQ-8 is the polyphony
  ceiling.
- **Phase 11 consumer** (lines 716, 741): IDs 100–199 hold "Personality macros + Mass + Morph position". The Motion
  target set "covers Mass and the core spectral controls first (no general matrix)".
- **Phase 12 consumer** (lines 771, 801): "Macro-to-parameter mapping table — Phase 9's `ProfundumMacroMatrix` table
  reproduced and ratified", and the Main page shows "Motion. Main page: source mode only (OFF / ENV / CYCLE / DRIFT)".

**Concept source:** `specs/Profundum-concept.md`:
- §9 "Bass Personality" (lines 422–461): WEIGHT "Sub/fundamental/body", DARK ↔ BRIGHT "Spectral distribution", TIGHT ↔
  LOOSE "Envelope, transient and compression behavior", CLEAN ↔ VIOLENT "Nonlinear processing", and "presets could
  store these coordinates".
- §11 "Motion" (lines 522–563): ENV "One flexible envelope", CYCLE "LFO / synced rhythmic modulation", DRIFT "Slow
  random modulation", "MODULATION DEPTH becomes a global concept", and "Motion can modulate *Mass*".
- §13, the processing architecture (lines 601–646).

**Layer.** Every new component is Layer 3, header-only, in `Krate::DSP`, under `dsp/include/krate/dsp/systems/`. Every
new name passes the ODR sweep (see "New components").

---

## Overview

Phase 9 composes the eight finished DSP phases into the playable instrument core. It adds two playable classes and two
control classes:

1. **`ProfundumVoice`** is one bass voice. It has two parallel paths:
   - **Main path:** core (+ edge excitation) → `ShapeMorph` → `HarmonicFold` → `BassCharacterStage`.
   - **Anchor path:** a per-voice `AnchorOscillator` that joins after Character by a plain add. It can also be fed into
     the Fold input by the `anchorFoldFeed` send, with the direct path always present.

   Drop and glide act on pitch. Note Memory decides what restarts. The amp envelope shapes the whole voice.
2. **`ProfundumEngine`** owns:
   - the voices, run as MONO (`MonoHandler`, the default) or POLY (`VoiceAllocator`, 4 voices);
   - the global Bass Morph (`BassMorphEngine`);
   - the Mass law (`MassLaw`);
   - the Motion section;
   - the Personality macros;
   - the global chain: voice sum → `MassDynamics` → `BassStereoField` safety stage → `DCBlocker` ×2 →
     `TruePeakLimiter`.

   Everything runs on the shared 32-sample control grid.
3. **The Motion section** offers three sources: ENV (one flexible envelope), CYCLE (`LFO`) and DRIFT (`BrownianDrift`).
   It carries a global **Motion Depth** and drives a small fixed target set: Mass and the core spectral controls. This
   is the configuration Phase 8 fixed as `MorphState::MotionConfig`.
4. **`ProfundumMacroMatrix`** holds four Personality macros: Weight, Dark ↔ Bright, Tight ↔ Loose and Clean ↔ Violent.
   It is a constexpr row table, copied from the `VoragoMacroMatrix` pattern.

This phase then **proves the instrument before any plugin code exists**:
- a capability audibility table in which every section's on/off and every lever moves a render descriptor past a
  ruled bar;
- clickless steals and note replacements;
- monotone macro sweeps;
- a measured Motion → Mass swell;
- a determinism harness;
- a 30-minute soak;
- a full-engine mono-sum check in every image mode;
- the Anchor-through-the-chain clauses Phases 3–5 deferred here;
- the CPU ceilings (≤ 8 % MONO and ≤ 25 % at 4 voices, one core @ 48 kHz).

## Scope

1. `ProfundumVoice` (L3), complete (FR-001–FR-016).
2. `ProfundumEngine` (L3), complete (FR-020–FR-032).
3. The Motion section (FR-040–FR-049). Drafted as a separate L3 component, `ProfundumMotion`; the plan may fold it into
   the engine (D-3).
4. `ProfundumMacroMatrix` (L3), complete (FR-050–FR-056).
5. Determinism, RT safety, non-finite safety, sample rates, block sizes and partition invariance (FR-060–FR-066).
6. The register of incoming handoffs. Each item is gated, recorded, listened to or decided (FR-070, FR-071).
7. The theme-level FRs: audibility table, mono compatibility, anti-aliasing and CPU (FR-080–FR-084), and the test
   taps and seams the gates read (FR-086).
8. The outgoing handoffs to Phases 10–12 (FR-085).
9. Tests for all of the above, in `dsp/tests/unit/systems/` (executable `dsp_systems_tests`), tagged
   `[systems][profundum][voice-engine]`.

## Non-Goals (owned by later phases, or deliberately excluded)

| Excluded | Owner | Roadmap |
|---|---|---|
| Plugin scaffold, AU identity, bus config, `profundum_tests` target | Phase 10 | 706–727 |
| Parameter IDs, normalisation, `kCurrentStateVersion`, `IMidiMapping` (mod wheel / pressure → Mass, pitch-bend range, sustain), the Edit A / Edit B surface, Copy A → B and compare | Phase 11 | 729–744 |
| The registered polyphony **range**. This phase rules only `kMaxVoices` and the 4-voice default (OQ-3) | Phase 11 | 1010–1011 |
| Any UI: spectrum / energy view, labels, the macro-mapping display, the control-name collisions (roadmap OQ-10) | Phase 12 | 746–936, 1014–1017 |
| Factory presets, the preset × capability matrix | Phase 13 | 938–951 |
| A general modulation matrix. The roadmap fixes "a small fixed destination set" (line 666) and Phase 11 "no general matrix" (line 741) | Never in v1 | 666, 741 |
| An FX rack, a conventional filter section | Never (KDD 5) | 145–147 |
| Any change to the shipped Phase 1–8 component headers. If the plan finds one is needed, it is append-only and gated by the consumer suites (FR-084) | — | 991–992 |
| Re-tuning any Phase 1–8 bar | — | 981–982 |

---

## Existing components (verified this session, 2026-10-19)

Paths are relative to `dsp/include/krate/dsp/` unless stated otherwise. Every row was opened this session, and the
signatures are quoted from the header.

### Voice chain (composed per voice)

| Component | Header | Reused for | Verified signature / fact |
|---|---|---|---|
| `ProfundumCore` | `systems/profundum_core.h:92` | Harmonic core + edge excitation | `enum class RetriggerPhase : std::uint8_t { Reset, FreeRunning }` (94).<br>• `kDefaultPartials = 64` (96), `kControlInterval = 32` (97), `kPitchUpdateInterval = 16` (98).<br>• `void prepare(double sampleRate, int numPartials = kDefaultPartials) noexcept` (129). It enables `bank_.setContinuousFrequencyUpdates(true)` (140).<br>• `void setControls(const SpectralShapeRecipe::Controls& controls) noexcept` (173), `void setFrequency(float f0Hz) noexcept` (195), `void setRetriggerPhase(RetriggerPhase policy) noexcept` (201), `void noteOn(float f0Hz) noexcept` (208).<br>• `void setPartialPanOffsets(const std::array<float, kMaxPartials>& offsets) noexcept` (218).<br>• `void processBlock(float* left, float* right, std::size_t numSamples, const float* f0PerSample = nullptr) noexcept` (237).<br>• `[[nodiscard]] std::span<const float> deliveredGains() const noexcept` (298).<br>• `struct ExcitationControls { float mix = 0.0f; float drive = 0.5f; float asymmetry = 0.0f; }` (334–338), `void setExcitation(const ExcitationControls& c) noexcept` (356). |
| `SpectralShapeRecipe` | `processors/spectral_shape_recipe.h` | Unbiased h1 for the Anchor (FR-005) | `static void evaluate(const Controls& c, float f0Hz, double sampleRate, std::span<float> out) noexcept` (211).<br>• `static constexpr float kCenterPanGain = 0.7071067811865476f` (52). |
| `ShapeMorph` | `systems/shape_morph.h` | SHAPE stage | `void prepare(double sampleRate, std::size_t maxBlockSize) noexcept` (135).<br>• `void setShape(float position) noexcept` (194), `void setEnabled(bool enabled) noexcept` (202), `void setFundamentalHz(float hz) noexcept` (209).<br>• `void processBlock(float* left, float* right, std::size_t numSamples, const float* f0PerSample = nullptr) noexcept` (221).<br>• `latencySamples() == 0` (307). |
| `HarmonicFold` | `processors/harmonic_fold.h` | Harmonic Fold | `void setAmount(float amount) noexcept` (168), [0, 100].<br>• `void setFundamentalHz(float hz) noexcept` (178).<br>• `void processBlock(float* left, float* right, std::size_t numSamples) noexcept` (188).<br>• `latencySamples() == 0` (271). |
| `BassCharacterStage` | `systems/bass_character_stage.h:76` | Character | `void setAlgorithm(Algorithm a) noexcept` (241), `void setAmount(float a) noexcept` (258), `void setTone(float t) noexcept` (265).<br>• `void setChannelOffsets(float amountOffset, float toneOffset) noexcept` (273), `void setFundamentalHz(float hz) noexcept` (287).<br>• `void syncPhase(float coreCycles) noexcept` (302). Its comment (295–301): "Call after prepare()", "reset() clears the sync".<br>• `void setInputHalfWaveSymmetric(bool symmetric) noexcept` (317).<br>• `void processBlock(float* left, float* right, std::size_t numSamples, const float* f0PerSample = nullptr) noexcept` (334).<br>• `[[nodiscard]] float perceivedPitchRatio() const noexcept` (467). |
| `AnchorOscillator` | `systems/anchor_oscillator.h:55` | Per-voice Anchor | `enum class Waveform : std::uint8_t { Sine, Triangle, RoundedSine, AsymmetricSine, OctaveDownSine }` (57), `enum class Mode : std::uint8_t { Lock, Follow, Punch, Ghost }` (58).<br>• `enum class RetriggerPhase : std::uint8_t { Reset, FreeRunning }` (59), commented "D-6: same values as ProfundumCore::RetriggerPhase".<br>• `struct Controls { Waveform waveform = Waveform::Sine; Mode mode = Mode::Lock; float level = kDefaultLevel; float punchBoostDb = 6.0f; float punchTimeMs = 40.0f; float ghostTimeMs = 15.0f; }` (99–106).<br>• `void setControls(const Controls&) noexcept` (224), `void setFrequency(float f0Hz) noexcept` (248), `void setFollowRatio(float r) noexcept` (255), `void setCoreH1Amplitude(float a1) noexcept` (262), `void setRetriggerPhase(RetriggerPhase) noexcept` (269), `void noteOn(float f0Hz, bool rearm = true) noexcept` (276).<br>• `void processBlock(float* left, float* right, std::size_t numSamples, const float* f0PerSample = nullptr, const float* followRatioPerSample = nullptr) noexcept` (285).<br>• `latencySamples() == 0` (383). |
| `DropEnvelope` | `processors/drop_envelope.h:29` | Drop | `void setAmount(float semitones) noexcept` (83), `void setTime(float ms) noexcept` (87), `void setShape(float s) noexcept` (91), `void setRebound(float r) noexcept` (95).<br>• `void trigger(bool snap) noexcept` (103), commented "Latch the parameters and plan the curve".<br>• `[[nodiscard]] float process() noexcept` (124), `void processBlock(float* out, std::size_t n) noexcept` (133). |
| `DropPhaseLock` | `processors/drop_phase_lock.h:64` | Re-lock of h1 after a Drop | `enum class TriggerKind : std::uint8_t { SnapReset, SnapFreeRunning, Bridged }` (66).<br>• `void setBypass(bool follow) noexcept` (115).<br>• `void trigger(const DropCurve& curve, float fResHz, std::size_t gridPhase, float minF0Hz, float capHz, …)` (130).<br>• `[[nodiscard]] float process(float plannedOffset) noexcept` (146). |
| `NoteMemory` | `processors/note_memory.h:21` | What restarts on each note event | `enum class Mode { AlwaysRetrigger, Legato, EnvelopeRetriggerOnly, OscillatorContinuous }` (24–29), `enum class Retrigger { Reset, Continue }` (32).<br>• `enum class Transition { FromSilence, Overlap, Tail, ReturnToHeld, VoiceReuse }` (35–41), `enum class PhasePolicy : std::uint8_t { Reset = 0, FreeRunning = 1 }` (45).<br>• `struct Actions { bool oscillatorRestart; PhasePolicy phase; bool envelopeRestart; bool dropTrigger; bool dropSnap; bool anchorRearm; bool declick; }` (48–56).<br>• `[[nodiscard]] Actions resolve(Transition t, bool voiceAudible) const noexcept` (76). |
| `RetriggerDeclick` | `primitives/retrigger_declick.h:28` | Restart declick | `void arm() noexcept` (58), `[[nodiscard]] std::size_t samplesUntilSwitch() const noexcept` (68), `void processBlock(float* gain, std::size_t n) noexcept` (89), `[[nodiscard]] bool isActive() const noexcept` (96). |
| `ADSREnvelope` | `primitives/adsr_envelope.h:54` | Amp envelope (drafted, OQ-1) and the ENV candidate | `void prepare(float sampleRate) noexcept` (68), `void gate(bool on) noexcept` (86).<br>• `setAttack(float ms)`, `setDecay(float ms)`, `setSustain(float level)`, `setRelease(float ms)` (115–135).<br>• `setAttackCurve(float amount)`, `setDecayCurve(float)`, `setReleaseCurve(float)` (160–178). Each one rebuilds a 256-entry table (Phase 8 handoff 9.3).<br>• `setRetriggerMode(RetriggerMode)` (209), `[[nodiscard]] float process() noexcept` (232), `[[nodiscard]] bool isActive() const noexcept` (266).<br>• The member defaults are attack 10 / decay 50 / sustain 0.5 / release 100 ms and `EnvCurve::Exponential` with `float attackCurveAmount_ = 0.7f; // Default matches EnvCurve::Exponential` (521–533). |
| `MultiStageEnvelope` | `processors/multi_stage_envelope.h:61` | Amp-envelope alternative (OQ-1) | `kMinStages = 4`, `kMaxStages = 8` (63–64).<br>• `void setStage(int stage, float level, float ms, EnvCurve curve) noexcept` (166). The curve is an `EnvCurve` **enum**, not a float amount.<br>• `setSustainPoint(int)` (178), `setLoopEnabled(bool)` (186), `gate(bool on)` (99), `process()` (249). |

### Note handling

| Component | Header | Reused for | Verified signature / fact |
|---|---|---|---|
| `MonoHandler` | `processors/mono_handler.h:103` | MONO mode | `struct MonoNoteEvent { float frequency; uint8_t velocity; bool retrigger; bool isNoteOn; }` (41–46).<br>• `enum class MonoMode : uint8_t { LastNote, LowNote, HighNote }` (53), `enum class PortaMode : uint8_t { Always, LegatoOnly }` (64), `enum class GlideMode : uint8_t { ConstantTime, ConstantRate, Quantized }` (74).<br>• `[[nodiscard]] MonoNoteEvent noteOn(int note, int velocity) noexcept` (148), `[[nodiscard]] MonoNoteEvent noteOff(int note) noexcept` (224).<br>• `void setPortamentoTime(float ms) noexcept` (293), `[[nodiscard]] float processPortamento() noexcept` (305).<br>• `setMode(MonoMode)` (325), `setLegato(bool)` (341), `setPortamentoMode(PortaMode)` (347), `setGlideMode(GlideMode)` (357), `[[nodiscard]] bool hasActiveNote() const noexcept` (383).<br>• `kMaxStackSize = 16` (109). |
| `VoiceAllocator` | `systems/voice_allocator.h:187` | POLY mode | `kMaxVoices = 32` (193).<br>• `[[nodiscard]] std::span<const VoiceEvent> noteOn(uint8_t note, uint8_t velocity) noexcept` (228). Its `noteOff` returns the same type.<br>• `void voiceFinished(size_t voiceIndex) noexcept` (288), `void setAllocationMode(AllocationMode) noexcept` (311), `void setStealMode(StealMode) noexcept` (317), `[[nodiscard]] std::span<const VoiceEvent> setVoiceCount(size_t count) noexcept` (326).<br>• `struct VoiceEvent { enum class Type : uint8_t { NoteOn, NoteOff, Steal }; Type type; uint8_t voiceIndex; uint8_t note; uint8_t velocity; float frequency; }` (102–114).<br>• `findStealVictimSingle()` (631–638): "Prefer releasing voices over active voices", then `findBestVictimByState` → `findVictimOldest` under `AllocationMode::Oldest` (672–684). This is exactly the roadmap's "oldest released, then oldest active". |
| `NoteProcessor` | `processors/note_processor.h:51` | Pitch bend | `void prepare(double sampleRate) noexcept` (80), `void setPitchBend(float bipolar) noexcept` (101), `[[nodiscard]] float processPitchBend() noexcept` (113), `void setPitchBendRange(float semitones) noexcept` (126), `[[nodiscard]] float getFrequency(uint8_t note) const noexcept` (166). |

### Global chain and control layer

| Component | Header | Reused for | Verified signature / fact |
|---|---|---|---|
| `BassMorphEngine` | `systems/bass_morph_engine.h:46` | Global Bass Morph | `struct Resolved { MorphState state{}; float position = 0.0f; float anchorPosition = 0.0f; float anchorMass = MassLaw::kNeutralMass; }` (55–60).<br>• `kControlInterval = 32` (62).<br>• `bool setEndpoint(Endpoint e, const MorphState& s) noexcept` (113), `void setPosition(float p) noexcept` (140), `void setPositionRampMs(float ms) noexcept` (155), `void setAnchorMorphMode(AnchorMorphMode m) noexcept` (168).<br>• `const Resolved& advanceInterval() noexcept` (205), `[[nodiscard]] const Resolved& resolved() const noexcept` (227).<br>• `serializeRuntime` (235), `restoreRuntime` (274). |
| `MorphState` | `systems/morph_state.h:43` | The per-interval sound state every voice reads | `struct AmpEnvelopeFields { attackMs, decayMs, sustain, releaseMs, attackCurve, decayCurve, releaseCurve }` (71–79).<br>• `struct MotionConfig` (81–101) with `Source { Off, Env, Cycle, Drift }`, `Target { Mass, Depth, Body, Edge, Shift }`, `Polarity { Bipolar, Unipolar }`, `amount` [−1, 1], `depth` [0, 1], `cycleRateHz`, `cycleShape`, `cycleSync`, `cycleNote`, `cycleModifier`, `cyclePhaseDeg`, `cycleRetrigger`, `cycleFadeInMs`, `polarity`, `driftSmoothness`, `envAttackMs`, `envDecayMs`.<br>• Members `recipe`, `excitation`, `foldAmount`, `shape`, `characterAlgorithm` / `characterAmount` / `characterTone`, `dropAmountSt` / `dropTimeMs` / `dropShape` / `dropRebound`, `ampEnv`, `field`, `motion`, `mass`, `transientAttackDb` / `transientSustainDb`, `anchor`, `anchorFoldFeed` (103–122).<br>• `kMorphStateFormatVersion = 1` (131), `kMorphStateBytes = 201` (135). |
| `MassLaw` | `systems/mass_law.h:64` | Mass → seven targets | `enum class Target : std::uint8_t { Depth, Body, TransientDuration, FoldAmount, CompressionAmount, AnchorLevel, OutputTrim }` (67–69).<br>• `struct Targets` (82–90), `kMassSweepMs = 40.0f` (77).<br>• `[[nodiscard]] static float evaluateRow(const Row& row, float mass, float base) noexcept` (132), `[[nodiscard]] static Targets evaluate(float mass, const Targets& base) noexcept` (142).<br>• `prepare(double)` (153), `setMass(float)` (163), `setBase(const Targets&)` (170), `[[nodiscard]] float advanceInterval() noexcept` (179), `[[nodiscard]] Targets currentTargets() const noexcept` (185). |
| `MassDynamics` | `systems/mass_dynamics.h:469` | Global Mass dynamics | `void prepare(double sampleRate, std::size_t maxBlockSize) noexcept` (482).<br>• `setTransientDurationMs(float ms)` (504), `setCompressionAmount(float amount01)` (511), `setDetectorKeyHz(float f0Hz)` (514), `setOutputTrimDb(float dB)` (518), `setTransientAttackDb(float dB)` (530), `setTransientSustainDb(float dB)` (531).<br>• `void processBlock(float* left, float* right, std::size_t numSamples) noexcept` (539). `latencySamples() == 0` (564). |
| `BassStereoField` | `systems/bass_stereo_field.h:62` | Stereo field (per-voice law + global stage) | `enum class ImageMode : std::uint8_t { Solid, Spread, Wide }` (64).<br>• `struct Controls` (99–111), `struct VoiceState` (113–126, "one per voice, caller-owned"), `struct ChannelOffsets { float amount = 0.0f; float tone = 0.0f; }` (128–131), `class MotionOutput final : public ModulationSource` (133).<br>• `void setControls(const Controls& c) noexcept` (234), `void setTempoBpm(float bpm) noexcept` (274), `void setDriftSeed(std::uint32_t seed) noexcept` (282), `void advanceControl() noexcept` (293).<br>• `void computePanVector(VoiceState& state, float f0Hz, std::array<float, kMaxPartials>& out, int numPartials = kDefaultPartials) const noexcept` (349), `[[nodiscard]] ChannelOffsets wideOffsets(BassCharacterStage::Algorithm a) const noexcept` (413).<br>• `void processBlock(float* left, float* right, std::size_t numSamples) noexcept` (437). `latencySamples() == 0` (483). |
| `DCBlocker` | `primitives/dc_blocker.h:94` | Output DC safety ×2 | `void prepare(double sampleRate, float cutoffHz = 10.0f) noexcept` (135), `void setState(float x1, float y1) noexcept` (165), `[[nodiscard]] float process(float x) noexcept` (200), `void processBlock(float* buffer, size_t numSamples) noexcept` (228). |
| `TruePeakLimiter` | `processors/true_peak_limiter.h:45` | Output limiter | "Zero-latency true-peak limiter (stereo-linked)" (40).<br>• `kDefaultCeilingDb = -1.0f` (47).<br>• `void prepare(double sampleRate, std::size_t maxBlockSize) noexcept` (60), `void setCeilingDb(float db) noexcept` (86), `void processBlock(float* left, float* right, int numSamples) noexcept` (105). |
| `LFO` | `primitives/lfo.h:63` | CYCLE | `class LFO {` with **no base class**: it is not a `ModulationSource`.<br>• `prepare(double sampleRate)` (83), `[[nodiscard]] float process() noexcept` (114), `setWaveform(Waveform)` (189), `setFrequency(float hz)` (218), `setPhaseOffset(float degrees)` (226), `setTempoSync(bool)` (237), `setTempo(float bpm)` (246), `setNoteValue(NoteValue, NoteModifier = NoteModifier::None)` (255), `retrigger()` (272), `setRetriggerEnabled(bool)` (289), `setFadeInTime(float ms)` (295). |
| `BrownianDrift` | `processors/brownian_drift.h:94` | DRIFT | `class BrownianDrift : public ModulationSource`.<br>• `kControlRateInterval = 32` (105), `kDefaultDriftSeed = 0xB17Eu` (109).<br>• `setSeed(std::uint32_t)` (145), `setSmoothness(float normalized)` (152), `setDepth(float normalized)` (159), `processBlock(size_t numSamples)` (194), `[[nodiscard]] float getCurrentValue() const noexcept override` (212). |
| `ModulationSource` | `core/modulation_source.h:31` | Conformance of the Motion output | `[[nodiscard]] virtual float getCurrentValue() const noexcept = 0;`, `[[nodiscard]] virtual std::pair<float, float> getSourceRange() const noexcept = 0;`. |
| `VoiceModRouter` | `systems/voice_mod_router.h:58` | Named by the roadmap as the Motion bus (see finding F-1) | `kMaxRoutes = 16` (61), `void setRoute(int index, VoiceModRoute route) noexcept` (87).<br>• `void computeOffsets(float env1, float env2, float env3, float lfo, float gate, float velocity, float keyTrack, float aftertouch) noexcept` (154), `[[nodiscard]] float getOffset(VoiceModDest dest) const noexcept` (213).<br>• The destinations are a fixed enum, `VoiceModDest { FilterCutoff, FilterResonance, MorphPosition, DistortionDrive, TranceGateDepth, OscAPitch, OscBPitch, OscALevel, OscBLevel, SpectralTilt }` (`systems/voice_mod_types.h:50–62`). |
| `VoragoMacroMatrix` | `systems/vorago_macro_matrix.h:264` | **Pattern only** for `ProfundumMacroMatrix` | `struct VoragoMacroRow { VoragoMacro macro; VoragoMacroTargetOwner owner; VoragoMacroTarget target; float base; float amount; ModCurve curve; }` (233–255).<br>• `void setMacro(VoragoMacro, float) noexcept` (904), `void setMacros(const VoragoMacroValues&) noexcept` (987), `static constexpr float neutralFor(VoragoMacro) noexcept` (889), `void apply(VoragoEngine& engine) const noexcept` (1061).<br>• Header ruling FR-067: "calling apply() every block with unchanged macro values must step NOTHING". AR-6: "what is NORMATIVE is each row's DIRECTION and the per-macro minimum end-to-end effect size". |
| `SeraphisMacroMatrix` | `systems/seraphis_macro_matrix.h:182` | Pattern cross-reference | `enum class SeraphisMacro : std::uint8_t { Dream = 0, Bloom, Dissolve, Gravity, Entropy, Count }` (50), `struct SeraphisMacroRow` (159). |
| `VoragoEngine` / `VoragoVoice` | `systems/vorago_engine.h:219`, `systems/vorago_voice.h` | **Pattern only**: composition, seeds, determinism harness | `kMaxVoices = 6` (231), `kDefaultPolyphony = 4` (234).<br>• `void setPolyphony(std::size_t n) noexcept` (589), `void setSeed(std::uint32_t seed) noexcept` (626), `void noteOn(std::uint8_t note, std::uint8_t velocity) noexcept` (671), `void noteOff(std::uint8_t note) noexcept` (708).<br>• Per-voice seeds `deriveStreamSeed(seed_, kVoiceSaltBase + v)` (370; `core/random.h:102`).<br>• `vorago_voice.h:34–37` AR-2: "No ModulationEngine, no VoiceModRouter, no ModulationSource virtual dispatch on the audio thread".<br>• `vorago_voice.h:56–64` D3 / D4: a steal is issued between blocks, so the last served sample is captured and faded. |

### Test infrastructure reused

| Item | Location | Use |
|---|---|---|
| `render_fingerprint.h` | `tests/test_helpers/render_fingerprint.h` | Determinism fingerprints with measured tolerances (no bit-exact float goldens) |
| `renderMorphChain` / `renderMorphPoly` | `dsp/tests/unit/systems/bass_morph_test_helpers.h:679, 687` | The Phase 8 per-interval fixture. It is the reference for the voice's call order (Phase 8 spec FR-070 steps 1–10, lines 732–753) |
| `showcasePair()` | `dsp/tests/unit/systems/bass_morph_state_helpers.h:167` | The documented showcase setting for the audibility table (Phase 8 FR-071) |
| `unbiasedH1Amplitude` | `dsp/tests/unit/systems/mass_dynamics_test_helpers.h:288` | Test-side reference for FR-005. **Test-only today** (finding F-2) |
| `NoteBehaviourHarness` | `dsp/tests/unit/systems/profundum_note_behaviour_test_helpers.h:1–13` | "Phase 9's ProfundumVoice replaces it" (lines 11–12). Reference for the note-behaviour matrix |
| `VoragoEngine_DeterminismHarness` | `dsp/tests/unit/systems/vorago_engine_test.cpp:1502` | Determinism-harness template (seeded schedule `kDeterminismSchedule`, line 1418) |
| Phase 7 mono-sum helpers | `dsp/tests/unit/systems/bass_stereo_field_test_helpers.h:95, 97` | `kSubBandQBarDb = 1.0`, `kSubBandRhoBar = 0.99` |

### Findings from header verification (each has a drafted resolution in "Decisions")

- **F-1 `VoiceModRouter` does not fit the Motion bus as shipped.**
  - Its source inputs are Ruinae's eight (`computeOffsets(env1, env2, env3, lfo, gate, velocity, keyTrack,
    aftertouch)`, voice_mod_router.h:154).
  - Its destinations are a fixed Ruinae enum with no Mass, Depth, Body, Edge or Shift (voice_mod_types.h:50–62).
  - Adding them would be a shared-header change to a Ruinae engine component.
  - Both precedent voices exclude it on the audio thread: Vorago AR-2 (vorago_voice.h:34–37), and Seraphis lists it
    among "the members FR-002 forbids" (seraphis_voice.h:195).

  The roadmap's wording (line 666) and the headers conflict. Decision D-2 drafts a resolution for the clarify stage.
- **F-2 The Anchor's unbiased h1 exists only as a test helper.** Phase 6 G8-3 rules that the Anchor is fed the recipe's
  h1 for the voice's own controls *before* the Mass biases (roadmap line 482). The only implementation is
  `MassDynamicsTest::unbiasedH1Amplitude` (mass_dynamics_test_helpers.h:288–292). It calls
  `SpectralShapeRecipe::evaluate` over 64 partials and takes `g[0] · kCenterPanGain`. The voice must compute it in
  shipped code, once per interval (FR-005), and its CPU cost is part of SC-001 / SC-002.
- **F-3 The Phase 4 harness used a superseded a₁.** `NoteBehaviourHarness` feeds `deliveredGains()[0] · kCenterPanGain`
  (profundum_note_behaviour_test_helpers.h:22–23, "tasks.md deviation 6"). Phase 6 G8-3 (2026-10-17) replaced that
  with the unbiased h1. The two agree at Mass 50 (mass_dynamics_test_helpers.h:286–287). FR-005 follows G8-3.
- **F-4 Only one Motion source is a `ModulationSource`.** `BrownianDrift` derives from it (brownian_drift.h:94).
  `LFO` (lfo.h:63) and `ADSREnvelope` do not. The cross-cutting rule "modulation sources conform to the existing
  ModulationSource concept" is met by the Motion section's *output* (FR-048), not by each internal source.
- **F-5 `MultiStageEnvelope` cannot realise `AmpEnvelopeFields` as is.** The fields' curves are float amounts in
  [−1, +1] (morph_state.h:76–78). `MultiStageEnvelope::setStage` takes an `EnvCurve` enum (multi_stage_envelope.h:166).
  Choosing it would make three fields unrealisable, and they would have to be recorded as deviations (Phase 8 FR-083,
  handoff 9.6).
- **F-6 Two different scopes both declare a `VoiceState`.** One is the namespace-scope `enum class VoiceState`
  (voice_allocator.h:43). The other is the nested `BassStereoField::VoiceState` struct (bass_stereo_field.h:113). This
  is not an ODR clash. A TU that includes both must still qualify each use.
- **F-7 The globals cost more than the roadmap estimated.** Phase 6 measured `MassDynamics` at 0.991 %, and
  `DCBlocker` ×2 + `TruePeakLimiter` at 1.23 % (roadmap line 489). The field stage costs 0.07–0.10 % (line 537)
  and the morph 0.036 % (line 593). Together that is ≈ 2.36 %, against the roadmap's "globals ≈ 1.5 %" (line 679).
  - The per-voice lines sum to ≈ 3.93 %: core + excitation + Fold 1.708 % (line 280), Anchor 0.141 % (line 322), Drop
    0.0286 % (line 376), Shape + Character 1.85 % (line 417) and field law 0.198 % (line 536). This is below the
    roadmap's ≈ 4.8 %.
  - The predicted whole engine is ≈ 6.3 % MONO and ≈ 18.1 % at 4 voices, inside 8 % / 25 %. The prediction leaves out
    the amp envelope, Motion, the macros and the FR-005 h1 evaluation.
  - The figures come from different builds and lanes, so they size the work and are not evidence. SC-001 / SC-002
    measure.

---

## New components

ODR sweep, run 2026-10-19 at `0ae33b87`: `grep -rnE "(class|struct|enum class|enum|using) <Name>\b" dsp/ plugins/ tests/`.
A second pass, `grep -rnw "<Name>" dsp/ plugins/ tests/`, counts any mention.

| Class / type | Layer | Header | ODR sweep result |
|---|---|---|---|
| `ProfundumVoice` | 3 | `systems/profundum_voice.h` | 0 definitions. 1 mention, a comment at `dsp/tests/unit/systems/profundum_note_behaviour_test_helpers.h:11` ("Phase 9's ProfundumVoice replaces it"). Clear. |
| `ProfundumEngine` | 3 | `systems/profundum_engine.h` | 0 definitions, 0 mentions. Clear. |
| `ProfundumEngineConfig` (prepare-time config, drafted, `VoragoEngineConfig` pattern) | 3 | `systems/profundum_engine.h` | 0. Clear. |
| `ProfundumMotion` (drafted, D-3) | 3 | `systems/profundum_motion.h` | 0 definitions, 0 mentions. Clear. |
| `ProfundumMacroMatrix` | 3 | `systems/profundum_macro_matrix.h` | 0 definitions, 0 mentions. Clear. |
| `ProfundumMacro`, `ProfundumMacroTarget`, `ProfundumMacroRow`, `ProfundumMacroValues` | 3 | `systems/profundum_macro_matrix.h` | 0 each. Clear. |

Hazards re-checked on the same date. Each is a real class, and none of these names is used:
- `CharacterProcessor` (`systems/character_processor.h:85`);
- `SubOscillator` (`processors/sub_oscillator.h:111`);
- `StereoField` (`systems/stereo_field.h:73`);
- `MorphEngine` (`plugins/disrumpo/src/dsp/morph_engine.h:45`);
- `TransientDetector` (`processors/transient_detector.h:34`);
- `MacroMapper` (`plugins/membrum/src/processor/macro_mapper.h:29`). It is a near-name of the macro matrix, so the
  matrix keeps the `Profundum` prefix.

Helper names (nested structs, test helpers in `namespace Krate::DSP::ProfundumTest` with a `voiceEngine` / `VoiceEngine`
prefix) are swept at plan time.

---

## Functional Requirements

### FR-001 series — `ProfundumVoice`

- **FR-001 Component.** `ProfundumVoice` lives in `systems/profundum_voice.h`. It is Layer 3, header-only and
  `noexcept` on the audio path. It includes Layers 0–2 and Layer 3 peers only, and no `effects/` header. It owns one
  of each of the following:
  - `ProfundumCore`, `ShapeMorph`, `HarmonicFold`, `BassCharacterStage`, `AnchorOscillator`;
  - `DropEnvelope`, `DropPhaseLock`, `NoteMemory`, `RetriggerDeclick`, the amp envelope (FR-009);
  - a `BassStereoField::VoiceState`;
  - fixed scratch sized at `prepare`.

  KDD 3, line 134: "Every voice owns its oscillator state, Anchor, amplitude and pitch envelopes and voice-level
  processing".
- **FR-002 Chain order** (roadmap lines 655–657). The main path is core (+ edge excitation) → `ShapeMorph` →
  `HarmonicFold` → `BassCharacterStage`, in that order. The Anchor runs in parallel and **joins after Character** by a
  plain float add. There is no compensation or normalisation stage at the join (Phase 3 FR-025, SC-024(d); roadmap
  lines 693–695). **OQ-8 resolved by the main loop at clarify (2026-10-19):** the join point and the Anchor's phase
  reference (locked to core h1, Phase 3) are **unchanged** by Phase 8's finding 9.10. The regenerated fundamental's
  phase rotates with settings and note, so no fixed alignment on the Anchor's reference could hold across the
  surface; SC-017(b)/(c) bound and predict the summed fundamental instead, and the per-cell ρ / θ are recorded for
  Phase 13's audition.
- **FR-003 `anchorFoldFeed`** (lines 659–661; Phase 3 FR-067).
  - The send g ∈ [0, 1] (default 0) adds g · Anchor into the `HarmonicFold` input, which is the `ShapeMorph` output.
  - The **direct Anchor path is always present**.
  - The Anchor renders **once** per block, and that one render is used for both the feed and the direct path.
  - The voice owns a per-sample smoother for g. Phase 8 handoff 9.5: "No Anchor-feed smoother exists in the shipped
    components … the Phase 9 voice owns a fold-feed smoother". Drafted: a linear ramp across each 32-sample interval,
    as in the Phase 8 fixture.
  - g reaches the voice from the resolved `MorphState::anchorFoldFeed`.
- **FR-004 Core isolation.** Two outputs are bit-identical between a voice with the Anchor attached and one with it
  detached: the core's own output (the bank after excitation) and, at g = 0, the main path up to the join (roadmap
  lines 693–694). The core feeds the Anchor only through `setCoreH1Amplitude`, `setFrequency` / `noteOn` and
  `setFollowRatio`.
- **FR-005 Anchor amplitude input.** Once per interval, the voice computes a₁ = `SpectralShapeRecipe::evaluate`(the
  voice's recipe before the Mass Depth / Body biases, f, fs)[0] · `kCenterPanGain`, and passes it to
  `AnchorOscillator::setCoreH1Amplitude` (Phase 6 G8-3; findings F-2, F-3).
  - The "voice's recipe before Mass" is the resolved morph recipe plus macro and Motion offsets (D-4).
  - f is the Anchor's own pitch input (FR-006).
  - The computation is RT-safe: a fixed-size stack array of `ProfundumCore::kDefaultPartials` floats.
- **FR-006 Pitch routing** (roadmap lines 689–692; Phase 3 Q3, Q4; Phase 4 FR-055–FR-058).
  - f_res is the note pitch after glide and pitch bend.
  - The Drop runs `DropEnvelope` → `DropPhaseLock` → f_core = f_res · 2^(d_c / 12) per sample. **The core, `ShapeMorph`
    and `BassCharacterStage` read f_core.**
  - The **Anchor reads f_res in LOCK, PUNCH and GHOST** and **f_core in FOLLOW**.
  - `DropPhaseLock::setBypass(true)` in FOLLOW (Phase 4 SC-026).
  - Drop parameters are latched at trigger, so morph and Motion changes act on the next note (Phase 8 handoff 9.9).
  - `HarmonicFold::setFundamentalHz` is set once per interval from f_core.
- **FR-007 FOLLOW ratio.** The FOLLOW ratio r is engine-derived and never taken from processed audio (Phase 3 Q1;
  Phase 5 handoff 3, lines 428–429).
  - r = `BassCharacterStage::perceivedPitchRatio()` (bass_character_stage.h:467).
  - It is forwarded per interval through `setFollowRatio`, or per sample through `followRatioPerSample` if the plan
    finds the interval form too coarse.
  - **G14-13 (user ruling 2026-10-21): a changed ratio is handed to the Anchor only after it has held for
    `kFollowRatioDwellPeriods` = 4 consecutive periods of f_res** (profundum_voice.h step 6; 194 ms at E0, 48 ms at
    E2). The FR-086 follow-ratio tap keeps logging the measured ratio every interval (SC-018's event), `issued`
    marks the handoff. Why: with the Anchor fed into the Fold the FR-008 meter saw the Anchor's own even harmonic at
    ratio 2 and the ratio flipped every period (SC-021 row, G14-13 (1)).
  - `ShapeMorph` and the Fold contribute r ≡ 1, which is Phase 3's recorded law for the core, excitation and Fold
    (Phase 3 spec line 433).
- **FR-008 Phase sync and Rectify input** (Phase 5 handoff (2) and (4), lines 427–431).
  - **Source of `coreCycles`.** `BassCharacterStage::syncPhase(float coreCycles)` wants the core's h1 phase in cycles
    at the next sample processed (bass_character_stage.h:295–302). Neither `ProfundumCore` nor
    `HarmonicOscillatorBank` exposes an h1 phase accessor, and the voice keeps no h1 phase clock of its own. The voice
    therefore calls `syncPhase(0.0f)` only where the core's phase is 0 by construction:
    - at every note event whose resolved `Actions` restart the core under `PhasePolicy::Reset` (FR-010), on the
      sample the restart takes effect (the dip's switch point when a declick is armed, FR-011);
    - at the first note-on after the voice's `prepare` / `reset`, where the core starts from its post-reset phase.
      SC-033 verifies that the core's h1 phase is 0 there under both policies. If it is not under FreeRunning, the
      plan rules the alternative into this row before build.

    This is the Phase 8 fixture's call at a Reset note-on (bass_morph_test_helpers.h:572–576).
  - **No other call.** A FreeRunning retrigger, and an algorithm or Amount change that takes the stage out of bypass,
    are served by the stage's own clock. After the first `syncPhase`, `syncCycles_` integrates f0 / fs every sample,
    bypassed or not, so "a switch or a bypass exit keeps the core's phase" (bass_character_stage.h:298–300). The
    voice has no hook for a bypass exit and needs none.
  - Once per interval it sets `setInputHalfWaveSymmetric(true)` while the Character input's h2 is below −20 dB re h1.
    The detection method is ruled in plan; the recorded uncertainty band is −24 … −16 dB.
  - **G14-13 (user ruling 2026-10-21): the meter reads the Shape output mid BEFORE the Anchor feed is added**
    (profundum_voice.h step 5b), not the Fold output. The Fold is odd-symmetric (sine fold, soft saturation, a gain)
    and preserves half-wave symmetry; the one term that broke it on the Fold output was the Anchor itself at
    FOLLOW ratio 2 — an even harmonic of f_res — so the meter fed the ratio back into itself (flag → ratio → Fold
    input → flag) and flipped every period on the hot FOLLOW cells (SC-021 row). Measured: a 4-period dwell on the
    handoff alone (FR-007) slowed the cycle to 5 periods and started it on the 48 kHz E2 FOLLOW cell; the pre-feed
    tap with the dwell reads 0 flips on every cell, SC-033 and SC-018 green. The Character's rectifier therefore
    follows the voice's own waveform's symmetry; the Anchor's contribution never votes.
- **FR-009 Amp envelope** (OQ-1, OQ-6; **ruled at clarify Q6, 2026-10-19**).
  - The class is **`ADSREnvelope`**. It realises all seven `AmpEnvelopeFields` one-to-one, including the float
    curves (F-5); no field is a deviation. `MultiStageEnvelope` was rejected because its curves are an enum.
  - **Table path, always.** The three float curve setters (`setAttackCurve(float)`, `setDecayCurve(float)`,
    `setReleaseCurve(float)`, adsr_envelope.h:160–178) are called at prepare with the resolved values, and after that
    only when the resolved value changed bitwise (Phase 8 handoff 9.3). The envelope therefore renders on its
    256-entry table path for every curve value, **0.7 included**; the `EnvCurve` enum path is never used, so a morph
    across 0.7 crosses no path boundary. The neutral curve of handoff 9.3 is the table path at 0.7. The determinism
    references (SC-011) and the AT-24 / AT-25 rows are taken on this path.
  - **Immediate receivers.** Time and sustain changes apply immediately, with no smoother in the voice (Phase 8
    handoff 9.4); the step they produce under a morph is measured and recorded in SC-006's and SC-010's CSVs, not
    gated. A later ruling may add a smoother; this phase ships without one.
  - The gain multiplies the Character output **and** the Anchor before the sum (Phase 8 FR-070 fixture), so the
    Anchor is released with its voice. The exact voice-output expression is FR-015's.
- **FR-010 Note Memory.** Every note event is classified as a `NoteMemory::Transition` and resolved with
  `resolve(t, voiceAudible)`, where "audible" means the amp envelope is not idle (Phase 4 spec line 849). The voice
  then executes the `Actions` it gets back:
  - core restart under `phase`;
  - Anchor re-arm;
  - amp-envelope restart;
  - Drop trigger (snap or bridged);
  - declick.

  `NoteMemory::PhasePolicy`, `ProfundumCore::RetriggerPhase` and `AnchorOscillator::RetriggerPhase` are mapped by
  `static_cast` under `static_assert`s that the values agree (Phase 3 D-6, a recorded Phase 9 obligation).
- **FR-011 Declick.** When `Actions::declick` is set, the voice arms its `RetriggerDeclick` and applies the phase
  reset at the dip's switch point (`samplesUntilSwitch`). **Dip length (ruling R-B, 2026-10-20): `ProfundumVoice::kDeclickMs`
  = 3 ms**, through the append-only `RetriggerDeclick::prepare(fs, declickMs)`; Phase 4's 1.5 ms default and every
  existing caller are bit-identical. Probe P-2 measured Phase 4's 1.5 ms dip 7–51 % steeper than the SC-005(a) /
  SC-006(a) no-steal reference on clean E1 notes (ratios 1.07 / 1.11 / 1.29 / 1.52 against 1.059) with (b) passing
  everywhere; 3 ms halves the slope. Gating (b) only and changing Phase 4's constant were rejected. **Switch-sample
  chain reset (defect fix from the P-2 locator, 2026-10-20):** at the switch sample, before the transition is applied,
  the voice resets its `ShapeMorph`, `HarmonicFold` and `BassCharacterStage` state. The dip mutes the voice output
  there, but the chain's own memory (filters, oversamplers, DC blockers) still held the old note, and it reappeared at
  full level when g_d returned to 1 (a 0.28 step on the sum at E1, every P-2 cell failing (a) and (b)); after the
  reset the new note enters a silent chain as the first note did (max step 0.0099, (b) margin ≥ 1.4×).
- **FR-012 Stereo-field forwarding per voice** (Phase 7 handoff §2). These steps run in each interval, after the
  engine's `BassStereoField::advanceControl()`:
  1. Any note-on at this grid point runs first, including `VoiceState::reset()` under the Reset policy.
  2. `computePanVector(voiceState, f_core, pan, N)` → `ProfundumCore::setPartialPanOffsets(pan)`.
  3. `wideOffsets(algorithm)` → `BassCharacterStage::setChannelOffsets(amount, tone)`.

  The Anchor never enters any of this (KDD 2).
- **FR-013 Per-interval setter order.** The voice applies the resolved, macro-offset, Motion-modulated, Mass-biased
  state in the Phase 8 fixture order (Phase 8 spec FR-070 steps 3–10, lines 740–750), **with the Drop setters moved
  to the head of the interval (G14-3 defect fix, main loop 2026-10-21):**
  - Drop setters (latched by the component), ahead of the pitch stage, so that a transition's Drop trigger — deferred
    to the same point — fires with this interval's amount / time / shape / rebound: a voice that was idle has not run
    its setters since the state changed, and the POLY soak's lock notes dropped by the activity's stale amount;
  - core controls + excitation;
  - Fold amount, SHAPE;
  - Character algorithm (only when it differs from the last request), Amount, Tone;
  - Anchor controls with the Mass-biased level;
  - amp envelope (changed values only).

  A setter is never called with an unchanged value where the component re-arms on a call. Vorago's FR-067 idempotence
  rule applies (vorago_macro_matrix.h header).
- **FR-014 Placement in POLY** (OQ-2, roadmap OQ-7; **resolved by the main loop at clarify, 2026-10-19, measurement
  criterion fixed now, value written at build**). Fold, Shape and Character run **per voice**, "so chords do not
  intermodulate" (line 659), and FOLLOW is evaluated per voice. That is the shipped placement unless SC-004 shows,
  under SC-002's protocol, that the per-voice engine **breaches 25 %** at 4 voices while the post-sum alternative
  meets it; only then is the post-sum placement adopted for the stages that make the difference, and the ruling is
  written here with the two readings. FOLLOW is handled the same way on its own: per voice unless its per-voice cost
  alone is what breaches SC-002. SC-004 records both readings in every case. The ceiling is never the lever.
  **Readings so far (compliance remediation, 2026-10-21):** probe P-1 under SC-002's protocol on the final bits
  (`evidence/run-log/close/p1-t030-final.log:15–16, 123`) reads per voice 23.03 % (Tube) / 23.93 % (Fracture) and
  post-sum 28.30 / 28.37 % at 4 voices. Per voice does not breach 25 %, so the adoption condition is false and
  **per voice ships** (`ProfundumEngineConfig::placement` default `PerVoice`, profundum_engine.h:85). The post-sum figure
  is an upper bound, not a realised post-sum cost: the engine's `PostSum` variant still runs every voice's Shape / Fold /
  Character and discards the output (profundum_engine.h:34–37). Because the ruling turns only on the per-voice reading,
  that bound does not change it. **Closed (main loop, 2026-10-21; `evidence/close/cpu-lane.sh` run alone by the comply
  stage, run-log/close/cpu-lane.log:143–144; `evidence/placement.txt`):** `VoiceEngine_PlacementCost` read **per-voice
  max 22.862 % / post-sum max 27.089 %** of one core at 4 voices (bar 25 %) — the criterion "per-voice > bar AND
  post-sum ≤ bar" does not hold, so **per voice is kept**; FOLLOW read **per voice 22.862 % / off 23.161 %** (cost
  −0.30 %, within the reading's noise), so **FOLLOW stays per voice**. The default `PerVoice` (profundum_engine.h:85)
  and the PostSum caveat (:34–37) stand.
- **FR-015 Voice output and retirement.**
  - The voice **adds** its stereo output into the engine's sum buffers. The components themselves overwrite their
    buffers (Phase 3 FR-007), so the voice renders into its own scratch.
  - Per channel and per sample, the voice's contribution is `out = (c · g_amp) · g_d + (a · g_amp) · g_d`, evaluated
    in that float order (each product rounded, then the sum). Here c is the Character output, a the Anchor output,
    g_amp the amp-envelope gain, and g_d the product of the `RetriggerDeclick` dip (FR-011) and the steal fade
    (FR-016), which is 1.0f when neither is active. No other stage lies between the join and the sum. The plan may
    fix a different operation order. If it does, it writes that order here, and SC-019(b) asserts it.
  - A voice is idle when its amp envelope is idle. The engine then calls `VoiceAllocator::voiceFinished` in POLY.
- **FR-016 Steal fade.** A hard steal (`VoiceEvent::Type::Steal`) or a MONO note replacement that restarts the voice
  must be clickless (SC-005, SC-006). Drafted mechanism, ruled in plan (D-8): the `RetriggerDeclick` dip for restarts,
  and for steals a short fade of the old content captured at serve time (the Vorago D3 / D4 pattern,
  vorago_voice.h:56–64).

### FR-020 series — `ProfundumEngine`

- **FR-020 Component.** `ProfundumEngine` lives in `systems/profundum_engine.h` (Layer 3).
  - `prepare(double sampleRate, const ProfundumEngineConfig&)` sizes everything. It may be called again. It is the
    only configuration-time entry point.
  - `reset()` returns to the post-prepare state.
  - The audio-thread surface is note-on / note-off, pitch bend, host tempo, setters and `processBlock(float* left,
    float* right, std::size_t numSamples)`, which overwrites the buffers.
  - The audio-thread surface is `noexcept` and allocation-free.
- **FR-021 MONO (default).** `MonoHandler` drives one voice and implements:
  - priority (`MonoMode`);
  - legato (`setLegato`);
  - portamento mode, time and `GlideMode` (ConstantTime / ConstantRate / Quantized);
  - note replacement and the return to a held note.

  ~~`MonoNoteEvent::retrigger` and the Note Memory transition decide the voice's actions (FR-010).~~ **Amended by
  G14-9 (user ruling 2026-10-21):** the Note Memory transition (FromSilence / Overlap / Tail, from the voice's
  audibility and whether a note was held) and the Note Memory policy decide the voice's actions (FR-010);
  `MonoHandler`'s legato bit gates its own `PortaMode::LegatoOnly` glide only and its `retrigger` bit is not
  consulted (profundum_engine.h `applyNoteOn`), so Phase 4's policy is the single source of retrigger behaviour
  and the two legato controls cannot fight. Consulting the bit as well was rejected.
- **FR-022 POLY.** POLY uses `VoiceAllocator` with `AllocationMode::Oldest`. The steal order is oldest released, then
  oldest active, which is shipped behaviour (voice_allocator.h:631–638, 672–684). Polyphony is 4 by default.
  **`ProfundumEngine::kMaxVoices = 8` (ruled at clarify Q8, 2026-10-19; roadmap OQ-8).** Per-voice storage and the
  allocator are sized for 8; the active polyphony is a setter in [1, 8], default 4. SC-002's 25 % ceiling stays at 4
  voices; one CPU point at 8 voices is **recorded, not gated** (SC-002). Phase 11 registers the polyphony range 1–8
  from this value and never widens it (FR-085). Glide in POLY is out of scope unless the plan
  shows `MonoHandler`'s per-voice use is free (drafted: no POLY glide).
- **FR-023 MONO ↔ POLY switch.** Drafted behaviour:
  - a mode switch releases every sounding voice through its amp envelope, so no note hangs and none is cut;
  - new notes use the new mode;
  - MONO / POLY and the image mode are independent axes (KDD 3).
- **FR-024 Pitch bend.** Pitch bend uses `NoteProcessor` (`setPitchBend`, `setPitchBendRange`, `processPitchBend`)
  unchanged (Phase 4 spec line 126: "Phase 9 composes the existing component unchanged"). The bend applies to f_res in
  both modes. The range is registered in Phase 11.
- **FR-025 Global chain** (line 664; Phase 7 handoff §2). The order is voice sum → `MassDynamics` →
  `BassStereoField::processBlock` (safety stage) → `DCBlocker` ×2 (L, R) → `TruePeakLimiter`.
  - ~~The engine reports `latencySamples() == 0`, the sum of its zero-latency stages.~~ **Amended by G14-1 (user
    ruling 2026-10-21):** the engine reports `latencySamples() == kLimiterLookaheadSamples` (64 samples at every rate,
    1.33 ms at 48 kHz) once prepared — the `TruePeakLimiter`'s opt-in look-ahead, the chain's only latency; every
    other stage stays zero-latency (the `static_assert`s on `MassDynamics` and `BassStereoField` stand). Phase 10's
    processor forwards it to the host.
  - The output ceiling is the limiter default (−1 dBTP), settable.
- **FR-026 Control grid and call order.** All grid work runs on the shared 32-sample grid (every component's
  `kControlInterval` is 32). At each grid point the order is:
  1. Apply the due events (notes, setter changes) stamped at or before this sample.
  2. `BassMorphEngine::advanceInterval()` **exactly once** (Phase 8 handoff 9.1).
  3. `ProfundumMacroMatrix` offsets on the resolved state (FR-053).
  4. Motion: advance the sources and compute the target offsets (FR-043).
  5. `MassLaw`: set the base (the FR-054 split, FR-027), set the Mass, `advanceInterval()`.
  6. `BassStereoField::setControls(resolved.field)`, then `advanceControl()`.
  7. Per voice, FR-012 then FR-013.
  8. The `MassDynamics` setters from the Mass targets, and `setDetectorKeyHz(lowestSoundingF0)` (0 = no voice → 80 Hz;
     Phase 6 handoff (5)).

  The order in steps 2–8 is normative. The plan may move only steps whose outputs are independent, and must say which.
- **FR-027 Mass wiring** (Phase 8 handoff 9.2; Phase 6 handoffs (1), (2)).
  - `MassLaw::setBase` takes depth / body from the resolved recipe after the macro and Motion offsets, `foldAmount`
    from the resolved Fold, and `anchorLevel` from the resolved `anchor.level`. The Mass-driven bases stay at their
    `Targets{}` defaults.
  - `setMass` takes the resolved `mass` plus the macro and Motion contributions.
  - The Anchor's level is `MassLaw::evaluateRow(AnchorLevel row, m_a, anchor.level)` per interval (the row clamps
    its Mass argument). **m_a (ruled at clarify Q5, 2026-10-19; D-6):**
    - In Anchor morph **Blend** and **Punch**: m_a = `resolved.anchorMass` + that interval's Motion → Mass offset
      (the same value FR-043 adds to the Mass input), so a swelling bass swells its sub too.
    - In Anchor morph **Lock**: m_a = `resolved.anchorMass`. Motion never reaches the Anchor's level, which stays at
      its p = 0 value whatever Motion does; Lock's "constant Anchor" (Phase 8 Q5) is literal.
    - "Always" and "never" were rejected. SC-027(b), (c) and (d) gate this.
  - All seven targets are applied, including `OutputTrim` → `MassDynamics::setOutputTrimDb`.
  - The transient attack / sustain gains come from the resolved `transientAttackDb` / `transientSustainDb`.
- **FR-028 Host context.** The host tempo feeds `BassStereoField::setTempoBpm` and the CYCLE `LFO::setTempo`. If the
  host gives no tempo, the field's `kDefaultTempoBpm` (120) is used.
- **FR-029 SOLID re-synchronisation** (Phase 7 handoff §2; OQ-7; **ruled at clarify Q7, 2026-10-19**). After a
  switch into SOLID, the global DC blockers and the true-peak limiter carry L ≠ R history for ≤ 242.708 ms (measured).
  The engine **re-synchronises them at the crossfade end**: at the first grid point at which the field reports the
  SOLID crossfade complete, the right DC blocker is set to the left's state (dc_blocker.h:114–117, 165) for both
  blockers. **The limiter needs no copy (plan C-1, ratified by the main loop 2026-10-20):** `TruePeakLimiter` computes
  one shared gain from max(|L|, |R|) over both channels' upsampled peaks and applies it to both channels
  (true_peak_limiter.h:142–164), so L == R inputs give L == R outputs whatever the upsamplers' history; no shared
  header changes for it. Accepting the convergence was rejected. From the DC-blocker copy **plus the limiter's look-ahead delay** (G14-1,
  2026-10-21: `kLimiterLookaheadSamples` = 64, the FR-025 latency; the delay line holds the last 64 pre-copy L ≠ R
  samples — the SC-015 smoke read exactly 64 differing samples after the copy and none after the latency) onwards the
  output is L == R bitwise (SC-015), with no settle time.
- **FR-030 Morph surface.** The engine owns one `BassMorphEngine` and passes its control surface through: `setEndpoint`,
  `setPosition`, `setPositionRampMs`, `setAnchorMorphMode`, `serializeRuntime` / `restoreRuntime`. Every voice reads the
  same `Resolved` in each interval (KDD 4: "affecting all voices together").
- **FR-031 Non-morphed controls.** These are engine setters outside `MorphState` (Phase 8 excluded "note behaviour,
  output level, polyphony", roadmap line 620):
  - voice mode (MONO / POLY), polyphony, MONO priority, legato, portamento time / mode, glide mode;
  - Note Memory mode and Retrigger;
  - pitch-bend range;
  - output ceiling;
  - the four macro values;
  - the seed.
- **FR-032 Seeds.** One engine seed derives every random stream through `deriveStreamSeed` (core/random.h:102). The
  streams are DRIFT and the field's Drift (`setDriftSeed`), plus a fixed salt per stream. This follows the Vorago
  pattern (vorago_engine.h:370). Per-voice streams are derived the same way, with a voice salt.

### FR-040 series — Motion section

- **FR-040 Sources** (lines 665–667; concept §11).
  - **ENV** is one flexible envelope with attack / decay from `motion.envAttackMs` / `envDecayMs`.
  - **CYCLE** is `LFO`, free-running or tempo-synced, configured from the `cycle*` fields.
  - **DRIFT** is `BrownianDrift` with `driftSmoothness`.
  - The resolved `motion.source` selects **one active source**: Off / Env / Cycle / Drift. This matches the
    `MotionConfig` single-source shape (morph_state.h:82–87) and the Phase 12 main-page "source mode only (OFF / ENV /
    CYCLE / DRIFT)" (line 801).
- **FR-041 Targets.** The fixed destination set is `MotionConfig::Target { Mass, Depth, Body, Edge, Shift }`
  (morph_state.h:83). It is "Mass and the core spectral controls first" (line 741). There is no general matrix.
- **FR-042 Depth, amount, scale and polarity** (**ruled at clarify Q2, 2026-10-19**). Offset = source value (after
  `polarity`) × `amount` × `depth`. `depth` is the global **Motion Depth** (line 667). Depth 0 is an exact
  identity: the render is bit-identical to Motion Off (SC-026).
  - **Half-span scale.** |offset| = 1 moves the target by **half its range**: Mass ±50 (of 0–100), Depth / Body /
    Edge ±0.5 (of 0–1), Shift ±1 (of −1…+1). A centred full-depth bipolar swing therefore covers the target's range
    exactly, with no clamp plateau. Full-span was rejected (half of every swing would sit on the clamp).
  - **Polarity.** Bipolar: LFO and Drift pass natively in −1…+1 and ENV (0–1) is mapped 2e − 1. Unipolar: ENV passes
    natively and LFO / Drift are mapped (x + 1) / 2. The same mapping applies to every source and target.
  - SC-009's and SC-014's readings are stated against this scale.
- **FR-043 Application domain.**
  - Spectral targets add their offset in the recipe domain to the macro-offset resolved control, before the Mass
    biases. The result is clamped to the control's range, and the core's own per-interval step ceiling still applies
    (profundum_core.h:142–147).
  - The Mass target adds its offset to the Mass input (FR-027). It passes through `MassLaw`'s 40 ms traversal slew, so
    CYCLE rates above the slew's reach are attenuated. That is recorded (SC-009), not hidden.
- **FR-044 Motion → Mass** (line 668; concept "basses that physically swell and contract"). The Mass target is the
  required one. Its swell is measured (SC-009).
- **FR-045 ENV in POLY.** Mass is global. Drafted D-5: ENV is one engine-level envelope, retriggered by every note
  event whose Note Memory actions restart the amp envelope. The spectral targets use the same global value in every
  voice.
- **FR-046 CYCLE configuration.** The LFO calls are:
  - `setWaveform(cycleShape)`, `setFrequency(cycleRateHz)`, `setTempoSync(cycleSync)`;
  - `setNoteValue(cycleNote, cycleModifier)`, `setPhaseOffset(cyclePhaseDeg)`;
  - `setRetriggerEnabled(cycleRetrigger)`, with `retrigger()` at an envelope-restarting note event;
  - `setFadeInTime(cycleFadeInMs)`.

  Each is called on change only.
- **FR-047 `MotionConfig` realisation.** Every `MotionConfig` field is realised, or recorded here as a deviation
  (Phase 8 handoff 9.6). Adding a field is append-only under a `kMorphStateFormatVersion` bump (Phase 8 FR-007).
- **FR-048 Motion bus** (D-2; finding F-1; **ruled at clarify Q1, 2026-10-19: explicit routing, no shared header**).
  The Motion section routes its one active source to its five fixed targets **explicitly inside `ProfundumMotion`**,
  over `MotionConfig`'s target set, with no virtual dispatch per sample (Vorago AR-2). `VoiceModRouter` is **not**
  composed and neither `voice_mod_types.h` nor `voice_mod_router.h` changes. This is a **recorded roadmap deviation**:
  the roadmap reads "→ `VoiceModRouter` as the modulation bus with a small fixed destination set" (lines 665–666),
  but the router's sources and destinations are Ruinae's (voice_mod_types.h:29–62, voice_mod_router.h:154) and both
  precedent voices keep it off the audio thread. Roadmap line 666 is amended at comply to name `ProfundumMotion` as
  the bus (FR-085). The rejected alternative (append the five targets and a source input to the router, gated by the
  Ruinae / Vorago suites and Ruinae's state round-trip) is recorded here for Phase 11. The Motion section exposes its
  current output as a `ModulationSource` (`getCurrentValue`, `getSourceRange`), on the pattern of
  `BassStereoField::MotionOutput` (bass_stereo_field.h:133); finding F-4. SC-030 therefore has no router clause.
- **FR-049 Two different "Motions".** The Motion section is separate from the stereo field's own MOTION
  (`BassStereoField::Controls::motionDepth` / `motionSource`). Neither drives the other.

### FR-050 series — Personality macros

- **FR-050 Component.** `ProfundumMacroMatrix` lives in `systems/profundum_macro_matrix.h` (Layer 3). It is a pattern
  copy of `VoragoMacroMatrix`:
  - a constexpr table of `ProfundumMacroRow { ProfundumMacro macro; ProfundumMacroTarget target; float amount; ModCurve
    curve; }`;
  - evaluated through `applyModCurve` (core/modulation_curves.h);
  - Linear / Exponential / SCurve only, never Stepped;
  - `static_assert`ed table invariants (every target claimed, no Stepped curve).

  Unlike Vorago, the base is the resolved morph state's own field, not a table literal (D-4), so the table carries no
  `base` column.
- **FR-051 Macros** (lines 669–671; concept §9). The four macros are **Weight**, **Dark ↔ Bright**, **Tight ↔ Loose**
  and **Clean ↔ Violent**, each in [0, 1]. **D-7 confirmed at clarify (main loop, 2026-10-19):** each has a neutral of
  0.5, Weight included, and at the neutral every row contributes exactly 0 (SC-008).
- **FR-052 Documented axes.** Every macro drives only targets on its documented axis. The rows below are **confirmed
  at clarify (main loop, 2026-10-19)**: the target sets and directions are normative; the amounts and curves are
  harvested, as Vorago AR-6 allows:

  | Macro | Axis (roadmap) | Drafted targets (direction as the macro rises) |
  |---|---|---|
  | Weight | "sub / fundamental / body" | recipe depth ↑, recipe body ↑, Anchor level ↑ |
  | Dark ↔ Bright | "spectral distribution" | recipe edge ↑, recipe shift ↑, Character Tone ↑ (**SHAPE dropped, ruling B10-2, 2026-10-20**: on B_s the AT row passed with 1.5–4 oct of travel but the SC-007 sweep broke at macro 0.55–0.65 for every curve and amount; the per-target sweep showed Edge, Shift and Tone each monotone and SHAPE not — from 0.5 toward BITE the centroid first falls 1.03 → 0.72 oct leaving HOLLOW's band, by ShapeMorph's design; the row stays in the table with amount 0) — harvested and gated on **B_s** (showcase B + sustain 1), ruling B10-1: on the sine-recipe A every target is at its clamp with the centroid moved 0.016 oct, so A has no spectral distribution to move; on A the macro is inert by construction and that is recorded |
  | Tight ↔ Loose | "envelope, transient, compression" | amp attack / decay / release times ↑ (log domain), transient attack gain ↓, compression amount ↓ |
  | Clean ↔ Violent | "nonlinear processing" | excitation mix and drive ↑, Fold amount ↑, Character Amount ↑ |

  The compression row reaches the compressor **after the Mass law (plan C-7, ratified by the main loop 2026-10-20)**:
  the engine hands clamp(`CompressionAmount` law output + c_TL, 0, 1), the add skipped at ±0, so SC-027(a) compares the
  compression receiver with that expression, which is the plain law value at the neutral. Offsetting the law's base
  instead would leave the Loose half inert.
- **FR-053 Application point.**
  - The macros are global and not morphed: `MorphState` has no macro fields (morph_state.h:103–122), and the roadmap
    puts macros with Mass and the Morph position in IDs 100–199 (line 716).
  - Each interval, the offsets apply to the resolved morph state after `advanceInterval` and before Motion and the Mass
    law (FR-026 step 3).
  - The results are clamped to each field's range.
- **FR-054 Idempotent application.** Applying unchanged macro values at every interval steps nothing. The render is
  bit-identical to applying them once (the Vorago FR-067 rule; SC-008(b)).
- **FR-055 Stored coordinates.** `ProfundumMacroValues` (four floats) can be set and read as one value ("presets store
  these coordinates", line 671). Phase 11 registers and persists them.
- **FR-056 Monotone axes.** A sweep of one macro with the others at neutral moves its axis descriptor monotonically
  (SC-007).

### FR-060 series — Determinism, safety, rates

- **FR-060 RT safety.** Every audio-thread method is `noexcept`, allocation-free, lock-free, exception-free and free
  of I/O. This covers note events, setters, `processBlock`, the morph / macro / Motion paths, voice steal and the mode
  switch. `prepare` is the only configuration call.
- **FR-061 Non-finite safety.** NaN, ±Inf or denormal values in any setter, in pitch bend or in tempo never reach
  audio. The components' own guards are kept, and the engine adds none that change their behaviour. Every engine-level
  guard uses `detail::isFinite` or a bit pattern, and the non-finite TU builds with `-fno-fast-math`.
- **FR-062 Determinism.** The same seed, event list and settings give a bit-identical render across two engine
  instances in one binary. That is an in-process comparison, not a stored golden. A different seed changes only the
  random-stream outputs. Cross-build references use `render_fingerprint.h` with measured tolerances (no bit-exact float
  goldens).
- **FR-063 Partition invariance.** The rendered output is independent of the host block partition, because every
  event is applied at the first grid point at or after its stamp (the Phase 8 FR-070 automation rule).
- **FR-064 Sample rates.** The engine runs at 22.05–192 kHz, the components' `kMinSampleRate` / `kMaxSampleRate`.
  Calling `prepare` again at a new rate re-initialises everything. `prepare` is configuration-time only and is never
  called from the audio thread (Edge Cases). A re-prepare at an unchanged `maxBlockSize` reuses its storage and
  performs no allocation. A larger `maxBlockSize` may allocate, because the composed `TruePeakLimiter::prepare`
  assigns its oversampling buffers (true_peak_limiter.h:60, 70–71).
- **FR-065 Block sizes.** Any host block from 1 to `maxBlockSize` works. Larger blocks are split internally.
- **FR-066 State read-back.** Getters return every engine setter's pending value, for Phase 11 state. The engine's
  morph blob is 422 bytes per instance (Phase 8 handoff 11.1) plus the engine's own non-morphed state. The layout is
  Phase 11's.

### FR-070 — Incoming handoff register

Each handoff the roadmap or the earlier phase records assigns to Phase 9 gets one treatment. **Gate** means an SC.
**Record** means a measured value is written to evidence. **Listen** means the item goes on the listening sheet
(FR-071). **Decide** means an Open Question.

| # | Source | Item | Treatment |
|---|---|---|---|
| H-1 | Phase 2, line 280 | Split hump +1.2…+1.6 dB at 500–550 Hz on the excitation's linear path | Listen |
| H-2 | Phase 3, line 328 | 0.59° / 60 s "breathing" at C3 through the Fold | Record in the soak (SC-013), then Listen |
| H-3 | Phase 3 D-7; roadmap 683–686 | Anchor sub through max Fold + every Character at max Amount, E0–E2 | Gate SC-017 |
| H-4 | Phase 3 Q1; roadmap 687–689 | FOLLOW through a real pitch-moving stage, ±5 cents, latency | Gate SC-018 |
| H-5 | Phase 3 Q3/Q4; roadmap 689–692 | Join after Character, direct path always present, Drop to the core only | Gate SC-019, SC-020 |
| H-6 | Phase 3 C-5; roadmap 692–693 | Anchor–h1 lock in the 30 min soak with Motion on the spectral controls | Gate SC-014 |
| H-7 | Phase 3 FR-025; roadmap 693–695 | Core bit-identical with the Anchor attached / detached; plain add | Gate SC-019 |
| H-8 | Phase 4, line 372 | Anchor raw level swing on hard snaps, −8.6 dB at E0 A 48 T 10 ms | Listen |
| H-9 | Phase 4, lines 696–697 | Tail upswing 0.418 st/sample for the voice's own aliasing measurement | Gate SC-021(c) |
| H-10 | Phase 5 (1), line 425 | A note after a Drop keeps partial phases unlike a fresh note | Listen; a possible Phase 4 amendment is recorded, not made |
| H-11 | Phase 5 (2), (4) | `syncPhase` calls; the Rectify half-wave rule | FR-008, Gate SC-033 |
| H-12 | Phase 5 (5) | Fracture observer lag on saw-like voices at Tone ≥ 0.75 | Record in SC-018 |
| H-13 | Phase 5 (6) | h1 transfer table, group delays, growl level swing, crossfade cell 2.65 % | Record. The CPU cell feeds SC-003 |
| H-14 | Phase 6 (3) | Sub-only voice sits on the limiter at Mass 100; reference / saw voices hit it 5.8–7.9 dB for ≈ 0.5 s | Listen; Record in SC-010 |
| H-15 | Phase 6 (4) | Fold-100 through the dynamics folds gain-modulation sidebands at −63 … −78 dBFS; 2× gain application + 8× detectors (≈ +0.5 %) | **Ruled Q4:** opt-in added (FR-082); Gate SC-021(b) at −80 dBFS |
| H-16 | Phase 6 (6) | `db_utils.h` run-time fast path as a CPU lever | Used only if SC-001/002 need it (FR-083) |
| H-17 | Phase 6 (8) | 4 detuned voices at Mass 100 read −10.1 dB sub-band GR | Record at 4 voices |
| H-18 | Phase 7 (4) | SOLID re-sync of DC / limiter states | **Ruled Q7:** re-synchronise at the crossfade end (FR-029); Gate SC-015 |
| H-19 | Phase 7 (5) | Fold / Rectify at offset ceilings invert the fundamental between channels; the Anchor restores the sub | Listen; Gate through SC-015 |
| H-20 | Phase 7 (8) | Limiter leaves a −45 … −55 dBFS anti-phase residue above the partial range | Record in SC-015 |
| H-21 | Phase 8 9.1, 9.2 | `advanceInterval` order; Mass split | FR-026, FR-027; Gate SC-032, SC-027 |
| H-22 | Phase 8 9.3, 9.4 | ADSR curve change-only setters; neutral curve; immediate receivers | FR-009; **ruled Q6** |
| H-23 | Phase 8 9.5 | Fold-feed smoother owned by the voice | FR-003 |
| H-24 | Phase 8 9.6 | `AmpEnvelopeFields` / `MotionConfig` final for v1 | FR-009, FR-047 |
| H-25 | Phase 8 9.7 | Worst transition interval 81.6 % of a core at 4 voices (fixture overhead included) | **Ruled Q3:** Gate SC-003 (worst host block ≤ 100 % of a core); interval peak recorded |
| H-26 | Phase 8 9.8 | `MotionConfig` selectors click-free | Gate SC-025 |
| H-27 | Phase 8 9.10 | Regenerated fundamental rotates against the Anchor (θ −149° … +179°, 0.27–8.3×) | Decide OQ-8 |
| H-28 | Phase 8 9.11 | Component switch clicks (Fracture → Crush / Rectify; OctaveDownSine → X) | Decide OQ-9 |
| H-29 | Phase 8 9.12 | HarmonicFold path dependence above `kSoftEnd` (≈ 0.13 dB) | SC-011 compares same-path renders only; Record |
| H-30 | Phase 8 9.13 | Limiter engages from p ≈ 0.8 on the showcase; read monotone claims on the sum / core taps | SC-007, SC-010 read the pre-dynamics sum and core taps; the output is recorded |

- **FR-071 Listening sheet.** The plan produces `evidence/listening-sheet.md`. For every "Listen" row it holds a
  render, the setting, and the question to answer. The user's verdicts are recorded there. A verdict that asks for a
  change becomes a ruling in the owning FR / SC row. **OQ-9 resolved by the main loop at clarify (2026-10-19):**
  Phase 5's and Phase 3's own component-switch clicks (Phase 8 handoff 9.11: Fracture → Crush / Rectify at C3,
  OctaveDownSine → the other waveforms at E1) are **not re-measured by this phase**. They enter the listening sheet as
  Listen rows rendered through the engine; a verdict that hears them as a defect opens a Phase 5 / Phase 3 fix
  outside this phase. SC-005 / SC-006 / SC-025 gate only what the engine adds, in the Phase 8 B10-1 form.

### FR-080 series — Theme-level FRs and handoffs out

- **FR-080 Audibility table** (lines 674–676, 973–975: "every section's on/off and every macro/lever moves a render
  descriptor ≥ the ruled bar"; "Phase 9 proves the whole table").
  - **Coverage.** Every row below is rendered at E1 and C3, and each row's contrast must move its descriptor by at
    least the row's bar. The rows cover every section and every lever inside a section. Where a section has no product
    "off" state (the amp envelope, Mass dynamics and the stereo field always run, FR-009, FR-025), the contrast is two
    settings of a real lever. No fabricated bypass is used.
  - **Base setting.** "A" and "B" are Phase 8's `showcasePair()` endpoints at p = 0 and p = 1
    (bass_morph_state_helpers.h:167). Endpoint A is clean: excitation mix 0, Fold 0, SHAPE 0, Character Tube at
    Amount 0, Drop 0 (bass_morph_state_helpers.h:167–176). So each row names the lever values it contrasts, and "+" names any other field
    the row changes from the base. All fields not named stay at the base.
  - **Showcase inert.** If a row's contrast is below its bar on the named base at both endpoints, the row gets a
    documented per-row setting, written into this table and the CSV with the reason (Assumptions). A row is never
    removed, and its bar is never lowered.
  - **Drafted descriptors.** The plan may replace a row's descriptor with a stated reason, keeping the row and a bar
    of the same family.
  - **Taps** are FR-086's. "Sum" is the pre-dynamics sum (Phase 8 B10-4). "Char / twin" is D_nl's reading: the
    Character tap over the `forceLinearCore` twin's core tap.
  - **Bars by family:** dB descriptors ≥ **3 dB**; centroid (D_br) ≥ **0.25 oct**; time descriptors ratio ≥ **2**;
    Anchor rows ΔE_sub ≥ **2 dB**. Frame-wise peak-to-peak rows read with SC-009's frame. Drop rows use Phase 4
    SC-021's descriptor bars (specs/profundum-phase4-drop-note-behaviour/spec.md:692). All bars are drafted for
    clarify.

  | Row | Lever: contrast | Base | Descriptor | Tap | Bar |
  |---|---|---|---|---|---|
  | AT-01 | Excitation: mix 0 → mix 1, drive 1 | A + edge 1 (**per-row setting, G14-8, main loop 2026-10-21:** the excitation's drive is excitationDriveLaw(drive) · edgeDriveGain(edge) (Phase 2 FR-004), so at edge 0 the row is inert on A (D_nl 0), on kSawAnchor (0.09 dB) and masked on B_s (0.89 / 0.28 dB); Phase 2 SC-001's own fixture is kSineAnchor — A's recipe — at edge 1, where it reads **29.4 dB**; the other candidates recorded) | D_nl | Char / twin | ≥ 3 dB |
  | AT-02 | SHAPE: 0 → 1 | A + recipe kSawAnchor at edge 0.5 (**per-row setting, G14-8, main loop 2026-10-21:** A is a sine (0.016 oct); B's Fold 80 / Character 0.5 regenerate the spectrum after SHAPE (0.024 oct, 0.37 at C3); kSawAnchor at edge 0 reads 0.24; at edge 0.5 SHAPE has a harmonic-rich linear core to move and reads **2.47 oct**; the other candidates recorded) | D_br | Sum | ≥ 0.25 oct |
  | AT-03 | Fold: `foldAmount` 0 → 100 | A | D_nl | Char / twin | ≥ 3 dB |
  | AT-04 … AT-09 | Character, one row per algorithm (Tube, Fold, Rectify, Bite, Crush, Fracture): Amount 0 → 1 | A + the row's algorithm | D_nl | Char / twin | ≥ 3 dB |
  | AT-10 | Anchor: level 0 → 1 (Sine, LOCK) | A | E_sub | Sum | ΔE_sub ≥ 2 dB |
  | AT-11 | Anchor mode: LOCK → PUNCH (boost 6 dB, 40 ms) | A + Anchor level 1 | E_sub over [note-on, note-on + `punchTimeMs`] | Sum | ≥ 2 dB |
  | AT-12 | Anchor mode: LOCK → GHOST (`ghostTimeMs` 15) | A + Anchor level 1 | E_sub over [note-on, note-on + `ghostTimeMs`] | Sum | ≥ 2 dB |
  | AT-13 | Anchor mode: LOCK → FOLLOW | A + Anchor level 1 + Fracture at Amount 1. A second row uses Rectify at Amount 1 on a half-wave-symmetric input (FR-008). Each uses the per-row setting at which `perceivedPitchRatio()` ≠ 1 (SC-018) | Anchor fundamental (instantaneous frequency from `phase()`, FR-086) | Anchor tap | moves by the flipped ratio (±1200 cents) within ±5 cents. If neither stage flips at any Amount, FOLLOW is **flagged inert** with the SC-018 evidence (roadmap 688–689) |
  | AT-14 … AT-16 | Anchor waveform: Sine → Triangle / RoundedSine / AsymmetricSine | A + Anchor level 1 | D_cx | Sum | ≥ 3 dB |
  | AT-17 | Anchor waveform: Sine → OctaveDownSine | A + Anchor level 1 | power at f₀/2 (Goertzel) | Sum | ≥ 3 dB |
  | AT-18 | `anchorFoldFeed`: g 0 → 1 | A + Fold 100 + Anchor level 1 | D_nl | Char / twin | ≥ 3 dB |
  | AT-19 | Drop amount: 0 → 24 st (time 200 ms) | A | start pitch (Phase 4 descriptor on d_core) | f_core pitch tap | ≥ 6 st |
  | AT-20 | Drop time: 60 → 500 ms (amount 24 st) | A | t_s | f_core pitch tap | ratio ≥ 2 |
  | AT-21 | Drop shape: 0 → 1 (amount 24 st) | A | α | f_core pitch tap | ≥ 0.1 |
  | AT-22 | Drop rebound: 0 → 1 (amount 24 st) | A | ρ | f_core pitch tap | ≥ 0.05·A_drop |
  | AT-23 | Glide: portamento time 0 → 200 ms, legato E1 → E2 (C3 → C4 at the C3 cell), MONO | A | t₅₀: time for f_res to stay within 50 cents of the target, floored at 32 samples | f_res pitch tap | ratio ≥ 2 |
  | AT-24 | Amp envelope attack: 0.1 → 500 ms | A. **Per-row setting (T016, the "Showcase inert" rule above): plain A, without C-8's sustain 1.** Reason: on any base held at sustain 1, the gate render's 1 s hold dominates D_t. D_t(0.1 ms) ≥ ≈ 1015 ms (1 s at the peak plus the 50 ms release) and D_t(500 ms) ≥ 515 ms, so the ratio is at most 1015 / 515 = 1.97, below the bar of 2. This holds on A_s and on B_s alike, and on B's own sustain 0.8 the hold dominates in the same way. So the row is inert at both endpoints. On A (decay 150 ms, sustain 0) the attack is the note. The A_s reading is recorded beside it in the CSV, ungated. **Descriptor (G14-8, "Drafted descriptors", main loop 2026-10-21): t_90, the time from note-on until the Sum's 1 ms-frame RMS first reaches 90 % of its maximum over a 1.5 s held note** — D_t, the gate render's fall time, read 108 / 161 ms for attack 0.1 / 500 ms (ratio 1.49) because the fall does not measure the attack | t_90 (was D_t) | Sum | ratio ≥ 2 |
  | AT-25 | Amp envelope release: 50 → 2000 ms | A | D_t (release reading) | Sum | ratio ≥ 2 |
  | AT-26 | **Mass**: 0 → 100 | A + anchor level 0.5 (**per-row setting, user ruling G11-2, 2026-10-20**: A's anchor level 1.0 is the top of the Mass law's AnchorLevel row (±0.5, clamped at 1), so Mass 0 halves the Anchor but Mass 100 cannot raise it, while the Depth row and Fold 61 drop the core's h1 by 3.2 dB; measured on plain A_s at E1: +1.78 dB, with E_sub −5.44 / −2.96 / −3.66 dB at Mass 0 / 50 / 100. At anchor level 0.5 the law moves the Anchor over its full row 0 → 1. The plain A_s reading is recorded beside it, ungated), and B as a second gated cell | E_sub | Sum | ≥ 3 dB. L_K at the output is recorded (H-14) |
  | AT-27 | Mass dynamics, gain: Mass 0 → 100 | B_s = B + sustain 1 (**per-row base, G14-8, FR-080's rule, main loop 2026-10-21:** B read 3.50 dB at E1 and **2.92 dB at C3**; B_s 4.59 / 4.01 dB; the B lines are recorded) | ΔL_K = L_K(dynamics output) − L_K(Sum), compared between the two settings | dynamics output tap and Sum | ≥ 3 dB |
  | AT-28 | Mass dynamics, transient: `transientAttackDb` 0 → +12 | B | onset ratio: peak \|y\| over the first 20 ms after note-on over the RMS over 100–300 ms, in dB | dynamics output tap | ≥ 3 dB |
  | AT-29 | Image mode: SOLID → SPREAD (spread 1) | A | D_st (**G14-8:** SOLID's side is exactly 0, the −120 dBFS floor that G8-1 flags invalid; for this off → on contrast the floor is the reading and the contrast is read against it: 87.9 / 118.0 dB on A_s / B_s) | field stage tap | ≥ 3 dB |
  | AT-30 | Image mode: SOLID → WIDE **at spread 1** (**G14-8:** WIDE's offsets are wideCeiling · Spread · K, {+0, +0} at spread 0 (Phase 7 FR-040), so the first run read −120 / −120 dBFS) | A + Character Rectify at Amount 0.5, so that WIDE's per-channel Character offsets (FR-012) act on a running stage (drafted) | D_st (the AT-29 floor rule) | field stage tap | ≥ 3 dB |
  | AT-31 | Spread: 0 → 1 (SPREAD) | A | D_st (the AT-29 floor rule) | field stage tap | ≥ 3 dB |
  | AT-32 | Gravity: 0 → 1 (SPREAD, spread 1) | B_s (**per-row base, G14-8:** A_s reads −120 dBFS in the band, one partial) | **D_st below f_c** (side / mid over 20 Hz … f_c, the band the Gravity law governs; **G14-8, "Drafted descriptors":** D_st above 2 f_c is blind to Gravity by construction, 0.25 / 0.07 dB on A_s / B_s). **At C3, f₀ lies above f_c, so no partial is in the band: recorded** (B_s E1 **11.7 dB**) | field stage tap | ≥ 3 dB |
  | AT-33 | Field MOTION: `motionDepth` 0 → 1 (SPREAD, spread 1) | B_s (**per-row base, G14-8, FR-080's rule:** A_s read 2.40 dB at E1 / 11.2 at C3; B_s **16.4 / 14.5 dB**; A_s recorded) | frame-wise D_st, peak-to-peak | field stage tap | ≥ 3 dB |
  | AT-34 … AT-38 | Motion target, one row per target (Mass, Depth, Body, Edge, Shift): Motion Off → CYCLE 0.1 Hz, amount 1, Motion Depth 1 | **Mass: A + anchor level 0.5** (the AT-26 per-row setting; plain A_s read 2.67 dB, the setting **5.66 / 5.75 dB**, B_s 5.10 / 4.94 recorded). **Depth, Body, Shift: B_s** (per-row base, FR-080's rule: A_s is a sine at depth 1 with no spectrum to move, 0.31 / 0.07 / ~0 readings; B_s **6.27 / 7.76 dB, 0.30 oct** at E1). **Edge: A + recipe kSawAnchor at edge 0.5** (per-row setting, G14-8: A_s ~0 and B_s 0.13 / 0.18 oct — B's edge is 1.0 so the bipolar swing clamps at the top; the setting gives the full 0 … 1 swing on a harmonic-rich linear core and reads **2.43 / 2.45 oct**; A_s and B_s recorded) (G14-8, main loop 2026-10-21) | frame-wise peak-to-peak of: Mass → E_sub; Depth → E_sub; Body → D_cx; Edge → D_br; Shift → D_br (**D_br frame-wise = the full-band centroid of the 8-period frame, Goertzel at n·f₀ up to min(fs / 2f₀, 128); the S9.11 h1..h8 fit was blind to Edge / Shift**) | Sum | ≥ 3 dB, or ≥ 0.25 oct for D_br |
  | AT-39, AT-40 | Motion source → Mass: Off → ENV (retriggered every 2 s), Off → DRIFT (smoothness 0.5) | A + anchor level 0.5 (the AT-26 per-row setting; plain A_s and B_s recorded; **G14-8:** DRIFT reads **3.22 / 3.54 dB**; ENV's frame series is read per held span — frames from two grid steps after each note-on to the next note-off — because the retriggers' own release / attack dominated the p2p of both arms (12.5 dB on the Off arm); the ENV source is 10 ms attack / 300 ms decay, reading under measurement) | frame-wise E_sub, peak-to-peak | Sum | ≥ 3 dB |
  | AT-41 | Morph position: 0 → 1 | showcase pair | D_nl | Char / twin | ≥ 3 dB |
  | AT-42 | Morph position: 0 → 0.5 | showcase pair | D_nl | Char / twin | ≥ 3 dB |
  | AT-43 … AT-46 | Note Memory, one row per mode M (AlwaysRetrigger, Legato, EnvelopeRetriggerOnly, OscillatorContinuous). M is compared with a reference mode M_ref, on a scripted MONO transition whose resolved `Actions` differ between M and M_ref in an acted-on field. M_ref and the transition kind are named in the CSV | A | max over the 50 ms after the transition of the RMS-envelope difference (5 ms frames), in dB. Where the pair differs only in `phase`: the core-h1 Goertzel phase jump across the transition | Sum, or core tap for the phase reading | ≥ 3 dB, or ≥ 30° for the phase reading |
  | AT-47 … AT-50 | Macros, one row per macro: 0 → 1, the others at neutral | A (AT-48 Dark ↔ Bright: **B**, per-row base, ruling B10-1; its A reading recorded beside it) | the SC-007 axis descriptor | the SC-007 tap | by family |

  Motion Depth needs no row of its own: Depth 0 is the Motion-Off identity (SC-026), so AT-34 … AT-40 are its rows.
  The table is written to `evidence/audibility-table.csv`. No cell may be below its bar (SC-010).
- **FR-081 Mono compatibility** (lines 976–977).
  - The full engine passes the Phase 7 sub-band mono-sum bars in every image mode (SOLID, SPREAD, WIDE), in MONO and at
    4 voices.
  - The Anchor tap is bit-identical L == R in every mode.
  - SOLID's output is L == R once its crossfade has settled, under the FR-029 ruling.
- **FR-082 Anti-aliasing** (lines 978–980; OQ-5 **ruled at clarify Q4, 2026-10-19**). The whole voice at E0–E2 meets
  **−80 dBFS** at the pre-dynamics sum, with every nonlinear stage at its maximum together and the Drop's worst
  upswing, and the **engine output meets the same −80 dBFS** (SC-021(b)). To reach it, `MassDynamics` gains the fix
  Phase 6 deferred (handoff (4), H-15): **2× oversampled gain application plus 8× detectors**, as an **append-only
  opt-in** (off by default, so every existing caller stays bit-identical; the engine turns it on at prepare). The
  opt-in's surface is `MassDynamics::setOversampling(bool)` + `setOversamplingFactor(2 | 4)` (plan S7, amended by
  T029). **Mechanism (optimisation group T029, 2026-10-20): the SIDECHAIN is oversampled, nothing else.** The input
  is upsampled by a heap-free halfband pair and the compressor's high-pass and held-peak detector run on the f·fs
  samples (`BassCompressor::setSidechainOversampling`, `processSampleGainOversampled`, append-only, default 1 =
  bit-identical); the gain computer, the transient shaper, the output trim and the audio path stay at the base rate,
  so the opt-in adds **no latency** and the requested factor runs at every sample rate (the compressor's 192 kHz
  ceiling no longer binds; `oversamplingActiveFactor`). The engine requests **4×**. Why this and not the whole
  chain at f·fs: the attribution probe (evidence/probes/p3-attribution.txt) showed the compressor is the sole source
  (the shaper adds nothing; input −106 dBFS); oversampling only the gain application changed nothing (on == off,
  −45.8 dBFS), so the reading is detector gain jitter, not gain-application aliasing, and inner-rate detection is
  what removes it. P-3 with the sidechain at 4×: **−94.9 / −95.1 dBFS at 44.1 kHz, −90.2 / −89.5 dBFS at 48 kHz** (T032: with the
  detector's own interpolation at 4 / f — 1 at 4× — **−88.3 / −88.4 at 44.1 kHz, −85.5 / −85.1 at 48 kHz**, bar −80,
  run-log/close/p3-t032.log)
  (the whole chain at 4× read −96.9 / −90.8 for about 3 % of a core in MONO; the sidechain-only path keeps the
  audio at the base rate). The cost is part of SC-001 / SC-002. Its cost (≈ +0.5 % of a core) is
  inside SC-001 / SC-002. The −60 dBFS and relative-bar alternatives were rejected.
- **FR-083 CPU** (lines 678–679, 981–982). The whole engine, everything on, at 48 kHz: ≤ 8 % of one core in MONO and
  ≤ 25 % at 4 voices. The ceilings are never relaxed. The levers are placement (FR-014), the H-16 fast path, and
  change-only setters. A per-stage breakdown is recorded. **Ruling R-A (2026-10-20), after probe P-1 read MONO 13.6 % /
  4 voices 46.3 % (post-sum 53.0 %), with the morph held static 10.5 % / 31 % (the continuous morph's control path
  costs 3 % / 15 % on top), and every stage above its own phase bar (static MONO: core+excitation 4.3 %, field 4.05 %,
  Character 3.7 %, Mass dynamics 2.8 %, Shape 2.0 %, DC+limiter 1.9 %; evidence/probes/p1-cpu*.txt): an
  optimisation group runs inside this phase before the rendered criteria (tasks.md Group 10b)** — profile the engine
  under the SC-001 setting, then behaviour-preserving fixes in the engine and append-only / bit-identical ones in the
  Phase 1–8 components (one control resolve shared by all voices, change-only setters, cheaper per-interval coefficient
  work, the unlinked L / R chain in the stereo modes); every touched phase's own suites gate its header (FR-084); the
  residual is re-measured by P-1 and reported with the breakdown. Raising the control interval to 64 samples and a
  separate Phase 9b were rejected. **T028 / T029 outcome (2026-10-20; evidence/probes/p1-hotspots.md):** a sampled
  profile of the SC-001 chain against a `/DEBUG`-linked copy of the test exe put 23 % of the engine's samples in the
  polyphase halfband stages (four 4× residual paths per channel per voice, unlinked in SPREAD / WIDE), 17.8 % in scalar
  C-runtime math (`TanhADAA::F1` evaluated twice per sample 7.7 %, Fold's `std::sin` per 4× sample 4.5 %, the bank's
  pan trig per interval 2.5 %), 4.7 % in the limiter's IIR 4× upsampler and 3.7 % in the bank's per-sample continuity
  pass; the replica's "field" row (4.05 %) is the unlinked-stereo cost of every stage after the core, not the field's.
  Lever batch 1 (bit-identical: F1 cache, continuity-pass skip, Shape's shared decimation, halfband branch interleave)
  read, paired and pinned on the static-morph setting, MONO 13.6 → 11.4 % and 4 voices 42.2 → 34.7 % (−16 % / −18 %;
  p1-t029-batch1-ab.log). Batch 2 (limiter halfband detector opt-in, fold-local sine) read MONO 10.9 → 10.1 % paired,
  4 voices within the run-to-run noise (p1-t029-batch2-ab.log). **T030 on the idle machine (2026-10-20 18:55): MONO
  9.20 % (bar 8), 4 voices 30.21 % per-voice placement (bar 25), post-sum 33.94 % — RED; −32 % / −35 % against the
  group's start (13.61 / 46.32); the batch-1 exe read 11.13 / 32.97 in the same idle session.** **Ruled by the user on
  that reading (2026-10-20): the lock-step L / R lever runs inside this phase (tasks.md T031).** In SPREAD / WIDE every
  stage after the core runs its two channels one after the other (the replica's "field" row: 2.98 % MONO, 8.70 % at
  4 voices, 34 % / 30 % of the whole); T031 processes L and R together — one loop, both channels' state objects stepped
  statement by statement — through the halfband paths and the oversampled stage bodies (core excitation, Fold, Shape,
  Character). Each lane keeps exactly its own sequence of operations, so every existing caller renders the same bits
  (proved by the fingerprint harvest's digits and the owners' bit-equality cases); single-channel entry points stay.
  Stage by stage, measured paired after each; the bars stay 8 % / 25 %. **Measured first on the primitive (19:10): no
  gain** — scalar pair 1.14× slower than two single calls, SSE2 two-lane single 1.1× slower, four-lane pair 1.06–1.22×
  slower (tasks.md T031 outcome); the lever is closed without touching a stage body. **Ruled by the user (2026-10-20,
  options: one more lever round / accept the residual): one more round (tasks.md T032)** — the per-interval control
  path from a trace of the full probe, a `BassCompressor`-local dB path (Phase 6 tolerance), engine-side pan
  coefficients through an append-only bank setter; then T030 again. **T030 final (2026-10-20 20:45, idle machine):
  MONO 7.26 % (Tube) / 6.96 % (Fracture) against 8 %, 4 voices 23.03 / 23.93 % against 25 %, post-sum 28.30 / 28.37 %
  recorded — GREEN; −47 % / −48 % against the group's start.** The breakdown of that final run (MONO Tube, % of a
  core, test-side replica: full minus the run without the stage; `evidence/run-log/close/p1-t030-final.log:21–43`):
  Character 2.73, core + excitation 2.47, unlinked stereo (the "field" row) 2.46, Fold 1.57, DC + limiter 1.50, control
  path (morph + law + Motion + macros) 1.19, Shape 1.14, Mass dynamics 0.99, Anchor 0.64, pitch 0.60, a1 + meter 0.31,
  amp envelope 0.12 (replica full 7.86 against the engine's 7.26, :19). The figures 2.26 / 2.30 / 2.24 / 1.14 / 0.92 /
  0.91 / 0.87 / 0.71 quoted here before were the preceding idle run's (`p1-t030-idle-batch5.log:21–43`, verdict RED
  on 4 voices 25.71 %, :123), not the final one (citation corrected at compliance remediation, 2026-10-21).
- **FR-084 Shared headers.** No change is planned to any shared header: `MonoHandler`, `HarmonicOscillatorBank`,
  `VoiceAllocator`, `VoiceModRouter` or any Phase 1–8 component, with **the ruled exceptions**: (1) the `MassDynamics`
  opt-in of FR-082 (Q4), append-only, off by default, gated by Phase 6's own TUs (`[mass-dynamics]` / `[profundum]`
  cases of `dsp_systems_tests`) and the computed `[long]` cases of `mass_dynamics.h`'s include closure under the
  change-set rule; (2) `RetriggerDeclick::prepare(fs, declickMs)` (R-B), append-only, Phase 4's TUs gate it; (3) the
  headers the R-A optimisation group touches (tasks.md Group 10b lists each one with its gate): append-only or
  bit-identical for every existing caller, each gated by its phase's own TUs and the `[long]` cases of its include
  closure. **T029 touched (2026-10-20):** `primitives/polyphase_halfband_oversampler.h` (branch chains interleaved;
  bit-identical; `[polyphase-halfband]`), `primitives/tanh_adaa.h` (F1 of the previous sample kept; bit-identical;
  `[tanh_adaa]`, Phase 5 TUs, Membrum's `feedback_exciter.h` via `membrum_tests`),
  `processors/harmonic_oscillator_bank.h` (the Phase 4 opt-in's continuity pass skipped while no coefficient writer
  ran; bit-identical, internal; bank TUs `[harmonic_osc_bank]` / `[stereo]` / `[detune]`, Phase 1 / 4 TUs; Innexus,
  Seraphis and Vorago never enable the opt-in), `processors/oversampled_residual_path.h` (append-only
  `lastLinearBase()`) with `systems/shape_morph.h` (HOLLOW reads the path's linear decimation instead of a second
  identical decimator; bit-identical by the T010 design note; `[shape-morph]`, `[shape-character]`),
  `processors/true_peak_limiter.h` (append-only opt-in `setHalfbandDetector`, default off so Seraphis, Vorago and
  Membrum render the same bits; `[true_peak_limiter]` incl. a new case for the mode, `[seraphis]` / `[vorago]` /
  `[subharmonic_engine]` fast cases, `membrum_tests`), `processors/harmonic_fold.h` (fold-regime sine through a
  fold-local quadrant-reduced minimax pair, ≤ 2 ulp; not bit-identical, gated by Phase 2's own bars in `[edge-fold]`).
  Bit-identity of the first five was proved by `VoiceEngine_FingerprintHarvest` printing the same digits before and
  after (evidence/run-log/close/t029-batch1-gates.log). **T031 (lock-step L / R, ruled 2026-10-20)** adds pair entry
  points, append-only and bit-identical per lane, to `primitives/polyphase_halfband_oversampler.h`,
  `processors/oversampled_residual_path.h`, `processors/harmonic_fold.h`, `systems/shape_morph.h`,
  `systems/bass_character_stage.h` and `systems/profundum_core.h` as far as each is needed; the same gates as above
  plus each owner's L == R / bit-equality cases — **closed without a change: every lock-step variant measured slower
  (T031).** **T032 touched (2026-10-20):** `processors/harmonic_oscillator_bank.h` (append-only `setPanCoefficients`;
  the core evaluates the constant-power pair itself, Cephes pair ≤ 2 ulp, so `applyPanOffsets` is no longer called
  by Profundum; other consumers untouched), `processors/dynamics_processor.h` (append-only `processDetectionOnly`:
  processSample's detection, gain computer and smoother without the discarded audio path, dB through `std::log2`;
  `processSample` itself untouched, `[dynamics]` green), `processors/period_peak_detector.h` (append-only
  `setPeakOversampling(1 | 2 | 4)`, default 4 bit-identical; `BassCompressor` runs it at 4 / f on its f× sidechain —
  P-3 re-read **−85.1 dBFS** worst at 48 kHz, bar −80, was −89.5; `[period-peak]`, `[mass-dynamics]`),
  `processors/keytracked_tilt.h` (`updateGains` as a float exp2 pair instead of two double pows: the Character stage
  hands it the per-sample smoothed Tone, 5.8 % of the engine's samples under a moving Tone; `ShapeMorph::computeInverse`
  evaluates the same float expressions so the C-23 inverse stays the tilt's gains bit for bit; Phase 5 TUs),
  `processors/harmonic_fold.h` (the T029 fold sine's `std::lround` → add-and-truncate),
  `processors/spectral_shape_recipe.h` (log2(harmonic) from a table filled by the same `std::log2` call;
  bit-identical for every consumer), `systems/profundum_core.h` (append-only `setControlsWithShape`: the engine's one
  `evaluateShape` per changed interval adopted by every voice — bit-identical, fingerprint digits unchanged),
  `systems/profundum_voice.h` / `profundum_engine.h` (`IntervalInputs::sharedShape`). Q1 removed the router exception
  (FR-048); plan C-1 (ratified 2026-10-20) removed the limiter accessor
  (FR-029). If the plan needs any other such change,
  it is append-only, the other consumers stay bit-identical, and their suites gate it under the change-set rule
  (Ruinae, Seraphis, Vorago, Innexus; lines 983–985, 991–992).
- **FR-086 Test taps and seams.** Most SC rows read signals that the product never exposes. `ProfundumVoice` and
  `ProfundumEngine` provide them through one test seam. This FR lists every tap and seam, so that each SC's measurand
  exists.
  - **Per-voice taps.** There is one set per voice in MONO and in POLY. A POLY gate reads each voice's own taps,
    never a sum, unless its row says "summed".
    - **Core tap:** the `ProfundumCore` output after the excitation, before `ShapeMorph` (L, R).
    - **Character tap:** the `BassCharacterStage` output, that is, the main path at the join, before g_amp (L, R).
    - **Anchor tap:** the `AnchorOscillator` output before g_amp (L, R).
    - **Gain taps,** per sample: g_amp, and g_d (the declick dip × the steal fade; FR-015).
    - **Pitch taps,** per sample: f_res, f_core, and the Anchor's pitch input (f_res or f_core by mode, FR-006). Per
      interval: the follow ratio handed to the Anchor.
    - **Handed values,** per interval: the Mass-biased Anchor level, a₁ (FR-005), and every FR-013 setter call with
      its argument, including `syncPhase` (the call log).
  - **Per-engine taps.**
    - Audio: the pre-dynamics sum, the `MassDynamics` output, the `BassStereoField` stage output, the DC output and
      the engine output (each L, R).
    - Per interval: the `Resolved` state; the state after the macro offsets and after the Motion offsets; the Motion
      source value; `MassLaw::smoothedMass()` and every value handed to a `MassLaw` receiver; and the FR-026 call log
      (SC-032).
  - **Seams.** These are configuration-time switches, all off by default:
    - `macroBypass`: the matrix contributes nothing (SC-008(a)).
    - `anchorDetached`: the Anchor is not added at the join and is not fed to the Fold. The core still receives the
      same calls (FR-004, SC-019(a)).
    - `forceLinearCore`: every voice's excitation mix is set to 0 after every offset (macro, Motion, Mass). A twin
      engine rendered with the same seed, settings and events then gives, on its core tap, the linear twin core
      that D_nl's denominator reads (bass_morph_test_helpers.h:171, 239, 767–771). Only the excitation differs, so
      under Motion → Mass the twin follows the same Mass-biased recipe per interval. **In this phase, D_nl is always
      the Character tap of the render under test over the core tap of its `forceLinearCore` twin, per voice.**
    - `disableStealFade`, `disableDeclick`: positive controls for SC-005 / SC-006.
    - `followNoBypass` (FOLLOW without `DropPhaseLock::setBypass(true)`) and `lockReadsCore` (LOCK fed f_core):
      positive controls for SC-020.
  - **Exposure (drafted; the plan rules the mechanism).** A non-owning observer pointer and a seam struct, both set
    only at configuration time. A compile-time test macro is not used: a header-only class compiled with and without
    the macro in one test binary would be an ODR violation.
  - **RT rule.**
    - With no observer attached and every seam off, the cost is one predictable branch per tap point per block.
    - The engine never allocates, copies or locks for a tap. The observer receives const spans of the voice's own
      scratch. Any copying is the observer's, on the test side.
    - SC-022 runs with no observer attached.
  - **Transparency.** Attaching the observer with every seam off leaves the output bit-identical (SC-036).
  **G14-12 (user ruling 2026-10-21, exception (5)):** `systems/profundum_core.h` (the excitation split low-passes)
  and `processors/harmonic_fold.h` (the Fold split) take their 500 Hz split biquads from the new L1 primitive
  `primitives/lowpass_biquad_d.h` (`LowpassBiquadD`: the Biquad Lowpass design with double coefficients and state
  behind a float interface; non-finite input resets, `stateFinite()`). Not bit-identical: the float32 TDF2 split at
  fc / fs ≤ 0.01 left a −100 … −118 dBFS rounding floor coherent with the input (SC-021 row); the design, the cutoff
  and every caller are unchanged. Gated by Phase 1 / 2 TUs, the Phase 9 lanes and the `[long]` closure of every
  modified header (T025-3: 354 consumer shards over 14 modified headers, seven of them in `dsp_processors_tests` /
  `dsp_effects_tests`). **G14-13 (user ruling 2026-10-21, exception (6)):** `processors/harmonic_fold.h`
  `kDetectorReleaseHysteresis` 0.005 → 0.01 (the static_assert bound ≤ 0.01): the 0.5 % window ripple on a
  parity-panned channel toggled a Crush quantiser slot (SC-021 row, G14-13 (2)); Phase 2's tests read the constant
  symbolically (harmonic_fold_test.cpp:1724, :1866); gated by Phase 2's per-push suite and its six `[long]` cases
  in `dsp_processors_tests` on the 1 % bits. **Consumer-lane scope (user ruling 2026-10-21):** the four long Vorago
  cases of the closure (`VoragoEngine_OvernightSoak` / `_OvernightEvolution` 4 h each, `_ConfigurationFuzz` 70 min,
  `VoragoMacro_SweepAxes` 55 min) are ruled out: the three modified headers on Vorago's path (bank: the skipped
  continuity pass needs the Phase 4 opt-in Vorago never enables; TanhADAA: the same expression on the same inputs;
  limiter: default-off look-ahead with a bit-identity test for the off path) are bit-identical for Vorago, and
  `vorago_tests` plus the Vorago per-push cases of `dsp_systems_tests` are green on the final bits. The seven short
  Vorago cases run.
- **FR-085 Handoffs out.** Written to `evidence/phase-handoffs.txt` and copied into the roadmap at comply:
  - **Roadmap:** Phase 9 line 666 is amended to name `ProfundumMotion` as the Motion bus (FR-048, Q1 deviation).
  - **Phase 10:** the engine's prepare / process contract and latency 0.
  - **Phase 11:** every engine setter with range, default and unit; `ProfundumMacroValues`; the polyphony range
    **1–8, default 4** (Q8, `kMaxVoices = 8`), registered once; the state byte budget.
  - **Phase 12:** the macro row table for the 12a mapping deliverable (line 771); the audibility table; the listening
    verdicts.

---

## Success Criteria

All measurements are at 48 kHz unless stated. E0 / E1 / E2 / C3 are MIDI 16 / 28 / 40 / 48. Descriptors (E_sub, D_cx,
D_nl, D_st, D_t, D_br, L_K) reuse the Phase 8 readers and Phase 7 helpers. Every tap named below is an FR-086 tap.
Following Phase 8 B10-4, the pre-dynamics sum and the core tap carry the monotone and audibility claims, and the
output is recorded beside them. D_nl always reads the Character tap over the core tap of the `forceLinearCore` twin
(FR-086). The Anchor, pitch, gain and handed-value taps carry SC-016 to SC-020, SC-027 and SC-033. A row whose bar is
open (drafted, or waiting on an OQ) has the ruling written into the row before build. A row left at "recorded" with
no ruling cannot close the phase. CPU cases run alone through `node tools/run-cpu-tests.js dsp_systems_tests`.

| ID | Criterion | Metric, threshold and method (test name sketch) |
|---|---|---|
| **SC-001** | CPU, MONO, everything on (FR-083) | `VoiceEngine_CpuMono` (`[performance]`, run alone, pinned, median of 3). Setting: excitation mix 1 / drive 1, SHAPE 0.5, Fold 100, the worst Character algorithm at Amount 1 (the plan names it from Phase 5's CPU record), Anchor FOLLOW, `anchorFoldFeed` 1, Drop 24 st retriggered every 250 ms, Motion CYCLE → Mass, depth 1, WIDE with field Motion, Mass 100, the morph ramping 0 ↔ 1 on the showcase pair, host block 64. Bar **≤ 8 %** of one core. |
| **SC-002** | CPU, 4 voices, everything on (FR-083) | `VoiceEngine_CpuPoly4`: same protocol, four held notes E1 / A1 / E2 / C3. Bar **≤ 25 %**. **Recorded, not gated (Q8):** the same protocol at **8 voices** (E1 / A1 / E2 / C3 / G1 / B1 / E3 / A2) in the same lane, written beside the 4-voice figure for Phase 11. |
| **SC-003** | Worst control interval and worst host block (H-25; OQ-4 **ruled at clarify Q3, 2026-10-19**) | `VoiceEngine_WorstInterval` (`[performance]`, same lane as SC-001 / SC-002): SC-001 and SC-002 are **render averages** and stay gated as written. On top of them a **real-time bound**: at 4 voices, host block 64, with every discrete morph group switching at one grid point and the morph position ramping at its 5 ms minimum, the **worst single 64-sample host block costs ≤ 100 % of one core** (≤ 1.333 ms at 48 kHz, measured as the maximum over the render of the block's wall time divided by the block's audio time), so no dropout is possible. The worst 32-sample control interval is recorded against Phase 8's 81.6 % fixture figure, not gated. A per-interval 25 % bound (staggering the discrete switches) was rejected. **Coverage (G14-7, main loop 2026-10-21):** the AmpCurve-on-all-4-voices coverage is asserted on every switch block (the host block that starts at a Character-switch grid point), where those records are issued; the timed worst block is whichever block cost most and need not be a switch block (the first run's worst block held no switch), so its mask is recorded beside whether it is one. |
| **SC-004** | Placement measurement (FR-014, OQ-2) | `VoiceEngine_PlacementCost`: per-voice vs post-sum Fold / Shape / Character at 4 voices, and FOLLOW per voice vs off, recorded as % of a core. The ruling is written into FR-014. |
| **SC-005** | Voice steal clickless (line 680; FR-016) | `VoiceEngine_StealClickless` (`[long]` grid): 5 notes into 4 voices at E1 / C3, under each of {held, releasing} victim state × Note Memory mode × image mode, host blocks {1, 37, 512}. All readings are on the pre-dynamics sum; the output is recorded. **Window W:** from the grid point at which the steal (or restart) is applied, to the end of the steal fade or declick dip, plus one period of the slower of the old and new f₀. **Reference render:** the same engine, seed and events, with the polyphony one higher, so nothing is stolen. The new note starts on an idle voice at the same pitch, **issued at the grid point of the switch sample** (the restart's grid point + `samplesUntilSwitch`, rounded up to the 32-sample grid; reading refinement by the main loop, 2026-10-20): that is where the stolen or restarted voice actually starts it, so both sums carry the new note at the same phase and (a) measures the dip rather than phase alignment (with the reference at the restart's grid point the steal cell read 1.107 from a slope 233 samples after a 144-sample dip). **(a) Relative bound:** max \|y[n] − y[n−1]\| over W ≤ **10^(0.5/20)** × the reference's maximum over the same W. **(b) Analytic bound:** the Phase 4 SC-010(b) sample-delta bound (Phase 3 SC-006(c) form) holds over W (specs/profundum-phase4-drop-note-behaviour/spec.md:681). S_max is taken from the declick's analytic raised-cosine slope (`ProfundumVoice::kDeclickMs` = 3 ms, ruling R-B), the Drop's analytic peak slope, and the steal fade's analytic slope (D-8; the plan derives it). **(c) `ClickDetector`'s differential count** against the reference is **recorded per cell, not gated**. This follows Phase 4's ruling (Clarifications 2026-10-13, "T023 epoch 6 readings" (B)): the steady core trips the detector at every saw / growl edge, so the count does not separate a click. **Positive controls:** a steal under `disableStealFade`, and a Reset restart under `disableDeclick` (FR-086), must each fail (a) or (b). |
| **SC-006** | MONO note replacement clickless (line 680) | `VoiceEngine_MonoReplaceClickless`: the legato × Retrigger × Note Memory × GlideMode matrix (Phase 4's matrix, now on the full chain with Shape, Fold, Character and Mass), intervals {1, 12, 24} st, both directions. The gate is SC-005's: the same window W, (a) and (b) gated, (c) recorded, and the same positive controls. The reference is a POLY-2 render of the same events, in which the new note starts on an idle voice. In cells with a portamento time > 0, the new note does not start at its pitch, so (a) is recorded there. Those cells are gated by (b) with Phase 4 SC-010(c)'s S_max for glides: the analytic one-semitone frequency step on a phase-continuous oscillator. **G14-5 (user rulings 2026-10-21).** 802 cells per case passed except the eight +24 st replacements at C3 on showcase B (worst (a) 1.064 per channel; 0.78 with the field in SOLID; (b) 0.684 against a bound of 0.653 while the POLY-2 reference itself read 0.681). **(a):** first read as the per-partial pan vector re-targeting at the jump and ruled a pan ramp on `ProfundumCore`; the ramp measured no effect (1.064 → 1.068) and was **reverted** (no pan-ramp code ships). The per-channel probe located the excess in WIDE's per-channel Character offsets: on B (WIDE, spread 1) the L channel (Amount +0.15) reads 1.068 and R 0.92; the same B in SPREAD reads 0.957 on both channels and in SOLID 0.78. At a two-octave legato jump without a dip the Character stage keeps its state from C3 (the memory the declick switch clears), and on the hotter WIDE channel that warm start exceeds the cold-start POLY-2 reference by 7 %. **Ruled (v2, user, 2026-10-21): (a) is recorded for a jump of ≥ 24 st on a WIDE base when no dip is armed** — a legato cell, or a retrigger cell under a Note Memory policy that suppresses the dip (EnvelopeRetriggerOnly, OscillatorContinuous, Legato; the Retrigger case's same seven C3 +24 st cells read the same 1.064, the AlwaysRetrigger cell with its dip 1.004) — gated everywhere else: SOLID and SPREAD bases, every cell whose dip clears the chain, every jump below two octaves; (b) gates the recorded cells. The dip is detected on the render, not assumed from the policy. **Re-read: Legato and Retrigger cases green, worst gated (a) 1.044 (B E1 −1 st), min (b) margin 1.125.** Clearing the Character stage at large legato jumps and keeping (a) gated were rejected. **(b):** a fresh C5 on B's cold chain overshoots its after-settle D_max by ~5 % in its first 10 ms (Fold / Character settling), so the bound's premise failed for the reference too — ruled **the steady reference's D_max / P_max are read from note-on (the whole fresh render), the after-settle bound recorded beside it (`bound_settled`)**; factor 1.1, S_max and P_max unchanged. |
| **SC-007** | Macro sweeps monotone (FR-056; line 680) | `VoiceEngine_MacroMonotone`: each macro from 0 to 1 in 21 steps, the others at neutral, E1 and C3, on the macro's harvest base — A_s for Weight, Tight ↔ Loose and Clean ↔ Violent, **B_s for Dark ↔ Bright (ruling B10-1, 2026-10-20)**. The documented descriptor is non-decreasing within ε, using Phase 8's D-8 tolerances (0.25 dB for dB descriptors, 0.02 oct for centroids, 2 % for time ratios; drafted). Axes: Weight → E_sub / sub-band share; Dark ↔ Bright → D_br (centroid); Tight ↔ Loose → D_t (gate render); Clean ↔ Violent → D_nl. |
| **SC-008** | Macro neutral and idempotence (FR-051, FR-054) | `VoiceEngine_MacroNeutral`: (a) all four at 0.5 give a render bit-identical to an engine whose matrix is bypassed (`macroBypass`, FR-086); (b) applying unchanged non-neutral macros at every interval is bit-identical to applying them once. |
| **SC-009** | Motion → Mass swell measured (FR-044; line 681) | `VoiceEngine_MotionMassSwell`: CYCLE → Mass, depth 1, amount 1 (half-span, Q2: Mass swings 0 … 100 around 50), rates {0.1, 0.5, 2} Hz, Mass 50, E1. **Frame:** the descriptors are read frame-wise. A frame is 8 whole periods of f₀ (194 ms at E1), rectangular over whole periods so that the harmonics fall on bins. The hop is 960 samples (20 ms). D_nl's twin is read over the same frames. Phase 8's full `kLowFrequencyFftSize` frame (262 144 samples, ≈ 5.5 s; bass_morph_test_helpers.h:38) would average a 0.1–2 Hz swell away, so it is not used here. **Render:** 1 s settle, then max(3 cycles, 3 s) per rate, so 30 s at 0.1 Hz. **Peak-to-peak** is max − min of the frame-wise series over whole cycles. Recorded: the peak-to-peak of E_sub, D_nl and the sum-tap L_K, with their attenuation against rate. The frame's analytic boxcar response at each rate is reported beside the reading, so that the frame's smoothing is separated from the 40 ms Mass slew's. **Gate:** at 0.1 Hz the swell moves D_nl by ≥ **3 dB** peak-to-peak (**bar confirmed at clarify by the main loop, 2026-10-19**: it is Phase 6 SC-011's Mass audibility bar, and Phase 6 measured 54 dB of THD separation over 0 → 100). ENV → Mass and DRIFT → Mass are recorded with the same frame. |
| **SC-010** | Audibility table complete (FR-080) | `VoiceEngine_AudibilityTable` (`[long]`): every FR-080 row **AT-01 to AT-50** at E1 and C3, on the row's base setting, contrast, descriptor and tap. The rows cover the excitation, SHAPE, Fold and each of the six Character algorithms. They cover the Anchor level, each Anchor mode (PUNCH, GHOST, FOLLOW) and each waveform, plus `anchorFoldFeed`. They cover the Drop amount, time, shape and rebound, glide, and amp attack and release. They cover **Mass** 0 → 100 and Mass dynamics (gain and transient). They cover each image mode, Spread, Gravity and field MOTION. They cover each Motion target and each Motion source on Mass. They cover the Morph position (0 → 1 and 0 → 0.5), each Note Memory mode, and each macro. **Bars** are FR-080's, drafted from the Phase 5 / 8 family: dB descriptors ≥ **3 dB**; centroid ≥ **0.25 oct**; time descriptors ratio ≥ **2**; Anchor rows ΔE_sub ≥ **2 dB**; Drop rows by Phase 4 SC-021's descriptor bars (start pitch ≥ 6 st, t_s ratio ≥ 2, α ≥ 0.1, ρ ≥ 0.05·A_drop); FOLLOW (AT-13) moves the Anchor's fundamental by the flipped ratio within ±5 cents, or FOLLOW is flagged inert with evidence; Note Memory rows ≥ 3 dB or ≥ 30°. **No cell below its bar.** A row moved to a documented per-row setting carries the reason in the CSV. The CSV is written to evidence with the columns row, contrast, base, note, descriptor, tap, value, bar and pass. The output-tap reading of each row is recorded beside it. |
| **SC-011** | Determinism harness (FR-062) | `VoiceEngine_DeterminismHarness` (`VoragoEngine_DeterminismHarness` template): a seeded 6-event schedule in MONO and POLY renders bit-identical across two instances and two runs in one process. Seeds A ≠ B differ (residual above −60 dB) only where a random stream is audible (DRIFT on). A `render_fingerprint.h` fingerprint with measured tolerance is stored for cross-build checks. Path-dependent HarmonicFold states (H-29) are compared along identical paths only. |
| **SC-012** | Partition invariance (FR-063) | `VoiceEngine_Partition`: host blocks {1, 7, 32, 64, 511, 4096} give bit-identical output for the same sample-stamped event list. |
| **SC-013** | 30 min soak (line 682) | `VoiceEngine_Soak30Min` (`[long]`): 30 min of seeded activity in a MONO half and a POLY half. Each half holds the four SC-014 lock segments, and seeded note and automation activity in the rest, with Motion on and the morph moving. Gates: every sample finite; true peak ≤ the limiter ceiling + 0.1 dB (**G14-1, user ruling 2026-10-21:** the zero-look-ahead `TruePeakLimiter` read **+1.017 dBTP** (MONO, 74 s in, inside a held hot E1 lock note) and +0.211 dBTP (POLY) over the first 30 min soak, identical with the T029 halfband detector off: it clamps every sample to the ceiling exactly but its gain steps sample by sample on hot asymmetric material, and the band-limited reconstruction of that output overshoots between samples. Ruled: **an append-only, default-off look-ahead on `TruePeakLimiter`** (`setLookaheadSamples`; the gain applied to the N-sample-delayed audio is the mean over the last N of the running minimum over the last N + 1 of ceiling / tp, so |out| ≤ ceiling still holds sample by sample and the gain ramps instead of stepping), **engaged by the engine at 64 samples** (`kLimiterLookaheadSamples`, 1.33 ms at 48 kHz) and reported by `latencySamples()` (FR-025); the bar is unchanged; SC-021(b) and P-1 re-read on the final bits. Applying the gain at 4× inside the limiter (zero latency, the clipper remains) and gating the sample peak only were rejected); DC per 10 s window ≤ **−80 dBFS at the DC-safety tap** (FR-025's `DCBlocker`, the FR-086 DC tap) **over the windows that contain no note-on / note-off; the windows with note events are recorded** (**G14-2, user ruling 2026-10-21:** the 30 min soak's lock windows read −97 … −133 dBFS and its activity windows −68.6 (MONO) / −63.6 dBFS (POLY); the blocker is a 5 Hz first-order high-pass, τ = 31.8 ms, so a DC step Δ at its input — an asymmetric Fold-100 voice's note-on or note-off — leaves a transient whose 10 s-window mean is Δ·τ / 10 s, −63.6 dBFS for Δ = 0.21; keeping the bar on every window (unreachable below a ~50 Hz cutoff, which would move the sub's phase) and shorter windows (Δ·τ / 1 s reads worse) were rejected), and at the output no upward trend (the last 5 min mean ≤ the first 5 min mean + 1 dB, **over the same windows without note events, or the last mean ≤ −100 dBFS** — G14-2 round 5: the quiet windows sit at −130 … −141 dBFS, numerical noise 50 dB under the bar, where the +1 dB clause read a "rise" from −141 to −131; the all-window trend is recorded) with the output's per-window DC **recorded** (user ruling G11-4, 2026-10-20: the `TruePeakLimiter`, which FR-025 places after the DC blocker, has an instant attack, an 80 ms release and no look-ahead, so on the onset of a hot asymmetric note — the lock segment's Fold 100 / excitation asymmetry 1 voice, true peak −0.93 dBTP — its gain dips on the positive peaks and the gain-modulated asymmetric waveform carries a DC lobe: the smoke's window 0 reads −68.0 dBFS at the output against −115.0 dBFS at the DC tap, and the steady window −126.8 dBFS. It is gain modulation, not a build-up, and the trend clause gates build-up. A second DC blocker after the limiter (FR-025's order, a second sub phase shift) and a limiter look-ahead (latency on a sample-accurate bass) were rejected); **state finite**, checked once per interval. The components that declare `stateFinite()` are polled: `ProfundumCore`, `ShapeMorph`, `HarmonicFold`, `BassCharacterStage`, `AnchorOscillator` and `MassDynamics` (grep of `dsp/include`, 2026-10-19). The rest declare none and none is added (FR-084). For them, their FR-086 taps are checked with `detail::isFinite`: `DropEnvelope` / `DropPhaseLock` through f_core; the amp envelope through g_amp; `RetriggerDeclick` through g_d; `LFO` / `BrownianDrift` / ENV through the Motion source value; `MassLaw` through `smoothedMass()` and the handed targets; `BassMorphEngine` through every float of `Resolved`; and `BassStereoField`, `DCBlocker` ×2 and `TruePeakLimiter` through the field stage, DC and output taps. H-2 is recorded. |
| **SC-014** | Anchor–h1 lock in the soak (H-6) | Inside SC-013. **Segments:** each half holds four lock segments, one per spectral Motion target (Depth, Body, Edge, Shift). CYCLE 0.5 Hz drives Depth and Body, and DRIFT drives Edge and Shift (drafted), with amount 1 and Motion Depth 1. `MotionConfig` has one target at a time (morph_state.h:85), so each segment sets its own. Each segment is a held note of ≥ 122 s in Anchor LOCK, Sine, with no note event, Drop, glide, retrigger or morph motion inside, **at excitation mix 0** (**G14-3, main loop 2026-10-21:** the voice's core tap is read after the edge excitation, whose h1 phase rotates with its drive, and DRIFT on Edge / Shift (amount 1) moves that drive — the first 30 min soak read 4.15° (Edge) and 16.65° (Shift) against 0.16° / 0.08° for Depth / Body; the Anchor's reference is the linear core's phase (Phase 3), so the lock is measured against it. Re-read: MONO max window **0.078°**. The activity portions keep the hot state). **Defect fix (main loop, 2026-10-21, G14-3 POLY):** the POLY half's Shift segment still read 94° with two voices entering it 3.3 and 33.5 semitones off f_res for 1–2.5 s; a hidden probe (`VoiceEngine_SoakPolyLockProbe`) showed the grid's resolved Drop at 0 st / 100 ms at the note-on while the voices dropped by the activity's last amount — `ProfundumVoice` triggered the Drop at the note event with the amount / time / shape / rebound it had last applied, and a voice that had been idle had not run its interval setters since the state changed. The Drop setters now run at the head of the interval and the transition's trigger is flushed after them (at the declick switch the interval's setters are already in place, so the flush follows the transition at once). A user note after a pause, or after an automation change while idle, no longer drops by a stale amount. Gap before each lock segment raised from 3 s to 8 s while this was chased (kept: every voice idle at s₀). MONO holds E1; POLY holds E1 / A1 / E2 / C3. **Measurand:** per voice, the Anchor tap's phase against the h1 of that voice's own core tap, by the Phase 3 `goertzelPhase` method. **Statistic:** per 60 s window, the Phase 3 SC-003 form \|Δθ(t₀ + 60 s) − Δθ(t₀)\|. Windows are aligned to the segment start s₀ at t₀ = s₀ + 1 s and s₀ + 61 s, two windows per segment and voice (specs/profundum-phase3-anchor/spec.md:559). **Gate:** every window ≤ **0.5°** (the Phase 3 bar). The maximum and the trend across windows are recorded. The activity portions are not windowed, and FOLLOW never runs in a lock segment, since it tracks f_core·r rather than h1 (FR-006). |
| **SC-015** | Full-engine mono sum (FR-081; line 682) | `VoiceEngine_MonoSum` (`[long]`): SOLID / SPREAD / WIDE × MONO / 4 voices × showcase A and B × E1 / C3. Sub band Q_sub within **1.0 dB** (`kSubBandQBarDb`) and correlation ≥ **0.99** (`kSubBandRhoBar`). SOLID output L == R bitwise **from the crossfade end plus the engine's latency** (FR-029, Q7 re-synchronisation; no settle time; G14-1's 64-sample look-ahead delay line holds the last pre-copy samples — re-read 2026-10-21: exactly 64 L ≠ R samples after the copy, none after the latency). H-20 is recorded. |
| **SC-016** | Anchor L == R (FR-081) | In SC-015's renders: the Anchor tap is bit-identical L == R in every interval. |
| **SC-017** | Anchor through max Fold + Character (H-3; lines 683–687) | `VoiceEngine_AnchorThroughChain`: E0, E1, E2 × all six algorithms at Amount 1 × Fold 100 × max excitation, Anchor level 1, LOCK. S is that chain; R is the same chain with Fold 0, Amount 0 and excitation 0. **Gated rows:** the four Anchor waveforms whose fundamental coincides with core h1 (Sine, Triangle, RoundedSine, AsymmetricSine). **(a)** The Anchor's own w₁ (Anchor tap) is within **1 dB** between S and R. **(b)** \|E_sub(S) − E_sub(R)\| ≤ **4.1 dB**, two-sided, **for Tube, Fold and Bite**, the algorithms whose Amount-1 output keeps the core's h1. This is Phase 3 SC-009(ii) (specs/profundum-phase3-anchor/spec.md:565): a drop of the summed fundamental is bounded exactly as a rise is. **Rectify, Crush and Fracture remove or invert the core's h1 at Amount 1 by definition** (full-wave rectification doubles the frequency; Fracture's h1 → 0, Phase 5 FR-050 / SC-030), so on those rows the summed fundamental falls by 20·log₁₀(1/2) = −6.02 dB or more while the Anchor's own w₁ is unchanged (measured at E1 Sine, ρ / θ / ΔE: Rectify 0.18 / 172.7° / −7.77 dB, Crush 0.42 / −118.0° / −7.10 dB, Fracture 6e-6 / −176.1° / −6.03 dB; Tube 0.49 / 1.2° / −2.56 dB, Fold 0.70 / 1.5° / −1.44 dB, Bite 0.80 / −11.3° / −0.98 dB): **their (b) reading is recorded, and the gate on those three rows is the floor E_sub(S) ≥ E_sub(A_S) − 2 dB**, the sum's fundamental against the Anchor's own in the same render — the fundamental never disappears: an anti-phase residual core h1 of ρ ≤ 0.2 subtracts at most 20·log₁₀(0.8) = −1.94 dB from it (worst measured −1.7 dB, Rectify E1) (user ruling G11-3, 2026-10-20, Clarifications "Session 2026-10-20 (build rulings)"). **(c)** The summed fundamental's change equals the coherent-sum prediction **20·log₁₀(\|1 + ρ_S·e^{jθ_S}\| / \|1 + ρ_R·e^{jθ_R}\|)** from the recorded core-only h1 ratio and phase against the Anchor in both chains (ρ_S, θ_S stressed; ρ_R, θ_R reference), within **0.1 dB** (Phase 3 SC-009(i)). The plan's drafted R-coherence premise (ρ_R within 0.05 dB and θ_R within 2°, which reduces the form to 20·log₁₀(\|1 + ρ·e^{jθ}\|/2)) cannot hold on any Phase 9 chain: `ShapeMorph` at SHAPE 0 rotates h1 by −9.9° at E1 / −11.1° at E2 on every algorithm (Phase 5's recorded FR-040 transfer, sc020-h1-transfer.csv: −9.82° / −11.02°), while the core is coherent with the Anchor before it (0.0014°, −0.00006 dB); the −9.9° moves the two-chain prediction by 0.008 dB. So ρ_R / θ_R are **recorded**, and the Anchor-equality premise (\|20·log₁₀(\|A_S\|/\|A_R\|)\| ≤ 0.01 dB) and steady gains stay asserted (same ruling). **OctaveDownSine rows** are recorded, not gated, as in Phase 3: f₀/2 carries no core content. |
| **SC-018** | FOLLOW tracking (H-4) | `VoiceEngine_FollowTracking`: Rectify (on a half-wave-symmetric input, FR-008) and Fracture, at E1 / C3, each in two cases. **(i) Sweep:** Amount 0 → 1 over 10 s, then held at 1 for ≥ 6 s. **(ii) Step:** Amount 0 → 1 at one grid point, with ≥ 6 s holds on either side. **Event:** the interval at which `perceivedPitchRatio()` changes value (bass_character_stage.h:467–475; it returns discrete values), read from the FR-086 follow-ratio tap. **Anchor frequency:** per sample, from `AnchorOscillator::phase()` differences (anchor_oscillator.h:358) through the FR-086 pitch tap. **Reference:** the perceived fundamental of the Character tap, by the Phase 3 offline estimator (anchor_oscillator_test_helpers.h:215), on frames lying wholly within the steady holds before and after the event. **Settling:** the first sample after the event from which the Anchor's frequency stays within ±5 cents of the post-event reference for the rest of the hold. **Gate:** for every event, settling exists, and from it on the error is ≤ **±5 cents**. **Latency** = settling sample − event sample, recorded per case (since G14-13 the event is the ratio's handoff to the Anchor, after FR-007's 4-period dwell; the dwell itself is not in the latency figure). H-12 (the Fracture observer lag at Tone ≥ 0.75) is recorded. If neither stage flips the ratio at any Amount, FOLLOW is flagged inert here with that evidence, and AT-13 carries the flag. |
| **SC-019** | Join point, plain add, core isolation (H-5, H-7) | `VoiceEngine_AnchorJoin`: **(a)** with `anchorDetached` (FR-086) against attached, at g = 0, the core tap and the Character tap are bit-identical. **(b)** Per channel and per sample, the voice's contribution to the sum is bitwise equal to `(c · g_amp) · g_d + (a · g_amp) · g_d` in FR-015's float order, from the Character tap c, the Anchor tap a and the gain taps. The identity holds over the whole render, including intervals with an active dip or steal fade (g_d ≠ 1). Such intervals are tagged in the record. A render with at least one Reset retrigger and one steal is included. **(c)** At g = 1 the direct Anchor path is still present: the Anchor tap is unchanged by g. |
| **SC-020** | Drop routing (H-5) | `VoiceEngine_DropRouting`: a 24 st Drop, in MONO with a 200 ms glide and a ±2 st pitch bend during the Drop, and at 4 voices in POLY. **Independent reference:** a test-side `MonoHandler` (MONO only) + `NoteProcessor` + `DropEnvelope` + `DropPhaseLock`, configured identically and driven from the same event list at the same grid points. `DropPhaseLock` is bypassed in FOLLOW and not otherwise. The reference computes f_res^ref[n] and f_core^ref[n] = f_res^ref[n] · 2^(d_c[n] / 12), independently of the voice's own variables. **Gate:** per sample, the voice's Anchor pitch input (FR-086) equals f_res^ref in LOCK / PUNCH / GHOST and f_core^ref in FOLLOW, and its core pitch input equals f_core^ref, within **1e-4 cent**. If the plan fixes the operation order to match, the gate is bitwise, and this row then says so. **Positive controls:** `followNoBypass` (FOLLOW without the bypass) and `lockReadsCore` (LOCK fed f_core) each fail the gate. |
| **SC-021** | Anti-aliasing (FR-082) | `VoiceEngine_Aliasing` (`[long]`): E0–E2 at 44.1 / 48 / 96 kHz. **Setting:** excitation mix 1 / drive 1 / asymmetry 1; SHAPE ∈ {0, 1}; Fold 100; each of the six Character algorithms at Amount 1 with Tone ∈ {0, 1}, every cell gated and the worst reported; Anchor level 1 in LOCK and in FOLLOW; `anchorFoldFeed` 1; WIDE at spread 1; Mass 100; MONO. **Protocol:** a held note with no Drop, no glide, Motion Off and the morph static. Motion Off is used because Motion's own modulation sidebands are not aliasing. After a 1 s settle, one frame of `aliasFftLength(fs, f_m)` samples is read on L and R separately, and the worst channel is gated. **Estimator:** `aliasedPower(x, aliasFftLength(fs, f_m), fs, f_m)` (dsp/tests/unit/systems/profundum_core_test_helpers.h:257, 273). It is BH7 and Parseval-normalised, and it sums the power of every bin not excluded. It excludes ±8 bins around every n·f_m < fs/2 and bins 0–8. f_m = f₀, except f₀/2 where the lattice needs it: Fracture (the Phase 5 SC-010 n·f₀/2 mask), and FOLLOW with r = 0.5. **(a)** The pre-dynamics sum ≤ **−80 dBFS** (the Phase 2 / 5 bar). **(b)** The engine output, at the same cells plus Mass 100 / Fold 100 (the H-15 setting), with the same estimator. A is also read at the pre-dynamics sum of the same render, and the dynamics-added term is recorded as in Phase 5 SC-010's attribution. **(b) bar: ≤ −80 dBFS (OQ-5 ruled at clarify Q4, 2026-10-19), with the `MassDynamics` oversampling opt-in of FR-082 on; the same cells are also recorded with the opt-in off, so the fix's effect is attributable.** **G14-4 (user ruling 2026-10-21):** the first full grid read every shard red (output −46 … −71 dBFS on the hot cells) and the tap-by-tap probe attributed the content to two gain modulators, not aliasing — the `TruePeakLimiter` riding its gain on a sustained tone whose peaks beat against the sample grid (E2 Tube SHAPE 1 Tone 1 at Mass 100: Sum −78.6, dynamics −70.9, limiter input −71.1, output −47.8 dBFS; unchanged by the G14-1 look-ahead) and the compressor's program-dependent release at heavy gain reduction (E0 Tube SHAPE 0 Tone 0: dynamics tap −66.4 at Mass 100, −105 at Mass 50, −93 standalone with the release pinned). Ruled: **(b) is gated on the oversampled sidechain's own aliasing, FR-082's subject, read standalone with the release pinned by `VoiceEngine_DynamicsAliasingGrid` (Phase 6's Fold-100 saw at E0 / E1 / E2 × 44.1 / 48 / 96 kHz, shaper +12 dB, compression 1, opt-in on at 4×, ≤ −80 dBFS; off and 2× recorded; **re-read GREEN, worst on4x −83.6 dBFS (E0 at 96 kHz), E2 at 44.1 / 48 kHz −88.8 / −85.3 against −45.9 / −55.0 off**, evidence/probes/sc021-dynamics-grid.txt) and the engine output of every grid cell is recorded with the two gain-modulation sources named.** Bar number and mask unchanged; engine changes to both modulators and keeping the output gate were rejected. **(c)** A 48 st Drop with the steepest tail (0.418 st/sample recorded, H-9), measured by Phase 4's rendered aliased-power method **on the core tap** (the tap Phase 4's bar was set on; plan C-21, ratified by the main loop 2026-10-20 — the pre-dynamics sum carries the nonlinear stages' own −80 dBFS floor, so a −96 dBFS bar there is a different bar), ≤ **−96 dBFS** (Phase 4 SC-015 bar). The sum-tap reading of the same render is recorded beside it. **G14-12 (user ruling 2026-10-21, (a)):** the close lane read (a) red on 102 of the 432 cells (−46 … −79 dBFS, 93 of them Tone 1, 79 SHAPE 1, worse at 96 kHz than at 44.1 kHz). Nine probe rounds (`VoiceEngine_AliasingSumProbe`, `VoiceEngine_AliasingCoreProbe` … `Probe4`, run-log/close/g14-aliasing-*.log) attributed it: the content is at n·f₀ ± the note's sample-phase-beat harmonics (≈ 8 / 16 Hz), present at the core tap with the excitation at drive 0 and at any partial count, absent at mix 0; the excitation path's linear skeleton rebuilt from the same primitives reproduces it, and the same skeleton with the two 500 Hz split low-passes in double precision returns to the clean core's floor (−108 → −132 dBFS at 48 kHz, −100 → −124 at 96 kHz). It is float32 rounding of the TDF2 split biquads at fc / fs = 0.01 … 0.0026, coherent with the input, which SHAPE 1's tilt + clipping and Character Tone 1 lift by 25–35 dB; the HarmonicFold detector's window ripple (Phase 2 D-4) adds to it at Mass 100. **Ruled: ProfundumCore's excitation split and HarmonicFold's split run in double precision** (new L1 header `primitives/lowpass_biquad_d.h`, the Biquad Lowpass design in double, float interface; an engine fix, the bar unchanged). Prototype re-read: 102 → **10** failing cells, the standalone excited core −95 … −103 → −129 … −132 dBFS. The ten (44.1 kHz E2 Crush in WIDE at Mass 100, −46 dBFS, SOLID −128; 96 kHz E1 Rectify FOLLOW with the ratio flipping 1 ↔ 2 inside the frame, −7.6 dBFS; E2 Rectify Tone 1 at −78 … −80) were triaged as **G14-13 (user rulings 2026-10-21):** (1) the FOLLOW cells: the FR-008 half-wave flag flipped every period (112 flips in 112.5 periods, `VoiceEngine_AliasingG1413Probe`) because the Anchor at ratio 2 — fed into the Fold through `anchorFoldFeed` — is itself the even harmonic the meter reacts to: flag → ratio → Fold input → flag. **Ruled (first round): a dwell on the FOLLOW ratio handoff** (FR-007, `kFollowRatioDwellPeriods` = 4); **measured:** the dwell alone slowed the cycle to 5 periods (23 flips / 112 periods) and started it on the 48 kHz E2 FOLLOW cell (22 flips); **ruled (second round, 2026-10-21): the FR-008 meter reads the Shape output before the Anchor feed, the dwell kept** — 0 flips on every cell, the 96 kHz cells −90.5 / −97.1 dBFS, the 48 kHz E2 FOLLOW cell −79.7 (green). **Final re-read of the 18 shards on these bits: 432 cells, every shard green; the four recorded E2 Rectify Tone 1 cells read −78.1 (44.1 kHz) and −79.7 dBFS (48 kHz); the worst gated cell is 44.1 kHz E2 Fold SHAPE 0 Tone 1 LOCK at −80.21 dBFS.** (2) the Crush cell: the Fold detector's 0.5 % window ripple (Phase 2 D-4) toggled a Crush quantiser slot sitting on a threshold at that note and rate (SOLID at fractional amounts −127 … −134; the cell −46.5); **ruled: `HarmonicFold::kDetectorReleaseHysteresis` 0.005 → 0.01** (FR-084 exception (6); re-read −121.8 dBFS; G low by ≤ 0.087 dB after a level drop, was 0.043). (3) **the four E2 Rectify Tone 1 cells at 44.1 / 48 kHz (LOCK and FOLLOW) are recorded** at −78 … −80 dBFS: the compounded chain's own aliasing at its hottest Rectify setting on the parity-panned half-spectrum channel (SOLID −85, feed 0 −91, Fold 0 −91, Mass 50 −89, 96 kHz green; unchanged by the hysteresis); a Phase 13 preset note (Rectify at Tone 1 with Fold 100 in WIDE below 48 kHz). Every other cell is gated. |
| **SC-022** | RT safety (FR-060) | `VoiceEngine_RealTimeSafety`: an `AllocationScope` around 10 s of notes, steals, mode switches, morph / macro / Motion automation and resets records **0** allocations, and `static_assert`s check `noexcept`. |
| **SC-023** | Non-finite safety (FR-061) | `VoiceEngine_NonFinite` (`-fno-fast-math`): NaN / ±Inf / denormal on every setter, pitch bend and tempo. The output stays finite and the previous values are kept. |
| **SC-024** | Sample rates (FR-064) | `VoiceEngine_SampleRates`: 22.05 / 44.1 / 48 / 96 / 192 kHz render finite, and **non-silent**: the output RMS is ≥ **−40 dBFS** over 1 s after a 100 ms settle, on a held E1 note at showcase endpoint A. The control grid stays 32 samples. A re-prepare at a new rate and an **unchanged `maxBlockSize`** performs **0** allocations under `AllocationScope`. A larger `maxBlockSize` may allocate; that is configuration-time only (FR-064). |
| **SC-025** | `MotionConfig` selectors click-free (H-26) | `VoiceEngine_MotionSelectorsClickFree` (`[long]`): every ordered pair of `source`, `target`, `cycleShape`, `polarity`, `cycleSync`, `cycleRetrigger` and the representative `cycleNote` × `cycleModifier` subset (Phase 8 handoff 9.8), through the morph, amount 1, depth 1. Max \|Δy\| ≤ 10^(0.5/20) × the direct-setter reference. The differential click count is ≤ the direct-setter reference's count at the same grid point, and both counts are recorded; **the direct-setter reference is issued at the grid point where the morph's stepped selector switches** (the position ramp's midpoint, 3 grid steps after the morph event at the 5 ms ramp; **G14-6, main loop 2026-10-21:** issued at the morph event's own sample it switched 96 samples earlier and, for a rate switch, left the LFO in a different phase — ratios 1.28 … 1.34 that were timing, not clicks; re-read **worst ratio 1.000**, 80 pairs) (the Phase 8 SC-004(b) form, ruled B10-1, specs/profundum-phase8-bass-morph/spec.md:846, 1064). |
| **SC-026** | Motion Depth 0 identity (FR-042) | `VoiceEngine_MotionDepthZero`: every source × target at depth 0 renders bit-identical to Motion Off. |
| **SC-027** | Mass split wired (H-21; FR-027, D-6 ruled Q5) | `VoiceEngine_MassSplit`. **(a) Receivers:** at every interval, the values handed to the main-voice and `MassDynamics` receivers (FR-086) are bitwise equal to the fields of `MassLaw::evaluate(massLaw.smoothedMass(), massLaw.base())`, read after that interval's `advanceInterval()`. That is `currentTargets()` (mass_law.h:185). The Anchor's level is read in (b)–(d). **(b) Lock, Motion not on Mass:** with Motion Off, or `motion.target` ≠ Mass, in Anchor morph Lock, the Anchor's handed level stays bitwise at its p = 0 value while Mass and the morph move the main voice. **(c) Lock, Motion → Mass:** CYCLE → Mass, amount 1, Motion Depth 1, Anchor morph Lock. The handed level stays bitwise at its p = 0 value in every interval (m_a = `resolved.anchorMass`, FR-027). **(d) Blend and Punch, Motion → Mass:** the same automation in each of Blend and Punch. The handed level is bitwise equal, per interval, to `MassLaw::evaluateRow(AnchorLevel row, resolved.anchorMass + that interval's Motion → Mass offset, anchor.level)`, and its peak-to-peak over one CYCLE period is > 0. |
| **SC-028** | Global morph (FR-030) | `VoiceEngine_MorphGlobal`: at 4 voices every voice applies the same `Resolved` per interval, and `advanceInterval` is called exactly ⌈n / 32⌉ times, on grid multiples. |
| **SC-029** | Mode switch (FR-023) | `VoiceEngine_ModeSwitch`: MONO ↔ POLY with held notes leaves no voice sounding past its release and loses no note-off. It produces no click by the SC-005 gate: W runs from the switch's grid point to the end of the longest release, the reference is the same render without the switch and with the notes released at that grid point, (a) and (b) are gated, and (c) is recorded. |
| **SC-030** | Shared headers unchanged except the ruled appends (FR-084) | `git diff --name-only` at close lists no shared header other than `systems/mass_dynamics.h` (FR-082 opt-in, Q4; `true_peak_limiter.h` dropped by plan C-1, ratified 2026-10-20), `primitives/retrigger_declick.h` (R-B) and the headers tasks.md Group 10b names (R-A): `primitives/polyphase_halfband_oversampler.h`, `primitives/tanh_adaa.h`, `processors/harmonic_oscillator_bank.h`, `processors/oversampled_residual_path.h`, `systems/shape_morph.h`, `processors/true_peak_limiter.h`, `processors/harmonic_fold.h`, and from T032 `processors/dynamics_processor.h`, `processors/period_peak_detector.h`, `processors/keytracked_tilt.h`, `processors/spectral_shape_recipe.h`, `systems/profundum_core.h` (FR-084 (3), T032 `setControlsWithShape`; reconciled by T026, 2026-10-21) (FR-084 lists each gate). For each listed header the consumer suites named in FR-084 run green under the change-set rule and the result is quoted; any other listed shared header is a red. **The gate on each listed header is that consumer evidence, not the diff's shape (R-A: "append-only / bit-identical fixes in the Phase 1–8 components, each header gated by its phase's suites"; main loop, T026, 2026-10-21):** `mass_dynamics.h` carries the T029 sidechain mechanism inside `BassCompressor` (`configureDetector` reads the sidechain rate `scRate_`, which equals `sampleRate_` with the opt-in off; `processBlock`'s loop body moved into a helper and its detection step is T032's `processDetectionOnly`), so `sc030-diff.js` prints every removed line of that header with its enclosing function and a relocated / rewritten tag as a **record**, and the Phase 6 suites (`[mass-dynamics]`, `[period-peak]`, `[dynamics]` green at T029 batch 2 and T032 batch 3–4, FR-084) are the gate. `core/db_utils.h` stays a hard red (R-D). |
| **SC-031** | POLY allocation order (FR-022) | `VoiceEngine_StealOrder`: with 4 voices, the victim is the oldest releasing voice when one exists, else the oldest active, over a seeded 1000-event schedule. |
| **SC-032** | Grid call order (FR-026) | `VoiceEngine_CallOrder`: the FR-086 call log records one interval's calls and matches the FR-026 sequence exactly, for MONO and 4 voices. |
| **SC-033** | Rectify input rule and phase sync (H-11) | `VoiceEngine_RectifySymmetry`: `setInputHalfWaveSymmetric` is true exactly when the measured Character-input h2 is below −20 dB re h1, for states outside the −24 … −16 dB band. **Phase sync (FR-008):** the call log shows `syncPhase(0.0f)` at exactly the FR-008 events, and nowhere else. Those events are every Reset restart, on its effective sample, and the first note-on after `prepare` / `reset`. At each of these events, the core tap's h1 `goertzelPhase` over the next 4 periods equals that of a fresh, prepared core given the same Reset note-on, within 1° (the Phase 4 SC-009(b) method), under both retrigger policies. |
| **SC-034** | Pitch bend (FR-024) | `VoiceEngine_PitchBend`: a full bend at ranges 2 and 12 st moves f_res by the range within 0.1 cent, in MONO and POLY. |
| **SC-035** | Listening sheet (FR-071) | `evidence/listening-sheet.md` holds one entry per Listen row of FR-070, each with a render path and the user's verdict, before comply closes. |
| **SC-036** | Test taps transparent (FR-086) | `VoiceEngine_TapTransparency`: SC-011's schedule, in MONO and POLY, with the observer attached and every seam off, renders bit-identical to the same schedule with no observer. With no observer attached, SC-022's `AllocationScope` records 0 allocations. |

---

## Edge Cases

- **RT boundaries.** A steal or note event lands mid-block: it is applied at the next grid point (FR-063), so there is
  no partial-interval state change. A `prepare` is never called from the audio thread. A host block larger than
  `maxBlockSize` is split.
- **Zero voices sounding.** `setDetectorKeyHz(0)` (80 Hz corner). Motion and the morph keep advancing, so a note
  starting later hears the current state. The global chain keeps running, so the DC and limiter tails decay naturally.
- **Every voice busy and every voice active (POLY).** The oldest active voice is stolen (FR-022). A same-note retrigger
  follows `VoiceAllocator`'s shipped rule ("Handles same-note retrigger (FR-012)", voice_allocator.h:222–223, 238).
- **MONO stack overflow.** `MonoHandler::kMaxStackSize` is 16 (mono_handler.h:109). The shipped behaviour is kept.
- **Parameter extremes.**
  - Mass 0 and 100, with Motion pushing past them: clamped by `MassLaw::evaluateRow`.
  - Macro and Motion offsets driving a field out of range: clamped per field.
  - Drop 48 st with time 1 ms.
  - Glide time 0 and 10 s (`kMaxPortamentoTimeMs = 10000`, mono_handler.h:112).
  - Fold 100 with feed 1.
  - All four macros at 0 or 1 together.
  - CYCLE at 20 Hz on Mass: attenuated by the slew and recorded.
- **Lowest notes.** E0 (20.6 Hz) with the Low Note Guard, the Drop landing below `kMinF0Hz` (8 Hz): the components
  clamp, and the engine adds nothing.
- **Sample-rate change.** A re-`prepare` at a new rate resets every component and preserves the engine setters'
  pending values and macro values. The morph endpoints are restored by the Phase 8 order: `setEndpoint` ×2 then
  `restoreRuntime` (handoff 11.2).
- **Seed determinism.** A seed change takes effect at the next `prepare` / `reset` (drafted, the Vorago `setSeed`
  pattern). A mid-stream seed change never jumps a running DRIFT.
- **Tempo.** A missing host tempo falls back to 120 BPM. Tempo changes while synced, and a non-finite tempo keeps the
  previous value.
- **Mode switch during glide or Drop.** The voice is released (FR-023), and the next note starts clean.
- **Anchor morph Lock while B's Anchor is edited.** The edits are stored but inaudible (Phase 8 handoff 12.3). The
  engine does not mirror them.
- **SOLID entered with L ≠ R tails.** This is governed by FR-029.
- **Denormals.** The components handle their own. The engine relies on FTZ/DAZ being set by the host wrapper, and the
  tests set it.

## Decisions (drafted for the clarify / plan stages to rule)

| # | Decision | Drafted choice | Why |
|---|---|---|---|
| D-1 | Amp-envelope class (OQ-1) | **Ruled Q6:** `ADSREnvelope`, table path always (FR-009) | It realises all seven `AmpEnvelopeFields` with float curves. `MultiStageEnvelope` would make three fields deviations (F-5) |
| D-2 | Motion bus, given F-1 | **Ruled Q1:** explicit routing, no shared header (FR-048). Drafted as Option D of FR-048: explicit routing inside `ProfundumMotion` over `MotionConfig`'s fixed target set. The output is exposed as a `ModulationSource`. `VoiceModRouter` is not composed, and its header is not changed | The roadmap names `VoiceModRouter` (line 666), but its sources and destinations are Ruinae's (voice_mod_types.h:29–62). Extending it is a shared-header change, and both precedent voices keep it off the audio thread (AR-2). **This contradicts the roadmap's wording, so it must be ruled, not assumed.** FR-048 states both options with their gates. Option R appends the Profundum destinations to `VoiceModDest`, append-only, gated by the Ruinae and Vorago consumer suites. Option D is a recorded roadmap deviation that amends roadmap line 666 at comply |
| D-3 | Motion as its own class | `ProfundumMotion` (L3) | It is separately testable (SC-009, SC-025, SC-026). The plan may fold it into the engine if it adds nothing |
| D-4 | Order of control contributions | **Confirmed at clarify (main loop):** resolved morph → macro offsets → Motion offsets → Mass biases (FR-026 steps 2–5) | Macros are global and not morphed (FR-053). The Anchor a₁ is taken before the Mass biases (G8-3) |
| D-5 | ENV scope in POLY | **Confirmed at clarify (main loop):** one engine-level ENV, retriggered by envelope-restarting note events (FR-045); Motion is a global section (roadmap line 666) | Mass is global (line 668) |
| D-6 | Motion → Mass and `anchorMass` | **Ruled Q5:** Motion reaches `anchorMass` in Blend / Punch only, never in Lock (FR-027; SC-027(b)–(d)) | The Anchor's "weight" swells with the voice. In Lock the Anchor morph stays at A, but Motion still moves it |
| D-7 | Macro neutral | **Confirmed at clarify (main loop):** 0.5 for all four, contributing exactly 0 (FR-051) | Bipolar names (Dark ↔ Bright …). Weight was drafted the same way for a uniform surface |
| D-8 | Steal fade | Serve-time capture and fade (Vorago D3 / D4) plus the `RetriggerDeclick` dip for restarts — **left to plan** (the main loop at clarify saw no user-facing choice in it; SC-005's positive controls decide) | It meets SC-005 without rendering ahead |

## Open Questions (only those the roadmap defers to this spec)

- **OQ-1 Amp-envelope class** — **ruled Q6:** `ADSREnvelope`, table path always (FR-009).
- **OQ-2 POLY placement and FOLLOW per voice** — roadmap OQ-7 (lines 1008–1009): which nonlinear stages run per voice
  vs post-sum, and whether FOLLOW is affordable per voice. **Resolved at clarify (main loop):** per voice is shipped
  unless SC-004 shows it breaches SC-002 while post-sum meets it (FR-014); the value is written at build.
- **OQ-3 Polyphony ceiling** — roadmap OQ-8 (lines 1010–1011): the `kMaxVoices` value, given that Phase 11's
  registered maximum "is chosen once". **Ruled Q8:** `kMaxVoices = 8`, default 4, an 8-voice CPU point recorded
  (FR-022, SC-002, FR-085).
- **OQ-4 Worst-interval budget** — Phase 8 handoff 9.7: "Not gated; Phase 9 rules on it against its 8 % MONO / 25 %
  4-voice ceilings". The choice is whether the ceilings are averages over a render, as the roadmap states them, with
  the per-interval peak recorded, or whether a per-interval bound applies as well. **Ruled Q3:** averages gated as
  written, plus the worst 64-sample host block ≤ 100 % of a core; the interval peak recorded (SC-003).
- **OQ-5 Dynamics aliasing at the output** — Phase 6 handoff (4) (roadmap lines 486–488): "Phase 9's chain budget
  decides 2× oversampled gain application plus 8× detectors (≈ +0.5 % of a core)". This decides SC-021(b)'s dB bar.
  **Ruled Q4:** the opt-in is added to `MassDynamics` (append-only, off by default) and SC-021(b)'s bar is −80 dBFS
  (FR-082, FR-084, SC-030).
- **OQ-6 Envelope neutral curve and immediate receivers** — Phase 8 handoff 9.3, "state which of the two it treats as
  the neutral value". The two are the default exponential path and the table path at 0.7. Handoff 9.4 adds that the
  envelope class "decides whether the immediate receivers get a smoother or are accepted with the recorded step".
  **Ruled Q6:** the table path at 0.7 is neutral; the immediate receivers get no smoother and their step is recorded
  (FR-009).
- **OQ-7 SOLID re-synchronisation** — Phase 7 handoff §2: "Phase 9 decides: re-synchronise the DC and limiter states
  at the crossfade end … or accept the measured ≤ 243 ms convergence" (FR-029). **Ruled Q7:** re-synchronise at the
  crossfade end; L == R bitwise from there (FR-029, SC-015).
- **OQ-8 Anchor join point / phase reference** — Phase 8 handoff 9.10: "Phase 9 decides the Anchor join point / phase
  reference with this in hand". The default join stays post-Character (roadmap line 689). The question is whether any
  phase alignment is added, given that the regenerated fundamental's phase rotates. A plain add is required by SC-019,
  so any alignment would have to be on the Anchor's own phase reference, not a compensation stage. **Resolved at
  clarify (main loop):** unchanged join point and phase reference; SC-017 bounds the sum, ρ / θ recorded (FR-002).
- **OQ-9 Component switch clicks** — Phase 8 handoff 9.11: "Phase 9 decides whether Phase 5 / Phase 3 re-measure them
  with their own detectors". **Resolved at clarify (main loop):** not re-measured here; Listen rows on the listening
  sheet (FR-071).

## Traceability

| Roadmap statement (Phase 9, lines 655–697) | FR | SC |
|---|---|---|
| Voice chain order; Anchor joins after Character | FR-002 | SC-019 |
| Drop on pitch; amp envelope ruled in spec | FR-006, FR-009 | SC-020; OQ-1 |
| POLY placement by CPU measurement | FR-014 | SC-004; OQ-2 |
| `anchorFoldFeed`, direct path always present | FR-003 | SC-019(c), SC-010 |
| MONO via `MonoHandler` + Phase 4 behaviour | FR-021, FR-010, FR-011 | SC-006 |
| POLY via `VoiceAllocator`, 4 voices, steal order | FR-022 | SC-031, SC-005; OQ-3 |
| Global chain order | FR-025 | SC-015, SC-021 |
| Motion ENV / CYCLE / DRIFT, Motion Depth, fixed destinations | FR-040–FR-047, FR-049 | SC-025, SC-026, SC-010 (AT-34 … AT-40) |
| Motion "→ `VoiceModRouter` as the modulation bus" (line 666) | FR-048 — **ruled Q1: explicit routing in `ProfundumMotion`; recorded roadmap deviation**, line 666 amended at comply (FR-085) | SC-026, SC-009; D-2 |
| Motion → Mass | FR-044 | SC-009 |
| Personality macros, coordinates stored | FR-050–FR-056 | SC-007, SC-008 |
| Global Bass Morph; Mass law wired | FR-030, FR-027 | SC-028, SC-027 |
| Determinism harness | FR-062 | SC-011, SC-012 |
| Capability audibility table: every section's on/off and every macro / lever | FR-080 (rows AT-01 … AT-50), FR-086 | SC-010, SC-036 |
| CPU 8 % / 25 % | FR-083 | SC-001, SC-002, SC-003 |
| Clickless steal / replacement | FR-016 | SC-005, SC-006 |
| 30 min soak, no DC build-up | FR-062, FR-061 | SC-013 |
| Mono sum, every image mode | FR-081 | SC-015, SC-016 |
| Anchor through Fold + Character; coherent-sum bound | FR-002, FR-005 | SC-017 |
| FOLLOW through a pitch-moving stage | FR-007 | SC-018 |
| Anchor–h1 lock in the soak with Motion | FR-043 | SC-014 |
| Core bit-identical with the Anchor attached/detached; plain add | FR-004 | SC-019 |
| Phase 4 handoff: 0.418 st/sample for aliasing | FR-082 | SC-021(c) |

## Assumptions

- The Phase 1–8 components are used as shipped at `0ae33b87`. Their own bars are not re-gated here, except where a
  clause was explicitly deferred to Phase 9 (FR-070).
- "Everything on" for CPU is the SC-001 setting. The plan may name a heavier cell if Phase 5's records show one.
- The showcase setting is Phase 8's `showcasePair()`. A lever inert on both endpoints gets a documented per-row
  setting in the audibility CSV.
- The test executable is `dsp_systems_tests`. Gates follow the change-set rule (phase-tag gates; `[long]` cases of this
  phase's TUs only; consumer suites only for modified shared headers).

## Clarifications

### Session 2026-10-19 (specify-stage rulings; each was written into its FR / SC row first)

- **Q1 — Motion bus?** Ruled: **explicit routing inside `ProfundumMotion`, no shared header.** `VoiceModRouter` is
  not composed; its Ruinae-specific sources and destinations would have needed a shared-header append gated by the
  Ruinae and Vorago suites. Recorded roadmap deviation; line 666 amended at comply. The output is exposed as a
  `ModulationSource`. The router append and the hybrid were rejected. [FR-048, FR-084, FR-085, D-2, SC-030]
- **Q2 — Motion offset scale and polarity?** Ruled: **half-span** — |offset| 1 = half the target's range (Mass ±50,
  Depth / Body / Edge ±0.5, Shift ±1); Bipolar passes LFO / Drift natively and maps ENV 2e − 1; Unipolar passes ENV
  natively and maps LFO / Drift (x + 1) / 2. Full-span and "ENV always unipolar" were rejected. [FR-042, SC-009, SC-014]
- **Q3 — Worst-interval budget?** Ruled: **the 8 % / 25 % ceilings are render averages and stay gated; on top, the
  worst 64-sample host block costs ≤ 100 % of one core** (no dropout). The worst control interval is recorded against
  Phase 8's 81.6 %. A per-interval 25 % bound (staggered switches) and "averages only" were rejected. [SC-003, H-25]
- **Q4 — Output aliasing bar and the dynamics fix?** Ruled: **`MassDynamics` gains 2× oversampled gain application
  plus 8× detectors as an append-only opt-in (off by default, on in the engine), and the engine output is gated at
  −80 dBFS**, the same bar as the pre-dynamics sum. Phase 6's own TUs gate the header change. −60 dBFS and a relative
  bar were rejected. [FR-082, FR-084, SC-021(b), SC-030, H-15]
- **Q5 — Does Motion → Mass move the Anchor's Mass input?** Ruled: **only outside Lock.** Blend / Punch: m_a =
  `anchorMass` + the Motion offset; Lock: m_a = `anchorMass`, so Lock's constant Anchor is literal. "Always" and
  "never" were rejected. [FR-027, D-6, SC-027(b)–(d)]
- **Q6 — Amp envelope class, neutral path, immediate receivers?** Ruled: **`ADSREnvelope` on the table path always**
  (the three float curve setters at prepare and on bitwise change; 0.7 is the table path, never the enum path); time
  and sustain apply immediately with the step recorded, no smoother. The enum-at-0.7 path and `MultiStageEnvelope`
  were rejected. [FR-009, D-1, OQ-1, OQ-6, SC-006, SC-010, SC-011, H-22]
- **Q7 — SOLID re-synchronisation?** Ruled: **re-synchronise at the crossfade end** — `DCBlocker::setState` L → R on
  both blockers and the limiter's per-channel state copied L → R (mechanism in plan; an append-only accessor under
  FR-084 if none exists). L == R bitwise from the crossfade end, no settle. Accepting the convergence was rejected.
  [FR-029, FR-084, SC-015, SC-030, H-18]
- **Q8 — Polyphony ceiling?** Ruled: **`kMaxVoices = 8`**, active polyphony [1, 8] default 4; SC-002 stays gated at 4
  voices and an 8-voice CPU point is recorded; Phase 11 registers 1–8 once. 6 and 4 were rejected. [FR-022, SC-002,
  FR-085, OQ-3]

### Session 2026-10-20 (plan-stage ratifications by the main loop; each was written into its FR / SC row first)

- **C-1 — No `TruePeakLimiter` state copy at the SOLID re-sync.** Ratified: the limiter's gain is shared across L / R
  (true_peak_limiter.h:142–164), so equal inputs give equal outputs; only the DC blockers are copied. SC-030's list is
  `mass_dynamics.h` only. [FR-029, FR-084, SC-015, SC-030]
- **C-3 (192 kHz clause) — The `MassDynamics` opt-in is a recorded no-op at 192 kHz.** Ratified: SC-021 reads 44.1 /
  48 / 96 kHz; a 384 kHz compressor would be a second shared-header change. [FR-082, SC-021(b)]
- **C-7 — The Tight ↔ Loose compression row applies after the Mass law.** Ratified: clamp(law + c_TL, 0, 1), add
  skipped at ±0; SC-027(a) compares against that expression. [FR-052, SC-027(a)]
- **C-21 — SC-021(c) is gated on the core tap, the sum tap recorded.** Ratified as a reading refinement: Phase 4's
  −96 dBFS bar was set on the Drop through the core; the sum carries the nonlinear stages' −80 dBFS floor. [SC-021(c)]
- **Task-cutting deviations accepted:** CMake registration first; the voice tested first through its own API; engine
  cases split across T008 / T011; the fingerprint as a separate case after the harvest; the CPU lane run per case under
  pin-perf-cores; the consumer `[long]` list computed from `mass_dynamics.h`'s include closure; P-1's per-stage
  breakdown from a test-side replica.
- **Deferred to their probes (STOP checkpoints, user rulings when measured):** R-A (CPU sizing predicts 10.3 % MONO /
  30.8 % at 4 voices against 8 % / 25 %; levers offered after P-1, never the ceiling), R-B (declick vs SC-005(a) /
  SC-006(a) reference after P-2), R-C (opt-in short of −80 dBFS after P-3).

### Session 2026-10-20 (build rulings; each was written into its FR / SC row first)

- **R-A — CPU over the ceilings (P-1: MONO 13.6 % / 8 %, 4 voices 46.3 % / 25 %; static morph 10.5 % / 31 %).** Ruled:
  **an optimisation group inside Phase 9** (tasks.md Group 10b, run from the main loop): profile under the SC-001
  setting, behaviour-preserving fixes in the engine and append-only / bit-identical fixes in the Phase 1–8 components,
  each header gated by its phase's suites, P-1 re-measured; the ceilings stay. Raising the control interval to 64 and a
  separate Phase 9b were rejected. [FR-083, FR-084, SC-001, SC-002, SC-003, SC-030]
- **R-B — The 1.5 ms dip out-slopes the no-steal reference on clean E1 notes (P-2 after the defect fix: four cells fail
  (a) by 7–51 %, every cell passes (b)).** Ruled: **the voice dips over 3 ms** through an append-only length parameter
  on `RetriggerDeclick` (Phase 4's default untouched); (a) and (b) both stay gated. Gating (b) only and changing
  Phase 4's constant were rejected. [FR-011, FR-016, FR-084, SC-005(b), SC-006, SC-030]
- **Defect fix (main loop, P-2 locator, 2026-10-20):** the voice resets its Shape / Fold / Character state at the
  declick switch sample; the chain's memory of the old note was reappearing at full gain (0.28 steps on the sum).
  [FR-011]
- **B10-2 — Dark ↔ Bright's SC-007 sweep breaks on B_s at macro 0.55–0.65 (the SHAPE row: ShapeMorph's centroid is
  non-monotone over its regimes).** Ruled: **drop SHAPE from Dark ↔ Bright** (row kept at amount 0); Edge, Shift and
  Tone remain. Exempting SHAPE from the gate and a one-sided row were rejected. [FR-052, SC-007, SC-010 AT-48]
- **Q4 factor refinement (main loop, after P-3's attribution, 2026-10-20):** the `MassDynamics` opt-in runs 4× at
  44.1 / 48 kHz (2× reached only −69.5 / −73.0 dBFS; 4× reaches −96.9 / −90.8 dBFS); the compressor is the sole source.
  The −80 dBFS bar is unchanged. [FR-082, SC-021(b)]
- **T029 lever 2 (main loop, 2026-10-20): the opt-in oversamples the sidechain only.** Oversampling the gain
  application alone changed nothing (on == off): the reading is detector jitter. The high-pass and held-peak detector
  now run at 4 fs on the upsampled input through append-only `BassCompressor` entries (default off, bit-identical);
  gain computer, shaper, trim and audio stay at the base rate, zero latency, the requested factor at every rate.
  P-3 GREEN at −89.5 dBFS worst (48 kHz). T005's group-delay expectation (plan S7 ~3.8 samples) becomes 0.
  [FR-082, SC-021(b), SC-030]
- **T031 lock-step L / R (user ruling, 2026-10-20, on T030's idle reading MONO 9.20 % / 4 voices 30.21 %; options
  offered: lock-step rewrite, control-path trace first, accept the residual).** Ruled: the rewrite, inside Phase 9,
  stage by stage with a paired reading after each, bit-identical per lane; bars unchanged. [FR-083, FR-084, SC-030]
- **T032 outcome (main loop, 2026-10-20).** Six lever batches in all (T029 two, T032 four): P-1 GREEN at MONO
  7.26 % / 4 voices 23.03 % on the idle machine. Of the eleven touched headers nine are bit-identical for every
  existing caller (fingerprint digits), two are tolerance-level (the Fold's sine, Phase 2 bars; the tilt's gain pair,
  Phase 5 bars) and one engine-internal change (the shared a₁ recipe) moves the render in the 9th significant figure
  through the compiler's vectorised exp at a second call site. The determinism fingerprint and the macro rows are
  re-harvested on the final bits before Group 11. [FR-083, FR-084, SC-001, SC-002, SC-004, SC-030]
- **T031 measured dead; T032 ruled (user, 2026-10-20).** The halfband lock-step variants were all slower than the
  scalar code (1.06–1.22×); the user chose one more lever round (control path, dynamics dB path, pan coefficients)
  over accepting MONO 9.20 % / 4 voices 30.21 %. [FR-083, FR-084, SC-030]
- **T028 / T029 levers (main loop, 2026-10-20).** The Release exe carries no PDB, so two traces resolved only the C
  runtime; a third trace against a `/DEBUG`-linked copy (`build/windows-x64-prof`, same `/O2 /Ob2` codegen) named
  the engine's functions. Seven headers were touched, six bit-identical (proved by the fingerprint harvest's digits)
  and one tolerance-gated (the Fold's sine, Phase 2 bars). Readings are paired A/B on the static-morph setting; the
  absolute level drifts with the machine's load (the same binary read 11.5 % at 12:54 and 13.6 % at 17:50), so only
  pairs are quoted. The determinism fingerprint constants (T017) are re-harvested once the group closes. [FR-083,
  FR-084, SC-030]

- **B10-1 — Dark ↔ Bright inert on its harvest base (T014 HARVEST RED).** On A_s (the pure-sine anchor recipe: depth 1,
  edge 0, Shape 0, Character Amount 0) Edge, Shift, Shape and Tone all reach their clamps by 2× the seed and the
  centroid moves 0.016 oct against the 0.25 oct bar; the other three macros passed on the same base. Ruled: **per-row
  base — Dark ↔ Bright is harvested, swept (SC-007) and tabled (AT-48) on B_s = showcase B with sustain 1**, under
  FR-080's documented per-row rule; targets, amounts' search and the bar unchanged; the A reading is recorded. Adding
  recipe depth ↓ as a Bright target (depth in two macros) and A with Character Amount 0.5 were rejected. [FR-052,
  FR-080 AT-48, SC-007, SC-010]
- **G11-3 — SC-017's R-coherence premise cannot hold and (b) is unreachable for the h1-removing algorithms (user
  ruling, 2026-10-20; options offered: general form + split (b), (b) recorded for all six, keep 4.1 dB everywhere).**
  `ShapeMorph` at SHAPE 0 rotates h1 by −9.9° on every chain (Phase 5 FR-040), and Rectify, Crush and Fracture remove
  or invert the core's h1 at Amount 1, so the sum falls by 6.0–7.8 dB while the Anchor's own w₁ is unchanged. Ruled:
  **(c) uses the two-chain prediction with ρ_R / θ_R measured and recorded (0.1 dB bar unchanged); (b) stays at 4.1 dB
  for Tube, Fold and Bite, and is recorded for Rectify, Crush and Fracture under the floor gate E_sub(S) ≥ E_sub(A_S)
  − 2 dB.** [SC-017, FR-002]
- **G11-2 — AT-26's A cell reads +1.78 dB (bar 3 dB) because A's anchor level 1.0 sits at the Mass law's AnchorLevel
  clamp (user ruling, 2026-10-20; options offered: per-row base A + anchor level 0.5, A recorded with B gated, change
  the Phase 6 row).** Ruled: **the per-row setting A + anchor level 0.5** under FR-080's documented rule, the plain
  A_s reading recorded beside it; bar and B cell unchanged. [FR-080 AT-26, SC-010]
- **G11-4 — SC-013's DC window 0 reads −68 dBFS at the output while the DC blocker tap reads −115 dBFS: the
  `TruePeakLimiter`'s gain dips on a hot asymmetric onset leave a DC lobe (user ruling, 2026-10-20; options offered:
  gate at the DC-safety tap with the trend at the output, a second DC blocker after the limiter, limiter look-ahead).**
  Ruled: **the −80 dBFS per-window bar is gated at the DC-safety tap, the output's windows are recorded, and the
  no-build-up trend clause stays gated at the output.** No engine change. [SC-013, FR-025, FR-061]
- **T026 findings (main loop, 2026-10-21).** (1) `tools/lint-float-bit-goldens.js` (a CI step) flagged Phase 3's
  `profundum_anchor_chain_test.cpp`: an FNV-1a over the float bits of a core render, printed once per Highway target
  and never compared — red on the tree since Phase 3 (Phase 8's portability lane did not run this lint). Fixed in that
  TU: the diagnostic prints the double sum of \|x\| instead (its last digits move with the target's rounding just the
  same); lint clean, the ISA smoke re-run. No header touched. (2) The SC-030 diff script read plan S7's "off path
  executes exactly the existing statements" literally and reddened `mass_dynamics.h`'s T029 shape (`configureDetector`
  on `scRate_`, the loop body in a helper, `processDetectionOnly`). R-A already rules bit-identical fixes gated by
  the consumer suites, so the row says so, the script records the hunks, and `systems/profundum_core.h` (FR-084 (3))
  is added to the row's list. [SC-030, FR-084, R-A; portability]
- **G14-1 — SC-013 true peak +1.017 dBTP over the 30 min soak (user ruling, 2026-10-21; options offered: opt-in
  look-ahead on `TruePeakLimiter`, gain applied at 4× inside the limiter, gate the sample peak and record the true
  peak).** The zero-look-ahead limiter clamps samples exactly but steps its gain on hot asymmetric material, a clipper
  whose reconstruction overshoots between samples (identical with the T029 halfband detector off; it also adds the
  −46 dBFS off-lattice content on SC-021(b)'s hot cells: limiter input −71, output −46). Ruled: **append-only,
  default-off `setLookaheadSamples` on the limiter (mean over N of the running min over N + 1 of ceiling / tp,
  applied to the N-sample-delayed audio), engaged by the engine at 64 samples and reported by `latencySamples()`.**
  Bar unchanged; SC-021(b) and P-1 re-read on the final bits. [SC-013, SC-021, FR-025, FR-084, SC-030, SC-001, SC-002]
- **G14-2 — SC-013 DC −68.6 / −63.6 dBFS in the soak's activity windows (user ruling, 2026-10-21; options offered:
  gate the windows without note events, keep the bar on every window, shorter windows).** A DC step Δ at the 5 Hz
  blocker leaves Δ·τ / 10 s (−63.6 dBFS for Δ 0.21), so the bar is unreachable in a window with an asymmetric voice's
  note-on / note-off. Ruled: **the −80 dBFS per-window bar is gated over the windows without note events (the lock
  segments: −97 … −133 dBFS), the windows with note events are recorded, the trend clause stays gated at the output.**
  [SC-013, FR-061]
- **G14-9 — FR-021's `MonoNoteEvent::retrigger` is never read (user ruling, 2026-10-21; options offered: Note Memory
  decides and amend the text, consult the bit too).** Ruled: **the Note Memory transition and policy decide; the
  MonoHandler legato bit gates its glide only.** No engine change. [FR-021, FR-010]
- **G14-4 — SC-021(b) red on every shard (user ruling, 2026-10-21; options offered: gate (b) on the dynamics'
  aliasing and record the output, engine changes to both gain modulators, keep (b) on the output).** The output's
  off-lattice content is the limiter riding its gain on a sustained tone (E2: −71 in → −48 out, unchanged by the
  look-ahead) and the compressor's program-dependent release at heavy gain reduction (E0 Mass 100: −66 vs −105 at
  Mass 50; −93 standalone, release pinned). Ruled: **(b) gates the oversampled sidechain's own aliasing standalone
  (`VoiceEngine_DynamicsAliasingGrid`, E0 / E1 / E2 × three rates, 4×, ≤ −80 dBFS) and records the engine output per
  cell with the two sources named.** [SC-021, FR-082]
- **G14-5 — SC-006 at the +24 st C3 replacements on B (two user rulings, 2026-10-21).** (a) read 1.064 per channel on
  the stereo bases and 0.78 in SOLID: the per-partial pan vector re-targets at the jump and the bank applies it at
  once (options offered: ramp the pan gains over the declick length, record (a) for large stereo jumps, slew inside
  `BassStereoField`) — ruled the opt-in pan ramp on `ProfundumCore`; **it measured no effect (1.068) and was reverted;
  the per-channel probe found WIDE's +0.15 Character offset on L (SPREAD 0.957, SOLID 0.78), i.e. the warm Character
  stage at a two-octave legato jump without a dip. Re-ruled (v2; options: record (a) for legato ≥ 24 st on WIDE
  bases, clear the Character stage at such jumps, keep (a) gated): recorded for those seven cells, gated everywhere
  else.** (b) read 0.684
  against 0.653 while the fresh POLY-2 reference read 0.681 (options offered: D_max from note-on, keep the
  after-settle D_max, soften the chain's onset) — ruled **D_max / P_max from note-on, the after-settle bound
  recorded**. [SC-006, SC-005, FR-011, FR-016, FR-084, SC-030]
- **Defect fix (main loop, 2026-10-21, G14-3 POLY): the voice's Drop trigger used stale setters.** The POLY soak's
  lock notes dropped by the activity's last Drop amount (+3.3 / +33.5 st for 1–2.5 s, 94° of SC-014 drift) although
  the resolved state at the note-on read Drop 0: the transition triggered the Drop before the idle voice's interval
  setters had applied the new amount. The trigger is now deferred past this interval's Drop setters
  (`flushDropTrigger`); a declick switch flushes it at once. Phase 4's matrix and the Drop rows re-read. [FR-006,
  FR-013, SC-014, SC-025 Drop rows]
- **Group 14 refinements (main loop, 2026-10-21; each in its row, each overturnable).** SC-014's lock segments at
  excitation mix 0 (the core tap is post-excitation; MONO re-read 0.078°); SC-003's coverage on the switch blocks;
  SC-025's direct-setter reference at the morph's switch grid (1.000 re-read); FR-080: AT-24 → t_90, AT-32 → the band
  below f_c (C3 recorded: f₀ > f_c), AT-29 / AT-31 / AT-30 read SOLID's silent side against the floor, AT-30 at
  spread 1, the frame-wise D_br full-band, AT-39 per held span, per-row bases by FR-080's rule measured on both
  endpoints (B_s for AT-27, AT-33, AT-35, AT-36, AT-38; the AT-26 anchor-0.5 setting for AT-34, AT-39, AT-40), and
  AT-26's B cells recorded at C3 where the A per-row cell carries the gate (B and B_s both +1.84 dB: Mass 100's
  Depth bias lowers a₁ against the Anchor-level rise). Harness fixes: the short-render L_K record, the SOLID floor
  rule, the lint-red Phase 3 diagnostic. [SC-014, SC-003, SC-025, FR-080, SC-010] SC-015's SOLID identity read from
  the DC-blocker copy plus the limiter's look-ahead latency (G14-1 moved the engine's latency from 0 to 64 samples;
  exactly 64 L ≠ R samples after the copy); the soak smoke's lock segment 15 → 25 s so its G14-2 trend holds two
  windows without note events (the 20 s smoke held one; the [long] halves hold 24). [SC-015, FR-029, SC-013]
- **G14-12 (user ruling 2026-10-21): the split low-passes in double precision.** SC-021(a) read red on 102 of 432
  cells at the close; the content was the float32 TDF2 split biquads' rounding (ProfundumCore excitation split,
  HarmonicFold split; 500 Hz at 48–192 kHz inner rate), coherent with the input and lifted by the chain's later
  stages — not aliasing (the probe chain is in the SC-021 row). Ruled: both splits in double (`lowpass_biquad_d.h`,
  a new L1 primitive), the bar unchanged; the same change was offered for the core only and as a record-and-hand-off
  and both were rejected. Consequence for every earlier phase's chain readings: a −100 dBFS floor they carried is
  gone; their bars were met before and after. The consumer lane re-runs over the include closure of every modified
  header (T025-3). [SC-021, FR-082, FR-084, SC-030]
- **G14-13 (user rulings 2026-10-21): the ten cells after G14-12.** (1) FOLLOW loop → a 4-period dwell on the ratio
  handoff (FR-007); (2) HarmonicFold release hysteresis 0.5 % → 1 % (FR-084 (6)); (3) E2 Rectify Tone 1 below
  96 kHz recorded (SC-021 (a)). Each in its row with the probe readings. Second round (same day): the dwell alone
  measured a 5-period cycle, so the FR-008 meter moved to the Shape output before the Anchor feed (the dwell kept);
  0 flips everywhere, SC-018 / SC-033 green. [FR-007, FR-008, FR-084, SC-021]

Resolved by the main loop at clarify (no user-facing choice; flagged here so they can be overturned):

- **OQ-2 POLY placement** — per voice is shipped; post-sum only if SC-004 shows per-voice breaches SC-002 while
  post-sum meets it; FOLLOW the same on its own. The ceiling is never the lever. [FR-014, SC-004]
- **OQ-8 Anchor join point / phase reference** — unchanged; the rotating regenerated fundamental admits no fixed
  alignment, SC-017(b)/(c) bound and predict the sum, ρ / θ recorded for Phase 13. [FR-002, SC-017]
- **OQ-9 Component switch clicks** — not re-measured here; Listen rows on the listening sheet; the engine gates only
  what it adds (B10-1 form). [FR-071, SC-005, SC-006, SC-025]
- **D-4** contribution order, **D-5** one engine-level ENV in POLY, **D-7** neutral 0.5 for all four macros and the
  **FR-052** target rows are confirmed as drafted. **D-8** (steal fade mechanism) is left to plan. **SC-009**'s 3 dB
  swell bar is confirmed (Phase 6 SC-011's Mass audibility bar). **FR-080**'s drafted bars stand (the Phase 5 / 8
  family); any bar the plan changes is a plan-stage ruling.
