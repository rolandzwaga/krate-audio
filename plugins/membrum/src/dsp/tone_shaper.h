#pragma once

// ==============================================================================
// ToneShaper -- Phase 2 no-op stub (data-model.md §6)
// ==============================================================================
// Declares the full ToneShaper API so DrumVoice can wire it into its
// signal chain, but every method is an unconditional no-op for Phase 2.A.
// isBypassed() returns true so the Phase 1 regression test (bit-identity with
// Phase 1) passes. The real SVF/drive/fold/pitch-env implementation arrives in
// Phase 5.
// ==============================================================================

namespace Membrum {

// Minimal enum so plugin_ids can reference filter-type values symbolically.
enum class ToneShaperFilterType : int
{
    Lowpass  = 0,
    Highpass = 1,
    Bandpass = 2,
};

enum class ToneShaperCurve : int
{
    Exponential = 0,
    Linear      = 1,
};

class ToneShaper
{
public:
    void prepare(double sampleRate) noexcept { sampleRate_ = sampleRate; }
    void reset() noexcept {}

    // Filter setters
    void setFilterType(ToneShaperFilterType type) noexcept { filterType_ = type; }
    void setFilterCutoff(float hz) noexcept { filterCutoffHz_ = hz; }
    void setFilterResonance(float q) noexcept { filterResonance_ = q; }
    void setFilterEnvAmount(float amount) noexcept { filterEnvAmount_ = amount; }
    void setFilterEnvAttackMs(float ms) noexcept { filterEnvAttackMs_ = ms; }
    void setFilterEnvDecayMs(float ms) noexcept { filterEnvDecayMs_ = ms; }
    void setFilterEnvSustain(float level) noexcept { filterEnvSustain_ = level; }
    void setFilterEnvReleaseMs(float ms) noexcept { filterEnvReleaseMs_ = ms; }

    // Drive / fold
    void setDriveAmount(float amount) noexcept { driveAmount_ = amount; }
    void setFoldAmount(float amount) noexcept { foldAmount_ = amount; }

    // Pitch envelope
    void setPitchEnvStartHz(float hz) noexcept { pitchEnvStartHz_ = hz; }
    void setPitchEnvEndHz(float hz) noexcept { pitchEnvEndHz_ = hz; }
    void setPitchEnvTimeMs(float ms) noexcept { pitchEnvTimeMs_ = ms; }
    void setPitchEnvCurve(ToneShaperCurve curve) noexcept { pitchEnvCurve_ = curve; }

    // Lifecycle
    void noteOn(float /*velocity*/) noexcept {}
    void noteOff() noexcept {}

    // Phase 2.A: pitch envelope disabled, return the start-Hz constant.
    [[nodiscard]] float processPitchEnvelope() noexcept { return pitchEnvStartHz_; }

    // Phase 2.A: no-op passthrough.
    [[nodiscard]] float processSample(float bodyOutput) noexcept { return bodyOutput; }

    // Phase 2.A: bypass is permanent.
    [[nodiscard]] bool isBypassed() const noexcept { return true; }

private:
    double sampleRate_ = 44100.0;

    // Filter state (values stored, not applied in stub)
    ToneShaperFilterType filterType_     = ToneShaperFilterType::Lowpass;
    float filterCutoffHz_                = 20000.0f;
    float filterResonance_               = 0.0f;
    float filterEnvAmount_               = 0.0f;
    float filterEnvAttackMs_             = 1.0f;
    float filterEnvDecayMs_              = 100.0f;
    float filterEnvSustain_              = 0.0f;
    float filterEnvReleaseMs_            = 100.0f;

    // Drive / fold state
    float driveAmount_ = 0.0f;
    float foldAmount_  = 0.0f;

    // Pitch envelope state
    float pitchEnvStartHz_        = 160.0f;
    float pitchEnvEndHz_          = 50.0f;
    float pitchEnvTimeMs_         = 0.0f;     // 0 = disabled
    ToneShaperCurve pitchEnvCurve_ = ToneShaperCurve::Exponential;
};

} // namespace Membrum
