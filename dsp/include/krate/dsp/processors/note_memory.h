// ==============================================================================
// Layer 2: DSP Processor - Profundum Phase 4 (specs/profundum-phase4-drop-note-behaviour)
// ==============================================================================
// NoteMemory: per-voice note-transition policy resolver. Includes the standard
// library only.
//   Spec:  specs/profundum-phase4-drop-note-behaviour/spec.md (FR-030 to FR-036, D-7, D-12)
//   Plan:  specs/profundum-phase4-drop-note-behaviour/plan.md (S6)
//   Carries: SC-009(a), SC-017, SC-019.
//
// resolve() is a pure function of (mode, retrigger, transition, voiceAudible):
// no state between calls, no allocation, no audio path (FR-030, FR-035).
// ==============================================================================

#pragma once

#include <array>
#include <cstdint>

namespace Krate::DSP {

class NoteMemory {
public:
    /// FR-032: the concept's four Note Memory modes (D-7).
    enum class Mode : std::uint8_t {
        AlwaysRetrigger = 0,
        Legato = 1,
        EnvelopeRetriggerOnly = 2,
        OscillatorContinuous = 3
    };

    /// FR-032: oscillator phase behaviour on a restart.
    enum class Retrigger : std::uint8_t { Reset = 0, Continue = 1 };

    /// FR-031: transition kinds.
    enum class Transition : std::uint8_t {
        FromSilence = 0,
        Overlap = 1,
        Tail = 2,
        ReturnToHeld = 3,
        VoiceReuse = 4
    };

    /// Mirrors ProfundumCore::RetriggerPhase and AnchorOscillator::RetriggerPhase
    /// (same underlying values; SC-023).
    enum class PhasePolicy : std::uint8_t { Reset = 0, FreeRunning = 1 };

    /// FR-033.
    struct Actions {
        bool oscillatorRestart = false;
        PhasePolicy phase = PhasePolicy::FreeRunning;
        bool envelopeRestart = false;
        bool dropTrigger = false;
        bool dropSnap = false;
        bool anchorRearm = false;
        bool declick = false;
    };

    /// Out-of-enum values are ignored.
    void setMode(Mode m) noexcept
    {
        if (static_cast<std::uint8_t>(m) < kNumModes)
            mode_ = m;
    }

    /// Out-of-enum values are ignored.
    void setRetrigger(Retrigger r) noexcept
    {
        if (static_cast<std::uint8_t>(r) < kNumRetriggers)
            retrigger_ = r;
    }

    [[nodiscard]] Mode mode() const noexcept { return mode_; }
    [[nodiscard]] Retrigger retrigger() const noexcept { return retrigger_; }

    /// FR-030: resolve with the configured mode and Retrigger setting.
    [[nodiscard]] Actions resolve(Transition t, bool voiceAudible) const noexcept
    {
        return resolve(mode_, retrigger_, t, voiceAudible);
    }

    /// FR-033 / FR-034. An out-of-enum mode or transition resolves to a
    /// default-constructed Actions (nothing restarts).
    [[nodiscard]] static constexpr Actions resolve(Mode m, Retrigger r, Transition t,
                                                   bool voiceAudible) noexcept
    {
        const auto mi = static_cast<std::uint8_t>(m);
        const auto ti = static_cast<std::uint8_t>(t);
        if (mi >= kNumModes || ti >= kNumTransitions)
            return Actions{};

        const std::uint8_t cell = kMatrix[mi][ti];
        const bool osc = (cell & kOscBit) != 0;
        const bool env = (cell & kEnvBit) != 0;
        const bool phaseReset = osc && r == Retrigger::Reset;
        const bool declick = phaseReset && voiceAudible;

        Actions a{};
        a.oscillatorRestart = osc;
        a.phase = phaseReset ? PhasePolicy::Reset : PhasePolicy::FreeRunning;
        a.envelopeRestart = env;
        a.dropTrigger = env;
        a.dropSnap = !voiceAudible || declick;
        a.anchorRearm = env;
        a.declick = declick;
        return a;
    }

private:
    static constexpr std::uint8_t kNumModes = 4;
    static constexpr std::uint8_t kNumRetriggers = 2;
    static constexpr std::uint8_t kNumTransitions = 5;

    static constexpr std::uint8_t kOscBit = 0x1;
    static constexpr std::uint8_t kEnvBit = 0x2;
    static constexpr std::uint8_t k00 = 0;                  // (osc 0, env 0)
    static constexpr std::uint8_t k01 = kEnvBit;            // (osc 0, env 1)
    static constexpr std::uint8_t k11 = kOscBit | kEnvBit;  // (osc 1, env 1)

    /// FR-034 cells (oscillatorRestart, envelopeRestart).
    /// Columns: FromSilence, Overlap, Tail, ReturnToHeld, VoiceReuse.
    static constexpr std::array<std::array<std::uint8_t, kNumTransitions>, kNumModes> kMatrix{{
        {{k11, k11, k11, k11, k11}},  // AlwaysRetrigger
        {{k11, k00, k11, k00, k11}},  // Legato
        {{k11, k01, k01, k01, k01}},  // EnvelopeRetriggerOnly
        {{k01, k00, k01, k00, k01}},  // OscillatorContinuous
    }};

    Mode mode_ = Mode::AlwaysRetrigger;
    Retrigger retrigger_ = Retrigger::Reset;
};

} // namespace Krate::DSP
