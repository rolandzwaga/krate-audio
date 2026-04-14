# Phase 1 Data Model -- Membrum Phase 6 UI

**Spec**: `specs/141-membrum-phase6-ui/spec.md`
**Plan**: `specs/141-membrum-phase6-ui/plan.md`
**Date**: 2026-04-12

This document specifies every new data structure introduced by Phase 6 and the v5 -> v6 state migration.

---

## 1. Parameter ID Allocations

### Globals (Range 280-299, reserved in Phase 5)

```cpp
enum Phase6GlobalIds : Steinberg::Vst::ParamID
{
    kUiModeId     = 280,  // StringListParameter: { "Acoustic", "Extended" }, default "Acoustic"
                          // Session-scoped; NOT written to IBStream; kit preset MAY override
    kEditorSizeId = 281,  // StringListParameter: { "Default", "Compact" },   default "Default"
                          // Session-scoped; NOT written to IBStream; NOT in presets
    // 282-299 reserved for future Phase 6+ globals
};

constexpr int kPhase6GlobalCount = 2;

static_assert(kCouplingDelayId < kUiModeId,
              "Phase 5 and Phase 6 global ranges must not overlap");
static_assert(kUiModeId + kPhase6GlobalCount <= kPadBaseId,
              "Phase 6 globals must not overlap per-pad range");
```

### Per-pad macro offsets (offsets 37-41 inside the reserved 36-63 range)

```cpp
enum PadParamOffset : int
{
    // ... Phase 4 offsets 0-35 ...
    kPadCouplingAmount       = 36,  // Phase 5
    kPadMacroTightness       = 37,  // Phase 6
    kPadMacroBrightness      = 38,  // Phase 6
    kPadMacroBodySize        = 39,  // Phase 6
    kPadMacroPunch           = 40,  // Phase 6
    kPadMacroComplexity      = 41,  // Phase 6

    kPadActiveParamCountV6   = 42,  // offsets 0-41 are active in Phase 6
    // Offsets 42-63 reserved for Phase 7+
};
```

Per-pad parameter ID formula (unchanged): `padParamId(pad, offset) = kPadBaseId + pad * kPadParamStride + offset`.

Per-pad macro count: 32 pads x 5 macros = **160 new per-pad parameters**. Total Phase 6 new params: 2 globals + 160 per-pad = **162**.

---

## 2. PadConfig v6 Layout

```cpp
struct PadConfig
{
    // --- Phase 4 fields (unchanged) ---
    ExciterType   exciterType = ExciterType::Impulse;
    BodyModelType bodyModel   = BodyModelType::Membrane;
    float material, size, decay, strikePosition, level;
    float tsFilterType, tsFilterCutoff, tsFilterResonance, tsFilterEnvAmount,
          tsDriveAmount, tsFoldAmount,
          tsPitchEnvStart, tsPitchEnvEnd, tsPitchEnvTime, tsPitchEnvCurve,
          tsFilterEnvAttack, tsFilterEnvDecay, tsFilterEnvSustain, tsFilterEnvRelease;
    float modeStretch, decaySkew, modeInjectAmount, nonlinearCoupling;
    float morphEnabled, morphStart, morphEnd, morphDuration, morphCurve;
    std::uint8_t chokeGroup = 0;
    std::uint8_t outputBus  = 0;
    float fmRatio, feedbackAmount, noiseBurstDuration, frictionPressure;

    // --- Phase 5 (unchanged) ---
    float couplingAmount = 0.5f;

    // --- Phase 6 NEW ---
    float macroTightness  = 0.5f;
    float macroBrightness = 0.5f;
    float macroBodySize   = 0.5f;
    float macroPunch      = 0.5f;
    float macroComplexity = 0.5f;
};
```

**Size impact**: +20 bytes per `PadConfig` (5 floats). Total per-pad array: 32 * ~200 bytes ~ 6.4 KiB. Cache-line behaviour unchanged.

**Default value rationale**: Macro = 0.5 is **neutral** (zero delta from registered default; see Clarification #1). All 160 macro fields default to 0.5 at construction and on v5 -> v6 migration.

---

## 3. MacroMapper (Processor-side, Audio-thread)

**Location**: `plugins/membrum/src/processor/macro_mapper.{h,cpp}`

**Namespace**: `Membrum`

### Class outline

```cpp
class MacroMapper
{
public:
    MacroMapper() noexcept = default;

    // Called once in Processor::initialize AFTER registered-default table is populated.
    // Caches the registered default for each target parameter for all 32 pads.
    void prepare(const RegisteredDefaultsTable& defaults) noexcept;

    // Called once per audio block inside processParameterChanges() on the audio thread.
    // For each pad whose macro values OR whose target-param registered defaults changed
    // since the last call, recomputes effective underlying param values and writes them
    // into `padConfig` via atomic field setters.
    //
    // No allocations, no locks, no I/O.
    void apply(int padIndex, PadConfig& padConfig) noexcept;

    // Called when a kit preset is loaded and all 32 pads need a forced refresh.
    void reapplyAll(std::array<PadConfig, kNumPads>& pads) noexcept;

    // Diagnostic: returns true if padIndex's cached macros differ from its live values.
    [[nodiscard]] bool isDirty(int padIndex) const noexcept;

private:
    // Cached registered defaults (filled by prepare()).
    // Keys: each target PadParamOffset used by any macro.
    struct RegisteredDefaults {
        float material;        // kPadMaterial default (0.5f)
        float decay;           // kPadDecay default (0.3f)
        float decaySkew;       // kPadDecaySkew default (0.5f)
        float modeInjectAmount;// kPadModeInjectAmount default (0.0f)
        float tsFilterCutoff;  // kPadTSFilterCutoff default (1.0f)
        float size;            // kPadSize default (0.5f)
        float modeStretch;     // kPadModeStretch default (0.333333f)
        float tsPitchEnvStart; // 0.0f
        float tsPitchEnvEnd;   // 0.0f
        float tsPitchEnvTime;  // 0.0f
        float couplingAmount;  // 0.5f
        float nonlinearCoupling; // 0.0f
        // ... exciter-specific attack/brightness defaults (per-exciter branch) ...
    };
    RegisteredDefaults defaults_{};

    // Per-pad "last applied macros" cache for early-out.
    struct PadCache {
        float tightness  = 0.5f;
        float brightness = 0.5f;
        float bodySize   = 0.5f;
        float punch      = 0.5f;
        float complexity = 0.5f;
    };
    std::array<PadCache, kNumPads> cache_{};

    // Private helpers (all noexcept, pure arithmetic):
    void applyTightness(const PadConfig& src, PadConfig& dst) noexcept;
    void applyBrightness(const PadConfig& src, PadConfig& dst) noexcept;
    void applyBodySize(const PadConfig& src, PadConfig& dst) noexcept;
    void applyPunch(const PadConfig& src, PadConfig& dst) noexcept;
    void applyComplexity(const PadConfig& src, PadConfig& dst) noexcept;
};
```

### Macro delta formulas (final `constexpr` values captured in `macro_mapper.cpp`)

All formulas: `effective = clamp(registeredDefault + delta(macro), 0.0f, 1.0f)`.

| Macro | Target | Delta curve (macro in [0,1]) |
|-------|--------|------------------------------|
| Tightness | `material` | Linear: `delta = (macro - 0.5f) * 0.3f` |
| Tightness | `decay` | Exponential: `delta = -(exp2f((macro - 0.5f) * 2.0f) - 1.0f) * 0.25f` (higher macro = shorter decay) |
| Tightness | `decaySkew` | Linear: `delta = (macro - 0.5f) * 1.0f` (full span on offset, since default is 0.5) |
| Brightness | `tsFilterCutoff` | Exp (log-Hz): `delta = (macro - 0.5f) * 0.4f` (0.4 norm units ~ 2 octaves @ default cutoff) |
| Brightness | `modeInjectAmount` | Linear: `delta = (macro - 0.5f) * 0.3f` |
| Brightness | per-exciter | See exciter-specific table below |
| Body Size | `size` | Linear: `delta = (macro - 0.5f) * 0.4f` |
| Body Size | `modeStretch` | Linear: `delta = (macro - 0.5f) * 0.2f` (+/-10% around default 1.0) |
| Body Size | `decay` | Linear: `delta = (macro - 0.5f) * 0.2f` (envelope scale) |
| Punch | `tsPitchEnvStart` | Exp: `delta = (macro - 0.5f) * 0.5f` (depth) |
| Punch | `tsPitchEnvTime` | Inverse-exp: `delta = -(macro - 0.5f) * 0.4f` (higher macro = faster env) |
| Punch | per-exciter attack | Linear: `delta = -(macro - 0.5f) * 0.3f` |
| Complexity | mode count (stepped) | Stepped: `delta = round((macro - 0.5f) * 48.0f)` partials (8..32 range; stored as separate `partialCount` field if added later -- in Phase 6, maps to `modeInjectAmount` as proxy) |
| Complexity | `couplingAmount` | Linear: `delta = (macro - 0.5f) * 0.5f` |
| Complexity | `nonlinearCoupling` | Linear: `delta = (macro - 0.5f) * 0.3f` |

### Per-exciter Brightness / Punch targets

| Active `exciterType` | Brightness target | Punch-attack target |
|----------------------|-------------------|---------------------|
| `Impulse` | `noiseBurstDuration` (inverted) | `noiseBurstDuration` |
| `FMImpulse` | `fmRatio` | `feedbackAmount` |
| `NoiseBurst` | `noiseBurstDuration` | `noiseBurstDuration` |
| `Feedback` | `feedbackAmount` | `feedbackAmount` |
| `Mallet` | `material` (as hardness proxy) | `noiseBurstDuration` |
| `Friction` | `frictionPressure` | `frictionPressure` |

(Exact mapping may be tuned during listening tests; final values captured as `constexpr` in `macro_mapper.cpp`.)

### Threading contract

- `prepare()` called on the component thread (during `Processor::initialize()`, before `setActive(true)`).
- `apply()` / `reapplyAll()` called ONLY on audio thread inside `Processor::processParameterChanges()`.
- `isDirty()` called from audio thread only.
- Zero allocations, zero locks, zero exceptions.

---

## 4. PadGlowPublisher (Lock-Free Amplitude Publisher)

**Location**: `plugins/membrum/src/dsp/pad_glow_publisher.h` (header-only)

**Namespace**: `Membrum`

```cpp
class PadGlowPublisher
{
public:
    static constexpr int kNumPads = 32;
    static constexpr int kAmplitudeBuckets = 32; // 5 bits per pad
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

    void reset() noexcept {
        for (auto& w : words_) w.store(0u, std::memory_order_relaxed);
    }

    // Audio-thread write. `amplitude` is the voice envelope [0..1].
    // Quantised to 5 bits and OR'd into the pad's word at a single-bit position.
    // Using a "currently-highest-bucket" encoding: word stores one-hot bit of the
    // current amplitude bucket. Audio thread overwrites unconditionally.
    void publish(int padIndex, float amplitude) noexcept {
        const int bucket = static_cast<int>(
            std::clamp(amplitude, 0.0f, 1.0f) * (kAmplitudeBuckets - 1) + 0.5f);
        const std::uint32_t word = (bucket > 0) ? (1u << bucket) : 0u;
        words_[static_cast<std::size_t>(padIndex)].store(word, std::memory_order_relaxed);
    }

    // UI-thread read at <= 30 Hz. Returns the current bucket (0..31) for each pad.
    void snapshot(std::array<std::uint8_t, kNumPads>& out) const noexcept {
        for (int i = 0; i < kNumPads; ++i) {
            const std::uint32_t w = words_[i].load(std::memory_order_acquire);
            // Find highest set bit (0 if none):
            out[i] = (w == 0) ? 0u : static_cast<std::uint8_t>(31 - std::countl_zero(w));
        }
    }

private:
    alignas(64) std::array<std::atomic<std::uint32_t>, kNumPads> words_{};
};
```

**Memory footprint**: 32 * 4 bytes = 128 bytes (single cache line aligned).
**Audio-thread cost**: 1 atomic store per active pad per block.
**UI-thread cost**: 32 atomic loads + 32 `countl_zero` per snapshot; called <= 30 Hz.
**Torn-read safety**: A torn 32-bit word is impossible on x86/x64/ARMv8 for `std::atomic<uint32_t>` -- `is_always_lock_free` asserted.

---

## 5. MatrixActivityPublisher (Tier 2 Coupling Activity)

**Location**: `plugins/membrum/src/dsp/matrix_activity_publisher.h` (header-only)

**Namespace**: `Membrum`

```cpp
class MatrixActivityPublisher
{
public:
    static constexpr int kNumPads = 32;
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

    void reset() noexcept {
        for (auto& w : activityMask_) w.store(0u, std::memory_order_relaxed);
    }

    // Audio-thread write: for source `src`, set a bit for each `dst` where
    // effective coupling energy exceeds the threshold (e.g., -60 dBFS).
    void publishSourceActivity(int src, std::uint32_t dstMask) noexcept {
        activityMask_[static_cast<std::size_t>(src)].store(
            dstMask, std::memory_order_relaxed);
    }

    // UI-thread read at <= 30 Hz.
    std::uint32_t readSourceActivity(int src) const noexcept {
        return activityMask_[static_cast<std::size_t>(src)].load(
            std::memory_order_acquire);
    }

    // Full snapshot for matrix view redraw.
    void snapshot(std::array<std::uint32_t, kNumPads>& out) const noexcept {
        for (int i = 0; i < kNumPads; ++i)
            out[i] = activityMask_[i].load(std::memory_order_acquire);
    }

private:
    alignas(64) std::array<std::atomic<std::uint32_t>, kNumPads> activityMask_{};
};
```

**Memory footprint**: 128 bytes.

---

## 6. MetersBlock (DataExchange Payload)

**Location**: `plugins/membrum/src/processor/meters_block.h`

**Namespace**: `Membrum`

```cpp
struct MetersBlock
{
    float        peakL;          // stereo peak [0..1] for main output, linear
    float        peakR;
    std::uint16_t activeVoices;  // 0..16
    std::uint16_t cpuPermille;   // 0..1000 (tenths of a percent)
    // Total: 12 bytes. Fixed-size; safe for DataExchange block.
};
```

Published once per block via `Steinberg::Vst::DataExchangeHandler::sendMainSynchronously()`.

Receiver: `Controller::onDataExchangeBlocksReceived()` decodes the last block and updates the Kit Column meters on the UI thread (per Innexus pattern).

---

## 7. UiModePolicy

**Location**: `plugins/membrum/src/ui/ui_mode.h`

```cpp
namespace Membrum::UI {

enum class UiMode : std::uint8_t { Acoustic = 0, Extended = 1 };

// Session-scoped with preset override.
// Defaults to Acoustic on instantiation AND on state load without explicit override.
constexpr UiMode kDefaultUiMode = UiMode::Acoustic;

// Helper: normalised 0.0 -> Acoustic, 1.0 -> Extended.
[[nodiscard]] constexpr UiMode uiModeFromNormalized(float n) noexcept {
    return (n < 0.5f) ? UiMode::Acoustic : UiMode::Extended;
}

[[nodiscard]] constexpr float uiModeToNormalized(UiMode m) noexcept {
    return (m == UiMode::Acoustic) ? 0.0f : 1.0f;
}

} // namespace Membrum::UI
```

**Persistence rules** (enforced in `Processor::getState`/`setState` and `Controller::getState`/`setState`):
- `kUiModeId` is NOT written to `IBStream`.
- On `setState`, the Controller calls `setParamNormalized(kUiModeId, uiModeToNormalized(kDefaultUiMode))` before consuming the state blob, so mode always resets to Acoustic.
- After `setState` completes, if the state blob was loaded from a kit preset file whose JSON contains `"uiMode": "Extended"`, the preset-load callback explicitly sets the mode. This is the ONLY path that can set it to Extended on load.

---

## 8. EditorSizePolicy

**Location**: `plugins/membrum/src/ui/editor_size_policy.h`

```cpp
namespace Membrum::UI {

enum class EditorSize : std::uint8_t { Default = 0, Compact = 1 };

constexpr EditorSize kDefaultEditorSize = EditorSize::Default;

constexpr int kDefaultWidth  = 1280;
constexpr int kDefaultHeight = 800;
constexpr int kCompactWidth  = 1024;
constexpr int kCompactHeight = 640;

constexpr const char* templateNameFor(EditorSize s) noexcept {
    return (s == EditorSize::Default) ? "EditorDefault" : "EditorCompact";
}

} // namespace Membrum::UI
```

**Persistence rules**:
- `kEditorSizeId` is NOT written to `IBStream` and is NOT encoded in any preset format.
- On every plugin instantiation AND every `setState`, resets to `Default`.
- User toggle in Kit Column drives `VST3Editor::exchangeView(templateNameFor(newSize))` on the UI thread via the `IDependent` pattern.

---

## 9. State Version 6 Migration (v5 -> v6)

**Binary layout (v6, appended to v5)**

```
[existing v5 blob: int32 version=5..couplingOverrides]
--- Phase 6 additions (appended when version == 6) ---
[160 x float64] per-pad macro values, pad-major order:
    pad0.tightness, pad0.brightness, pad0.bodySize, pad0.punch, pad0.complexity,
    pad1.tightness, ..., pad31.complexity
```

**Size impact**: 160 * 8 bytes = 1280 bytes appended.

**Migration rules**:
- `kUiModeId` and `kEditorSizeId` are NEVER written to `IBStream`. They are session-scoped and always reset to defaults on `setState`.
- v5 blob (`version == 5`): read v4/v5 body unchanged, then set all 160 macros to 0.5 (neutral).
- v6 blob (`version == 6`): read v5 body, then read 160 float64 macro values.
- v1..v4 blobs: delegate to existing migration chain, then apply v5 defaults (couplingAmount=0.5, coupling globals = 0), then apply v6 defaults (macros=0.5).
- Future versions (`version > 6`): return `kResultFalse` (unchanged behaviour).

**Round-trip guarantee** (FR-084): Save a v6 blob, load it, save again -- the byte sequence of the second save MUST equal the first (within float tolerance on the 160 macro values).

---

## 10. Kit Preset JSON Schema Extension

**Existing Phase 4 kit preset JSON** gains one optional field:

```json
{
  "format_version": 6,
  "name": "My Kit",
  "uiMode": "Acoustic",        // NEW in Phase 6; optional; values: "Acoustic" | "Extended"
  "pads": [
    {
      "padIndex": 0,
      "exciterType": "Impulse",
      "bodyModel": "Membrane",
      // ... Phase 4 fields ...
      "couplingAmount": 0.5,    // Phase 5
      "macros": {               // NEW in Phase 6; optional (defaults to 0.5 each)
        "tightness": 0.5,
        "brightness": 0.5,
        "bodySize": 0.5,
        "punch": 0.5,
        "complexity": 0.5
      }
    },
    // ... 31 more pads ...
  ],
  "couplingOverrides": [        // Phase 5
    // ...
  ]
}
```

**Rules**:
- `"format_version": 6` identifies Phase 6 kit presets. Loaders accept 4, 5, 6; defaults fill missing fields.
- `"uiMode"` is optional; absent -> `kUiModeId` is left at its current session value (no change).
- `"macros"` is optional per pad; absent -> all 0.5.
- Per-pad preset JSON gains the same `"macros"` block (optional).

---

## 11. Per-pad Preset v6 Payload

**Rule (FR-042 preserved, FR-083 extended)**: When loading a per-pad preset for pad `P`:
- Replace pad `P`'s sound parameters (exciter, body, material, size, decay, strikePosition, level, tone shaper, unnatural zone, morph, fmRatio/feedbackAmount/noiseBurstDuration/frictionPressure).
- Replace pad `P`'s **macro values** (if present in preset; else set to 0.5).
- **PRESERVE** pad `P`'s `outputBus`, `chokeGroup`, and `couplingAmount` at their pre-load values.

This preserves the Phase 4 FR-022 and Phase 5 FR-022 contracts.

---

## 12. Entity Relationship Summary

```
Controller (UI thread)                 Processor (audio thread)
-----------------------                ------------------------
  kUiModeId (param 280) --------+
  kEditorSizeId (param 281)     |
  kPadMacro*Id x 160  ----------+-> processParameterChanges()
                                |         |
                                |         v
                                |    MacroMapper::apply(pad, PadConfig&)
                                |         |
                                |         v
                                |    PadConfig fields (material, decay, ...)
                                |         |
                                |         v
                                |    DrumVoice reads PadConfig on noteOn
                                |
                                |    [audio-thread writes]
                                |    PadGlowPublisher (32 atomics)    --+
                                |    MatrixActivityPublisher (32 atomics)|
                                |    MetersBlock via DataExchangeHandler |
                                v                                       |
  IDependent::update() <-- setParamNormalized()                         |
  PadGridView reads PadGlowPublisher (30 Hz CVSTGUITimer) <-------------+
  CouplingMatrixView reads MatrixActivityPublisher (30 Hz) <------------+
  MetersView reads cached MetersBlock from DataExchange receiver <------+
```

**Thread-safety boundary**:
- Parameter changes (UI -> audio): VST3 `IParameterChanges` queue.
- Audio -> UI high-frequency (pad glow, matrix activity): lock-free atomic bitfields.
- Audio -> UI low-frequency (meters, CPU): `IDataExchangeHandler` queue.
- No other cross-thread communication.
