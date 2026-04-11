# Phase 1 Data Model: Membrum Phase 2

**Spec**: `specs/137-membrum-phase2-exciters-bodies/spec.md`
**Branch**: `137-membrum-phase2-exciters-bodies`

This document defines the shape of every new type added by Phase 2. It is **declarative**: struct members, member types, and invariants — no function bodies, no implementation.

All new types live in plugin-local namespaces `Membrum::` or `Membrum::Bodies::`. No `dsp/` changes.

---

## 1. Enums and tag types

### `Membrum::ExciterType`

```cpp
enum class ExciterType : int {
    Impulse     = 0,
    Mallet      = 1,
    NoiseBurst  = 2,
    Friction    = 3,
    FMImpulse   = 4,
    Feedback    = 5,
    kCount      = 6,
};
```

**Invariants:**
- Underlying integer is stable across plugin versions (state serialization).
- `kCount` is NOT a valid runtime value.

**Location:** `plugins/membrum/src/dsp/exciter_type.h` (new file).

### `Membrum::BodyModelType`

```cpp
enum class BodyModelType : int {
    Membrane   = 0,
    Plate      = 1,
    Shell      = 2,
    String     = 3,
    Bell       = 4,
    NoiseBody  = 5,
    kCount     = 6,
};
```

**Invariants:** Same as `ExciterType`.

**Location:** `plugins/membrum/src/dsp/body_model_type.h` (new file).

---

## 2. Exciter backends (variant alternatives)

All exciter backend classes are small plugin-local wrappers that own (or reference) a `Krate::DSP` core and translate the Membrum-common "velocity 0..1" contract into the backend's native trigger semantics. Each exposes the **same public API** so the variant dispatch works uniformly:

```cpp
// Conceptual API every exciter backend satisfies (not a base class;
// satisfied structurally by each variant alternative).
struct ExciterBackendAPI {
    void prepare(double sampleRate, uint32_t voiceId) noexcept;
    void reset() noexcept;
    void trigger(float velocity /* 0..1 */) noexcept;
    void release() noexcept;
    [[nodiscard]] float process(float bodyFeedback /* current body output, 0 if unused */) noexcept;
    [[nodiscard]] bool isActive() const noexcept;
};
```

**Pre-allocation contract:** All backends are default-constructed and `prepare()`-called in `DrumVoice::prepare()`. No heap touches on the audio thread.

### 2.1 `Membrum::ImpulseExciter`

```cpp
struct ImpulseExciter {
    Krate::DSP::ImpactExciter core_;
    // No extra state; trigger() uses Phase 1 default parameter mapping.
};
```

- Phase 1's carry-over. Velocity → hardness (0.3..0.8), mass (0.3), brightness (0.15..0.4), position 0, f0 0.

### 2.2 `Membrum::MalletExciter`

```cpp
struct MalletExciter {
    Krate::DSP::ImpactExciter core_;
    // Mallet character: velocity maps to different hardness/alpha range.
};
```

- Spec FR-011: velocity drives mallet hardness exponent α ∈ [1.5, 4.0], contact duration 8→1 ms, SVF brightness rising with velocity.

### 2.3 `Membrum::NoiseBurstExciter`

```cpp
struct NoiseBurstExciter {
    Krate::DSP::NoiseOscillator noise_;
    Krate::DSP::SVF filter_;
    float envelopeState_ = 0.0f;
    float envelopeCoeff_ = 0.0f;   // computed from burst duration on trigger
    float burstSamplesRemaining_ = 0.0f;
    bool active_ = false;
};
```

- Uses `NoiseOscillator` (Layer 1), NOT `NoiseGenerator` (block-oriented, wrong shape).
- Envelope: linear attack (< 1 ms) + exponential decay (tau = `durationMs * kTauFactor`).
- Filter: `SVFMode::Lowpass` or `Bandpass` depending on velocity → cutoff mapping (200 Hz → 5000+ Hz per FR-012).

### 2.4 `Membrum::FrictionExciter`

```cpp
struct FrictionExciter {
    Krate::DSP::BowExciter core_;
    Krate::DSP::ADSREnvelope bowEnvelope_;  // short transient envelope (≤ 50 ms)
    bool active_ = false;
};
```

- Wraps `BowExciter` in transient mode only (FR-013): trigger ramps bow pressure up quickly then releases within 50 ms. No sustained bowing.

### 2.5 `Membrum::FMImpulseExciter`

```cpp
struct FMImpulseExciter {
    Krate::DSP::FMOperator carrier_;
    Krate::DSP::FMOperator modulator_;   // or use carrier's self-feedback
    Krate::DSP::MultiStageEnvelope ampEnv_;
    Krate::DSP::MultiStageEnvelope modIndexEnv_;
    float carrierRatio_ = 1.0f;
    float modulatorRatio_ = 1.4f;  // Chowning bell-like default (FR-014)
    bool active_ = false;
};
```

- Two `FMOperator` instances for a carrier + modulator topology. The modulator's output phase-modulates the carrier via `FMOperator::setPhaseModulationInput` (or similar, per header).
- `ampEnv_` gates carrier amplitude (≤ 100 ms).
- `modIndexEnv_` decays the modulation index faster than `ampEnv_` — spec 135's "modulation index must decay faster than the carrier amplitude."

### 2.6 `Membrum::FeedbackExciter`

```cpp
struct FeedbackExciter {
    Krate::DSP::SVF filter_;
    Krate::DSP::TanhADAA saturator_;
    Krate::DSP::DCBlocker dcBlocker_;
    Krate::DSP::EnvelopeFollower energyFollower_;
    float feedbackAmount_ = 0.0f;   // velocity-scaled
    float previousBodyOutput_ = 0.0f;
    float energyThreshold_ = 0.7f;  // tuned for 0 dBFS peak guarantee
    bool active_ = false;
};
```

- **Custom per-voice feedback path**; does NOT use `Krate::DSP::FeedbackNetwork` (which is a block-oriented delay-feedback system — wrong shape).
- `process(bodyFeedback)` computes: `energyGain = 1.0 − clamp(energyFollower_.processSample(bodyFeedback) − energyThreshold_, 0, 1)`; then `feedbackSignal = tanhADAA( filter_.process(bodyFeedback * feedbackAmount_ * energyGain) )`; then `dcBlocker_.process(feedbackSignal)`.
- Guarantees SC-008 (peak ≤ 0 dBFS at any feedback amount).

### 2.7 `Membrum::ExciterBank`

```cpp
class ExciterBank {
public:
    // Public API
    void prepare(double sampleRate, uint32_t voiceId) noexcept;
    void reset() noexcept;
    void setExciterType(ExciterType type) noexcept;   // deferred to next trigger
    void trigger(float velocity) noexcept;
    void release() noexcept;
    [[nodiscard]] float process(float bodyFeedback) noexcept;
    [[nodiscard]] bool isActive() const noexcept;

private:
    using Variant = std::variant<
        ImpulseExciter,
        MalletExciter,
        NoiseBurstExciter,
        FrictionExciter,
        FMImpulseExciter,
        FeedbackExciter
    >;

    Variant active_;
    ExciterType currentType_ = ExciterType::Impulse;
    ExciterType pendingType_ = ExciterType::Impulse;
};
```

**Invariants:**
- `active_` always holds a valid alternative (never valueless).
- `setExciterType` only updates `pendingType_`; the swap happens in `trigger()` if `pendingType_ != currentType_`.
- `process()` dispatches via `std::visit` (or `switch` on `active_.index()` — see research §1).

**Location:** `plugins/membrum/src/dsp/exciter_bank.h`.

---

## 3. Body backends (variant alternatives)

Each body backend is thin — most modal bodies are configuration wrappers around the **single shared** `ModalResonatorBank`. Only String and Noise Body own additional resources.

Shared contract:

```cpp
struct BodyBackendAPI {
    // Note: configure() and reset() take the shared bank by reference
    // because modal bodies do not own the bank.
    void prepare(double sampleRate, uint32_t voiceId) noexcept;
    void configureForNoteOn(
        Krate::DSP::ModalResonatorBank& sharedBank,
        const VoiceCommonParams& params,
        float pitchEnvelopeStartHz
    ) noexcept;
    [[nodiscard]] float processSample(
        Krate::DSP::ModalResonatorBank& sharedBank,
        float excitation
    ) noexcept;
    void reset(Krate::DSP::ModalResonatorBank& sharedBank) noexcept;
};
```

Where `VoiceCommonParams` is the 5 Phase-1 parameters + body-specific parameters.

### 3.1 `Membrum::VoiceCommonParams`

```cpp
struct VoiceCommonParams {
    float material;      // [0, 1]
    float size;          // [0, 1]
    float decay;         // [0, 1]
    float strikePos;     // [0, 1]
    float level;         // [0, 1]

    // Phase 2 additions — passed to body mappers:
    float modeStretch;      // [0.5, 2.0]   default 1.0
    float decaySkew;        // [-1.0, 1.0]  default 0.0
};
```

**Location:** `plugins/membrum/src/dsp/voice_common_params.h`.

### 3.2 `Membrum::MembraneBody`

```cpp
struct MembraneBody {
    // No state — purely stateless; uses shared bank.
    // configureForNoteOn() calls MembraneMapper::mapToBank(sharedBank, params).
};
```

### 3.3 `Membrum::PlateBody`

```cpp
struct PlateBody {
    // Stateless wrapper. Uses Bodies::PlateMapper.
};
```

### 3.4 `Membrum::ShellBody`

```cpp
struct ShellBody {
    // Stateless wrapper. Uses Bodies::ShellMapper.
};
```

### 3.5 `Membrum::BellBody`

```cpp
struct BellBody {
    // Stateless wrapper. Uses Bodies::BellMapper.
};
```

### 3.6 `Membrum::StringBody`

```cpp
struct StringBody {
    Krate::DSP::WaveguideString string_;   // primary
    // Alternative: Krate::DSP::KarplusStrong (fallback, not used by default)
    // processSample() IGNORES sharedBank and calls string_.process(excitation) directly.
};
```

- **Owns its own resonator.** FR-023: first body model to break out of the shared ModalResonatorBank.
- Strike Position → `string_.setPickPosition()`. Size → `string_.setFrequency()`.

### 3.7 `Membrum::NoiseBody`

```cpp
struct NoiseBody {
    Krate::DSP::NoiseOscillator noise_;
    Krate::DSP::SVF noiseFilter_;       // bandpass, time-varying cutoff
    Krate::DSP::ADSREnvelope noiseEnvelope_;
    float modalMix_ = 0.6f;             // modal layer gain
    float noiseMix_ = 0.4f;             // noise layer gain
    static constexpr int kModeCount = 40;  // FR-062: start at 40, reduce if CPU blown
    // Modal layer uses sharedBank (configured with plate_modes at kModeCount entries).
};
```

- Two-layer hybrid. `processSample()` mixes `modalMix_ * sharedBank.processSample(excitation) + noiseMix_ * noiseEnvelope_.process() * noiseFilter_.process(noise_.process())`.

### 3.8 `Membrum::BodyBank`

```cpp
class BodyBank {
public:
    void prepare(double sampleRate, uint32_t voiceId) noexcept;
    void reset() noexcept;
    void setBodyModel(BodyModelType type) noexcept;  // deferred to next trigger
    void configureForNoteOn(const VoiceCommonParams& params, float pitchEnvStartHz) noexcept;
    [[nodiscard]] float processSample(float excitation) noexcept;

    // Exposed to FeedbackExciter:
    [[nodiscard]] float getLastOutput() const noexcept { return lastOutput_; }

private:
    using Variant = std::variant<
        MembraneBody,
        PlateBody,
        ShellBody,
        StringBody,
        BellBody,
        NoiseBody
    >;

    Variant active_;
    Krate::DSP::ModalResonatorBank sharedBank_;  // shared by all modal bodies
    BodyModelType currentType_ = BodyModelType::Membrane;
    BodyModelType pendingType_ = BodyModelType::Membrane;
    float lastOutput_ = 0.0f;
};
```

**Invariants:**
- `sharedBank_` is pre-allocated, `prepare`d once. Its modes are rewritten via `setModes()` at each note-on if the active body has changed.
- Switching body while a voice is sounding updates `pendingType_` only; the actual swap happens at `configureForNoteOn()`.
- `lastOutput_` is stored after every `processSample()` so the Feedback exciter can read the previous sample without re-calling the body.
- The public `processSample(float excitation)` wraps the active body's `processSample(sharedBank_, excitation)` call internally. Callers use the single-argument form; the bank reference is an implementation detail.

**Location:** `plugins/membrum/src/dsp/body_bank.h`.

---

## 4. Per-body mapping helpers

Each body model has a dedicated helper in `Membrum::Bodies::` namespace that converts `VoiceCommonParams` into the arguments for `ModalResonatorBank::setModes()` (or, for String, into `WaveguideString` setter calls).

**Contract** (structural; not a virtual base class):

```cpp
namespace Membrum::Bodies {

struct MapperResult {
    float frequencies[Krate::DSP::ModalResonatorBank::kMaxModes];
    float amplitudes[Krate::DSP::ModalResonatorBank::kMaxModes];
    int   numPartials;
    float decayTime;
    float brightness;
    float stretch;
    float scatter;
};

// Each mapper exposes this static function (no state, no virtual).
struct MembraneMapper {
    static MapperResult map(const VoiceCommonParams& params, float pitchHz) noexcept;
};
struct PlateMapper     { static MapperResult map(const VoiceCommonParams&, float pitchHz) noexcept; };
struct ShellMapper     { static MapperResult map(const VoiceCommonParams&, float pitchHz) noexcept; };
struct BellMapper      { static MapperResult map(const VoiceCommonParams&, float pitchHz) noexcept; };
// StringMapper and NoiseBodyMapper are different because they don't feed ModalResonatorBank.
struct StringMapperResult {
    float frequencyHz;
    float decayTime;
    float brightness;
    float pickPosition;
};
struct StringMapper    { static StringMapperResult map(const VoiceCommonParams&, float pitchHz) noexcept; };
struct NoiseBodyMapper {
    // NoiseBody gets BOTH a modal MapperResult (for the sharedBank)
    // AND noise-layer parameters.
    struct Result {
        MapperResult modal;
        float noiseFilterCutoffHz;
        float noiseFilterResonance;
        float noiseAttackMs;
        float noiseDecayMs;
        float noiseMix;
        float modalMix;
    };
    static Result map(const VoiceCommonParams&, float pitchHz) noexcept;
};

} // namespace Membrum::Bodies
```

**Invariants:**
- All mappers are **stateless pure functions** — no per-instance data, no RNG in Phase 2 (phase randomization lives in Mode Inject, not the mappers).
- `MembraneMapper::map` produces bit-identical output to Phase 1's inline code when given the same `(material, size, decay, strikePos)` (FR-031).
- `MapperResult.numPartials ≤ kMaxModes` (hard cap at 96; practical caps are per-body constants: Membrane 16, Plate 16, Shell 12, Bell 16, NoiseBody 40).

**Per-body Size-to-fundamental formulas** (from spec 137 FR-032):

| Body | Formula |
|------|---------|
| Membrane | `f0 = 500 * pow(0.1, size)` — Phase 1 |
| Plate    | `f0 = 800 * pow(0.1, size)` |
| Shell    | `f0 = 1500 * pow(0.1, size)` |
| Bell     | `f0 = 800 * pow(0.1, size)` (nominal partial) |
| String   | `f0 = 800 * pow(0.1, size)` |
| NoiseBody| `f0 = 1500 * pow(0.1, size)` |

**Location:** `plugins/membrum/src/dsp/bodies/mapper.h` + per-body implementation in `plate_mapper.h`, `shell_mapper.h`, etc.

---

## 5. Mode-ratio tables

New headers added alongside the existing `membrane_modes.h`:

### `plugins/membrum/src/dsp/bodies/plate_modes.h`

```cpp
namespace Membrum::Bodies {
// Square Kirchhoff plate (m² + n²) normalized to fundamental.
// Phase 2 default: 16 modes; Noise Body extends to 40.
inline constexpr int kPlateModeCount = 16;
inline constexpr int kPlateMaxModeCount = 40;   // for Noise Body
// Indices 8–9 are degenerate (ratio 13.0 twice): (3,2) and (2,3) modes of a square plate are physically equal — not a typo.
inline constexpr float kPlateRatios[kPlateMaxModeCount] = {
    1.000f, 2.500f, 4.000f, 5.000f, 6.500f, 8.500f, 9.000f, 10.000f,
    13.000f, 13.000f, 16.250f, 17.000f, 18.500f, 20.000f, 22.500f, 25.000f,
    // ... remaining 24 entries computed from (m²+n²) sorted
};

// Rectangular-plate strike-position amplitude: sin(m*pi*x/a) * sin(n*pi*y/b)
struct PlateModeIndices { int m; int n; };
inline constexpr PlateModeIndices kPlateIndices[kPlateMaxModeCount] = { /* ... */ };

// Strike-position helper
float computePlateAmplitude(int modeIdx, float strikePos) noexcept;
} // namespace
```

### `plugins/membrum/src/dsp/bodies/shell_modes.h`

```cpp
namespace Membrum::Bodies {
// Free-free Euler-Bernoulli beam (untuned).
inline constexpr int kShellModeCount = 12;
inline constexpr float kShellRatios[kShellModeCount] = {
    1.000f, 2.757f, 5.404f, 8.933f, 13.344f, 18.637f,
    24.812f, 31.870f, 39.810f, 48.632f, 58.336f, 68.922f
};
float computeShellAmplitude(int modeIdx, float strikePos) noexcept;  // sin(k*pi*x/L)
}
```

### `plugins/membrum/src/dsp/bodies/bell_modes.h`

```cpp
namespace Membrum::Bodies {
// Chladni church-bell partial ratios.
inline constexpr int kBellModeCount = 16;
inline constexpr float kBellRatios[kBellModeCount] = {
    0.250f, 0.500f, 0.600f, 0.750f, 1.000f, 1.500f, 2.000f, 2.600f,
    3.200f, 4.000f, 5.333f, 6.400f, 7.333f, 8.667f, 10.000f, 12.000f
};
// Normalization to "nominal" = index 4 at ratio 1.0: no normalization needed.
float computeBellAmplitude(int modeIdx, float strikePos) noexcept;
}
```

---

## 6. Tone Shaper

### `Membrum::ToneShaper`

```cpp
class ToneShaper {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // Per-stage parameter setters (called by DrumVoice from controller parameters)
    void setFilterMode(Krate::DSP::SVFMode mode) noexcept;
    void setFilterCutoff(float hz) noexcept;
    void setFilterResonance(float q) noexcept;
    void setFilterEnvAmount(float amount) noexcept;      // [-1, 1]
    void setFilterEnvAttackMs(float ms) noexcept;
    void setFilterEnvDecayMs(float ms) noexcept;
    void setFilterEnvSustain(float level) noexcept;
    void setFilterEnvReleaseMs(float ms) noexcept;

    void setDriveAmount(float amount) noexcept;           // 0 = bypass
    void setFoldAmount(float amount) noexcept;            // 0 = bypass

    // Pitch envelope — control plane, not audio-rate
    void setPitchEnvStartHz(float hz) noexcept;           // 20..2000
    void setPitchEnvEndHz(float hz) noexcept;             // 20..2000
    void setPitchEnvTimeMs(float ms) noexcept;            // 0 = disabled
    void setPitchEnvCurve(Krate::DSP::EnvCurve curve) noexcept;

    // Lifecycle
    void noteOn(float velocity) noexcept;
    void noteOff() noexcept;

    // Control-plane query (called by DrumVoice before configuring bodies)
    [[nodiscard]] float processPitchEnvelope() noexcept;  // returns current Hz

    // Audio-rate process (body_output → filtered/driven/folded output)
    [[nodiscard]] float processSample(float bodyOutput) noexcept;

    [[nodiscard]] bool isBypassed() const noexcept;       // true if ALL stages at bypass

private:
    // SVF Filter + its own ADSR
    Krate::DSP::SVF filter_;
    Krate::DSP::ADSREnvelope filterEnv_;
    float filterBaseCutoffHz_ = 20000.0f;
    float filterEnvAmount_ = 0.0f;

    // Drive
    Krate::DSP::Waveshaper drive_;
    float driveAmount_ = 0.0f;

    // Wavefolder
    Krate::DSP::Wavefolder wavefolder_;
    float foldAmount_ = 0.0f;

    // DC Blocker (between wavefolder and filter)
    Krate::DSP::DCBlocker dcBlocker_;

    // Pitch Envelope (control plane)
    Krate::DSP::MultiStageEnvelope pitchEnv_;
    float pitchEnvStartHz_ = 160.0f;
    float pitchEnvEndHz_ = 50.0f;
    float pitchEnvTimeMs_ = 0.0f;       // 0 = disabled
    bool pitchEnvEnabled_ = false;

    bool prepared_ = false;
};
```

**Invariants:**
- When all stages are at their bypass values (drive=0, fold=0, filter cutoff=20 kHz, filterEnvAmount=0, pitchEnvTimeMs=0), `processSample` output differs from input by ≤ −120 dBFS (FR-045).
- `processPitchEnvelope()` must be called **before** `BodyBank::configureForNoteOn()` each sample (or block) so the body frequency reflects the current envelope.
- All internal DSP is pre-allocated in `prepare()`. No allocations in any other path.

**Location:** `plugins/membrum/src/dsp/tone_shaper.h`.

---

## 7. Unnatural Zone modules

### `Membrum::MaterialMorph`

```cpp
class MaterialMorph {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    void setEnabled(bool on) noexcept;
    void setStart(float material01) noexcept;   // [0, 1]
    void setEnd(float material01) noexcept;     // [0, 1]
    void setDurationMs(float ms) noexcept;      // [10, 2000]
    void setCurve(bool exponential) noexcept;

    void trigger() noexcept;
    [[nodiscard]] float process() noexcept;     // returns current morph value [0, 1]
    [[nodiscard]] bool isActive() const noexcept;

private:
    bool enabled_ = false;
    float startMaterial_ = 1.0f;
    float endMaterial_ = 0.0f;
    float durationMs_ = 200.0f;
    bool exponential_ = false;
    // runtime state
    float currentValue_ = 0.0f;
    float sampleCounter_ = 0.0f;
    float totalSamples_ = 0.0f;
    double sampleRate_ = 0.0;
};
```

**Invariants:**
- When `enabled_` is `false`, `process()` returns a constant (equal to the DrumVoice's current Material param) — no morph.
- When `totalSamples_ == 0`, `process()` also returns constant (prevents divide-by-zero).
- Morph completes naturally at `sampleCounter_ >= totalSamples_`, then holds the `endMaterial_` value.

**Location:** `plugins/membrum/src/dsp/unnatural/material_morph.h`.

### `Membrum::ModeInject`

```cpp
class ModeInject {
public:
    void prepare(double sampleRate, uint32_t voiceId) noexcept;
    void reset() noexcept;

    void setAmount(float amount) noexcept;   // 0 = silent (bypass)
    void setFundamentalHz(float hz) noexcept;

    void trigger() noexcept;                 // randomizes injected partial phases
    [[nodiscard]] float process() noexcept;  // returns injected partial sum

private:
    Krate::DSP::HarmonicOscillatorBank bank_;
    Krate::DSP::XorShift32 rng_;
    float amount_ = 0.0f;
    float fundamentalHz_ = 440.0f;
    float phaseOffsets_[8] = { 0.0f };       // 8 integer partials (harmonic preset)
    bool active_ = false;
};
```

**Invariants:**
- When `amount_ == 0.0f`, `process()` returns `0.0f` **without** running the oscillator bank (early-out). Zero leak (FR-052).
- `trigger()` randomizes `phaseOffsets_` via `rng_.next()` and applies to `bank_`. Same seed + same voiceId → same phases (reproducible).

**Location:** `plugins/membrum/src/dsp/unnatural/mode_inject.h`.

### `Membrum::NonlinearCoupling`

```cpp
class NonlinearCoupling {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    void setAmount(float amount) noexcept;   // 0 = bypass
    void setVelocity(float velocity) noexcept;

    // Applied to the body output in-place. Returns the coupled output.
    [[nodiscard]] float processSample(float bodyOutput) noexcept;

private:
    Krate::DSP::EnvelopeFollower envFollower_;
    Krate::DSP::TanhADAA energyLimiter_;
    float amount_ = 0.0f;
    float velocity_ = 0.0f;
    float previousEnv_ = 0.0f;
    bool prepared_ = false;
};
```

**Invariants:**
- When `amount_ == 0.0f`, `processSample(x) == x` (exact bypass, no energy limiter active).
- When `amount_ > 0.0f`, the energy limiter guarantees `|output| ≤ 1.0` (SC-008).

**Location:** `plugins/membrum/src/dsp/unnatural/nonlinear_coupling.h`.

### `Membrum::UnnaturalZone`

```cpp
class UnnaturalZone {
public:
    void prepare(double sampleRate, uint32_t voiceId) noexcept;
    void reset() noexcept;

    // Mode Stretch + Decay Skew flow through to body mapping helpers,
    // not to an audio-rate process function. They're stored here so
    // DrumVoice can query them when configuring the body.
    void setModeStretch(float value) noexcept;    // [0.5, 2.0]
    void setDecaySkew(float value) noexcept;      // [-1, 1]
    [[nodiscard]] float getModeStretch() const noexcept;
    [[nodiscard]] float getDecaySkew() const noexcept;

    // Audio-rate components owned here:
    MaterialMorph materialMorph;
    ModeInject    modeInject;
    NonlinearCoupling nonlinearCoupling;

private:
    float modeStretch_ = 1.0f;
    float decaySkew_ = 0.0f;
};
```

**Invariants:**
- Defaults (1.0, 0.0, all sub-modules disabled) produce identical output to a "Phase 2 with Unnatural Zone off" patch, within −120 dBFS (FR-055).

**Location:** `plugins/membrum/src/dsp/unnatural/unnatural_zone.h`.

---

## 8. DrumVoice (refactored)

```cpp
class DrumVoice {
public:
    DrumVoice() = default;

    void prepare(double sampleRate, uint32_t voiceId) noexcept;

    // Phase 1 parameter setters (unchanged API)
    void setMaterial(float v) noexcept;
    void setSize(float v) noexcept;
    void setDecay(float v) noexcept;
    void setStrikePosition(float v) noexcept;
    void setLevel(float v) noexcept;

    // Phase 2 new setters
    void setExciterType(ExciterType type) noexcept;       // deferred to next noteOn
    void setBodyModel(BodyModelType type) noexcept;       // deferred to next noteOn

    // Tone Shaper passthrough
    ToneShaper& toneShaper() noexcept { return toneShaper_; }

    // Unnatural Zone passthrough
    UnnaturalZone& unnaturalZone() noexcept { return unnaturalZone_; }

    // Lifecycle
    void noteOn(float velocity) noexcept;
    void noteOff() noexcept;
    [[nodiscard]] float process() noexcept;
    [[nodiscard]] bool isActive() const noexcept;

private:
    // Parameter state
    VoiceCommonParams params_{};
    float level_ = 0.8f;

    // Sub-components
    ExciterBank exciterBank_;
    BodyBank bodyBank_;
    ToneShaper toneShaper_;
    UnnaturalZone unnaturalZone_;
    Krate::DSP::ADSREnvelope ampEnvelope_;

    // Voice identity
    uint32_t voiceId_ = 0;
    double sampleRate_ = 0.0;

    // State
    bool active_ = false;
};
```

**Public-API compatibility:** `prepare`, `noteOn(velocity)`, `noteOff()`, `process()`, `isActive()`, and the 5 Phase-1 parameter setters preserve their Phase-1 signatures **exactly** so that Phase 1 tests continue to compile (FR-007). The `prepare()` signature gains a `voiceId` argument — Phase 1's tests pass 0 and this default is kept via overload or default parameter.

**Location:** `plugins/membrum/src/dsp/drum_voice.h` (modified; the Phase 1 file is refactored in place).

---

## 9. Parameter IDs

Extend `plugins/membrum/src/plugin_ids.h`:

```cpp
enum ParameterIds : Steinberg::Vst::ParamID
{
    // Phase 1 (unchanged)
    kMaterialId       = 100,
    kSizeId           = 101,
    kDecayId          = 102,
    kStrikePositionId = 103,
    kLevelId          = 104,

    // ====== Phase 2 ======

    // 200-209: Exciter + Body selectors and secondary exciter params
    kExciterTypeId           = 200,   // StringListParameter, 6 choices
    kBodyModelId             = 201,   // StringListParameter, 6 choices
    kExciterFMRatioId        = 202,   // FM Impulse carrier:mod ratio (1.0..4.0, default 1.4)
    kExciterFeedbackAmountId = 203,   // Feedback exciter drive amount (0..1)
    kExciterNoiseBurstDurationId = 204, // Noise Burst duration ms (2..15, default 5)
    kExciterFrictionPressureId = 205, // Friction bow pressure (0..1, default 0.3)

    // 210-229: Tone Shaper
    kToneShaperFilterTypeId       = 210,  // StringListParameter: LP/HP/BP
    kToneShaperFilterCutoffId     = 211,  // 20..20000 Hz, default 20000
    kToneShaperFilterResonanceId  = 212,  // 0..1, default 0
    kToneShaperFilterEnvAmountId  = 213,  // -1..1, default 0
    kToneShaperDriveAmountId      = 214,  // 0..1, default 0
    kToneShaperFoldAmountId       = 215,  // 0..1, default 0
    kToneShaperPitchEnvStartId    = 216,  // 20..2000 Hz, default 160
    kToneShaperPitchEnvEndId      = 217,  // 20..2000 Hz, default 50
    kToneShaperPitchEnvTimeId     = 218,  // 0..500 ms, default 0 (disabled)
    kToneShaperPitchEnvCurveId    = 219,  // StringListParameter: Exp/Lin

    // 220-229: Filter envelope sub-parameters
    kToneShaperFilterEnvAttackId  = 220,
    kToneShaperFilterEnvDecayId   = 221,
    kToneShaperFilterEnvSustainId = 222,
    kToneShaperFilterEnvReleaseId = 223,

    // 230-249: Unnatural Zone
    kUnnaturalModeStretchId       = 230,  // 0.5..2.0, default 1.0
    kUnnaturalDecaySkewId         = 231,  // -1..1, default 0
    kUnnaturalModeInjectAmountId  = 232,  // 0..1, default 0
    kUnnaturalNonlinearCouplingId = 233,  // 0..1, default 0

    // 240-249: Material Morph
    kMorphEnabledId       = 240,
    kMorphStartId         = 241,   // 0..1, default 1 (current Material)
    kMorphEndId           = 242,   // 0..1, default 0
    kMorphDurationMsId    = 243,   // 10..2000 ms, default 200
    kMorphCurveId         = 244,   // StringListParameter: Lin/Exp
};

// State version bumped
constexpr Steinberg::int32 kCurrentStateVersion = 2;  // was 1 in Phase 1
```

**Invariants:**
- Integer values of Phase 1 IDs (100–104) are **frozen**. Phase 2 adds only higher IDs. Existing state files continue to deserialize.
- `kCurrentStateVersion = 2` — the loader reads version first, then Phase-1 parameters, then if version ≥ 2 reads Phase-2 parameters; if version == 1 fills Phase-2 with defaults (FR-082).

**Location:** `plugins/membrum/src/plugin_ids.h` (modified).

---

## 10. State format

```
[4 bytes]  int32   version                  (Phase 1 = 1, Phase 2 = 2)
[5 * 8]    float64 phase1 params            (Material, Size, Decay, StrikePos, Level)
# ==== if version >= 2: ====
[2 * 4]    int32   exciterType, bodyModel   (stored as int, not float, for type safety)
[N * 8]    float64 phase2 params            (in declaration order per plugin_ids.h)
```

**Invariants:**
- Byte order: little-endian (VST3 state streams are host-typed; Membrum uses `IBStreamer::writeInt32/writeDouble` per Phase 1 pattern).
- Parameter count N is computed at compile time from the highest Phase-2 parameter ID.
- Loader gracefully rejects corrupt/truncated streams (returns `kResultFalse`).

---

## 11. New files summary

### New headers

```
plugins/membrum/src/dsp/
├── drum_voice.h                     (MODIFIED — refactored)
├── exciter_type.h                   (NEW)
├── body_model_type.h                (NEW)
├── voice_common_params.h            (NEW)
├── exciter_bank.h                   (NEW)
├── body_bank.h                      (NEW)
├── tone_shaper.h                    (NEW)
├── exciters/
│   ├── impulse_exciter.h            (NEW)
│   ├── mallet_exciter.h             (NEW)
│   ├── noise_burst_exciter.h        (NEW)
│   ├── friction_exciter.h           (NEW)
│   ├── fm_impulse_exciter.h         (NEW)
│   └── feedback_exciter.h           (NEW)
├── bodies/
│   ├── membrane_body.h              (NEW — thin wrapper)
│   ├── plate_body.h                 (NEW)
│   ├── shell_body.h                 (NEW)
│   ├── string_body.h                (NEW)
│   ├── bell_body.h                  (NEW)
│   ├── noise_body.h                 (NEW)
│   ├── plate_modes.h                (NEW)
│   ├── shell_modes.h                (NEW)
│   ├── bell_modes.h                 (NEW)
│   ├── membrane_mapper.h            (NEW — extracts Phase 1 inline code)
│   ├── plate_mapper.h               (NEW)
│   ├── shell_mapper.h               (NEW)
│   ├── bell_mapper.h                (NEW)
│   ├── string_mapper.h              (NEW)
│   └── noise_body_mapper.h          (NEW)
└── unnatural/
    ├── unnatural_zone.h             (NEW)
    ├── material_morph.h             (NEW)
    ├── mode_inject.h                (NEW)
    └── nonlinear_coupling.h         (NEW)
```

### Modified files

- `plugins/membrum/src/plugin_ids.h` — new parameter IDs, state version bump.
- `plugins/membrum/src/dsp/drum_voice.h` — refactored to use ExciterBank + BodyBank + ToneShaper + UnnaturalZone.
- `plugins/membrum/src/dsp/membrane_modes.h` — unchanged (preserved for FR-031 regression).
- `plugins/membrum/src/processor/processor.{h,cpp}` — parameter handling for ~25 new parameters.
- `plugins/membrum/src/controller/controller.{h,cpp}` — parameter registration for ~25 new parameters.
- `plugins/membrum/CMakeLists.txt` — add new source files.
- `plugins/membrum/tests/CMakeLists.txt` — add new test files.
- `plugins/membrum/version.json` — bump minor version.

---

## 12. Class/struct name uniqueness audit (ODR prevention)

All new types are in the `Membrum` or `Membrum::Bodies` namespaces. Verified against existing codebase:

- No existing class named `ExciterBank`, `BodyBank`, `ToneShaper`, `UnnaturalZone`, `MaterialMorph`, `ModeInject`, `NonlinearCoupling`, `ImpulseExciter`, `MalletExciter`, `NoiseBurstExciter`, `FrictionExciter`, `FMImpulseExciter`, `FeedbackExciter`, `MembraneBody`, `PlateBody`, `ShellBody`, `StringBody`, `BellBody`, `NoiseBody` (different from `Krate::DSP::NoiseGenerator`), `VoiceCommonParams`, `PlateMapper`, `ShellMapper`, `BellMapper`, `StringMapper`, `NoiseBodyMapper`, `MembraneMapper` in `dsp/` or other `plugins/` subdirectories.
- `Krate::DSP::FeedbackNetwork` and `Krate::DSP::NoiseGenerator` are in a different namespace — no collision with `Membrum::FeedbackExciter` / `Membrum::NoiseBody`.
- ODR searches to run at implementation start are listed in `plan.md`.
