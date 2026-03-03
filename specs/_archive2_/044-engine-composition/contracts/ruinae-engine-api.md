# API Contract: RuinaeEngine

**Location**: `dsp/include/krate/dsp/systems/ruinae_engine.h`
**Namespace**: `Krate::DSP`
**Layer**: 3 (Systems)

## Class Declaration

```cpp
namespace Krate::DSP {

/// @brief Internal enum for global modulation destinations.
enum class RuinaeModDest : uint32_t {
    GlobalFilterCutoff     = 64,
    GlobalFilterResonance  = 65,
    MasterVolume           = 66,
    EffectMix              = 67,
    AllVoiceFilterCutoff   = 68,
    AllVoiceMorphPosition  = 69,
    AllVoiceTranceGateRate = 70
};

class RuinaeEngine {
public:
    // =========================================================================
    // Constants (FR-002)
    // =========================================================================
    static constexpr size_t kMaxPolyphony = 16;
    static constexpr float kMinMasterGain = 0.0f;
    static constexpr float kMaxMasterGain = 2.0f;

    // =========================================================================
    // Lifecycle (FR-003, FR-004)
    // =========================================================================
    RuinaeEngine() noexcept;
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;

    // =========================================================================
    // Note Dispatch (FR-005 through FR-009)
    // =========================================================================
    void noteOn(uint8_t note, uint8_t velocity) noexcept;
    void noteOff(uint8_t note) noexcept;

    // =========================================================================
    // Polyphony Configuration (FR-010)
    // =========================================================================
    void setPolyphony(size_t count) noexcept;

    // =========================================================================
    // Voice Mode (FR-011)
    // =========================================================================
    void setMode(VoiceMode mode) noexcept;

    // =========================================================================
    // Stereo Voice Mixing (FR-012, FR-013, FR-014)
    // =========================================================================
    void setStereoSpread(float spread) noexcept;
    void setStereoWidth(float width) noexcept;

    // =========================================================================
    // Global Filter (FR-015, FR-016, FR-017)
    // =========================================================================
    void setGlobalFilterEnabled(bool enabled) noexcept;
    void setGlobalFilterCutoff(float hz) noexcept;
    void setGlobalFilterResonance(float q) noexcept;
    void setGlobalFilterType(SVFMode mode) noexcept;

    // =========================================================================
    // Global Modulation (FR-018, FR-019, FR-020, FR-021)
    // =========================================================================
    void setGlobalModRoute(int slot, ModSource source,
                           RuinaeModDest dest, float amount) noexcept;
    void clearGlobalModRoute(int slot) noexcept;

    // =========================================================================
    // Global Modulation Source Config (FR-022)
    // =========================================================================
    void setGlobalLFO1Rate(float hz) noexcept;
    void setGlobalLFO1Waveform(Waveform shape) noexcept;
    void setGlobalLFO2Rate(float hz) noexcept;
    void setGlobalLFO2Waveform(Waveform shape) noexcept;
    void setChaosSpeed(float speed) noexcept;
    void setMacroValue(size_t index, float value) noexcept;

    // =========================================================================
    // Performance Controllers (FR-023, FR-024, FR-025)
    // =========================================================================
    void setPitchBend(float bipolar) noexcept;
    void setAftertouch(float value) noexcept;
    void setModWheel(float value) noexcept;

    // =========================================================================
    // Effects Chain (FR-026, FR-027, FR-028)
    // =========================================================================
    void setDelayType(RuinaeDelayType type) noexcept;
    void setDelayTime(float ms) noexcept;
    void setDelayFeedback(float amount) noexcept;
    void setDelayMix(float mix) noexcept;
    void setReverbParams(const ReverbParams& params) noexcept;
    void setFreezeEnabled(bool enabled) noexcept;
    void setFreeze(bool frozen) noexcept;
    void setFreezePitchSemitones(float semitones) noexcept;
    void setFreezeShimmerMix(float mix) noexcept;
    void setFreezeDecay(float decay) noexcept;
    [[nodiscard]] size_t getLatencySamples() const noexcept;

    // =========================================================================
    // Master Output (FR-029, FR-030, FR-031)
    // =========================================================================
    void setMasterGain(float gain) noexcept;
    void setSoftLimitEnabled(bool enabled) noexcept;

    // =========================================================================
    // Processing (FR-032 through FR-034)
    // =========================================================================
    void processBlock(float* left, float* right, size_t numSamples) noexcept;

    // =========================================================================
    // Voice Parameter Forwarding (FR-035)
    // =========================================================================
    // Oscillators
    void setOscAType(OscType type) noexcept;
    void setOscBType(OscType type) noexcept;
    void setOscAPhaseMode(PhaseMode mode) noexcept;
    void setOscBPhaseMode(PhaseMode mode) noexcept;

    // Mixer
    void setMixMode(MixMode mode) noexcept;
    void setMixPosition(float mix) noexcept;

    // Filter
    void setFilterType(RuinaeFilterType type) noexcept;
    void setFilterCutoff(float hz) noexcept;
    void setFilterResonance(float q) noexcept;
    void setFilterEnvAmount(float semitones) noexcept;
    void setFilterKeyTrack(float amount) noexcept;

    // Distortion
    void setDistortionType(RuinaeDistortionType type) noexcept;
    void setDistortionDrive(float drive) noexcept;
    void setDistortionCharacter(float character) noexcept;

    // Trance Gate
    void setTranceGateEnabled(bool enabled) noexcept;
    void setTranceGateParams(const TranceGateParams& params) noexcept;
    void setTranceGateStep(int index, float level) noexcept;

    // Amplitude Envelope
    void setAmpAttack(float ms) noexcept;
    void setAmpDecay(float ms) noexcept;
    void setAmpSustain(float level) noexcept;
    void setAmpRelease(float ms) noexcept;
    void setAmpAttackCurve(EnvCurve curve) noexcept;
    void setAmpDecayCurve(EnvCurve curve) noexcept;
    void setAmpReleaseCurve(EnvCurve curve) noexcept;

    // Filter Envelope
    void setFilterAttack(float ms) noexcept;
    void setFilterDecay(float ms) noexcept;
    void setFilterSustain(float level) noexcept;
    void setFilterRelease(float ms) noexcept;
    void setFilterAttackCurve(EnvCurve curve) noexcept;
    void setFilterDecayCurve(EnvCurve curve) noexcept;
    void setFilterReleaseCurve(EnvCurve curve) noexcept;

    // Modulation Envelope
    void setModAttack(float ms) noexcept;
    void setModDecay(float ms) noexcept;
    void setModSustain(float level) noexcept;
    void setModRelease(float ms) noexcept;
    void setModAttackCurve(EnvCurve curve) noexcept;
    void setModDecayCurve(EnvCurve curve) noexcept;
    void setModReleaseCurve(EnvCurve curve) noexcept;

    // Per-voice modulation routing
    void setVoiceModRoute(int index, VoiceModRoute route) noexcept;
    void setVoiceModRouteScale(VoiceModDest dest, float scale) noexcept;

    // =========================================================================
    // Mono Mode Configuration (FR-036)
    // =========================================================================
    void setMonoPriority(MonoMode mode) noexcept;
    void setLegato(bool enabled) noexcept;
    void setPortamentoTime(float ms) noexcept;
    void setPortamentoMode(PortaMode mode) noexcept;

    // =========================================================================
    // Voice Allocator Configuration (FR-037)
    // =========================================================================
    void setAllocationMode(AllocationMode mode) noexcept;
    void setStealMode(StealMode mode) noexcept;

    // =========================================================================
    // NoteProcessor Configuration (FR-038)
    // =========================================================================
    void setPitchBendRange(float semitones) noexcept;
    void setTuningReference(float a4Hz) noexcept;
    void setVelocityCurve(VelocityCurve curve) noexcept;

    // =========================================================================
    // Tempo and Transport (FR-039)
    // =========================================================================
    void setTempo(double bpm) noexcept;
    void setBlockContext(const BlockContext& ctx) noexcept;

    // =========================================================================
    // State Queries (FR-040)
    // =========================================================================
    [[nodiscard]] uint32_t getActiveVoiceCount() const noexcept;
    [[nodiscard]] VoiceMode getMode() const noexcept;
};

} // namespace Krate::DSP
```

## Invariants

1. All 16 RuinaeVoice instances are pre-allocated after `prepare()`. No voice allocation during `processBlock()`.
2. `polyphonyCount_` is always in [1, kMaxPolyphony].
3. `masterGain_` is always in [kMinMasterGain, kMaxMasterGain].
4. `stereoSpread_` is always in [0.0, 1.0].
5. `stereoWidth_` is always in [0.0, 2.0].
6. Voice pan positions are recalculated when `polyphonyCount_` or `stereoSpread_` changes.
7. `gainCompensation_` equals `1.0f / sqrt(polyphonyCount_)` and is recalculated on `setPolyphony()`.
8. `processBlock()` produces silence if not prepared or `numSamples == 0`.
9. All output samples are finite (no NaN/Inf) after `processBlock()` returns.
10. When soft limiter is enabled, all output samples are in [-1.0, +1.0].

## Error Handling

- NaN/Inf float inputs to setters: silently ignored (no-op), consistent with `detail::isNaN()`/`detail::isInf()` pattern.
- Out-of-range integer/enum inputs: silently clamped.
- `noteOn` before `prepare()`: silently ignored (early return).
- `processBlock` before `prepare()`: outputs silence.
- `numSamples == 0`: early return, no state modified.
