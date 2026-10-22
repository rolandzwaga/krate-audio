#pragma once

// ==============================================================================
// Profundum - Edit Controller (UI thread)   Plan section 2.6
// ==============================================================================
// Constitution Principle I: VST3 Architecture Separation. This header includes
// no processor header; processor and controller share only plugin_ids.h and the
// parameters/*.h packs.
//
// No INoteExpressionController (Phase 10 FR-019). Phase 11 FR-050 (T021) adds
// IMidiMapping: on bus 0, every channel, CC 1 -> Mod Wheel (6), CC 64 -> Sustain
// Pedal (5), channel pressure -> Channel Pressure (7), pitch bend -> Pitch Bend (4).
// Phase 11 T014: initialize registers all 84 parameters (FR-001); the
// getParamStringByValue chain is global -> macro -> sound -> note (Portamento
// suffix from the current Glide Mode, FR-007) -> morph control -> performance
// -> base class.
// Phase 11 T016 (plan 4.6; FR-021, FR-022, FR-024, FR-025 controller side): the
// endpoint mirror is replaced only from the processor's kMsgEndpoint messages
// (the processor is authoritative); an Edit Target switch re-displays the 60
// E-parameters from the selected mirror, then calls restartComponent
// (kParamValuesChanged) once, with no beginEdit / performEdit / endEdit.
// Phase 11 T020 (plan 4.6; FR-025, FR-030, FR-031): copyEndpoint and setCompare
// send requests to the processor (the mirror and the compare mode change only
// from its replies); an Edit Target switch first sends kMsgRequestSync (the
// pull), and a reply that replaces the Edit Target endpoint AFTER the re-display
// re-displays and restarts again.
// FR-050: owns a PresetManager built from makeProfundumPresetConfig().
//
// The implementation is controller.cpp (plan section 2.6).
// ==============================================================================

#include "parameters/morph_params.h"
#include "preset/preset_manager.h"

#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

#include <krate/dsp/systems/bass_morph_engine.h>
#include <krate/dsp/systems/morph_state.h>

#include <array>
#include <cstddef>
#include <memory>

namespace Profundum {

class Controller : public Steinberg::Vst::EditControllerEx1, public Steinberg::Vst::IMidiMapping {  // FR-050
public:
    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IEditController*>(new Controller());
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) override;
    /// FR-022 / FR-024: the Edit Target (1001) hook.
    Steinberg::tresult PLUGIN_API setParamNormalized(Steinberg::Vst::ParamID tag,
                                                     Steinberg::Vst::ParamValue value) override;
    Steinberg::tresult PLUGIN_API getParamStringByValue(Steinberg::Vst::ParamID tag,
                                                        Steinberg::Vst::ParamValue valueNormalized,
                                                        Steinberg::Vst::String128 string) override;
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) override;
    /// FR-025: kMsgEndpoint replaces a mirror, kMsgCompareMode sets the compare mode; others -> base.
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) override;

    /// FR-050 (IMidiMapping): bus 0 only, every channel; CC 1 -> 6, CC 64 -> 5, kAfterTouch -> 7, kPitchBend -> 4;
    /// anything else, or bus != 0 -> kResultFalse.
    Steinberg::tresult PLUGIN_API getMidiControllerAssignment(Steinberg::int32 busIndex, Steinberg::int16 channel,
                                                              Steinberg::Vst::CtrlNumber midiControllerNumber,
                                                              Steinberg::Vst::ParamID& id) override;

    DEFINE_INTERFACES
        DEF_INTERFACE(Steinberg::Vst::IMidiMapping)
    END_DEFINE_INTERFACES(EditControllerEx1)
    REFCOUNT_METHODS(EditControllerEx1)

    /// FR-030: sends kMsgCopyEndpoint ("from") and calls setDirty(true). The mirror is NOT edited locally: the
    /// processor reports the changed destination endpoint (FR-025).
    void copyEndpoint(Krate::DSP::BassMorphEngine::Endpoint from);
    /// FR-031: sends kMsgCompare ("mode"). Only the processor's kMsgCompareMode reply updates compareModeForTest().
    void setCompare(CompareMode m);

    [[nodiscard]] Krate::Plugins::PresetManager* presetManagerForTest() const noexcept {
        return presetManager_.get();
    }
    [[nodiscard]] const Krate::DSP::MorphState& mirrorForTest(
        Krate::DSP::BassMorphEngine::Endpoint e) const noexcept {
        return mirror_[static_cast<std::size_t>(e)];
    }
    [[nodiscard]] CompareMode compareModeForTest() const noexcept { return compare_; }
    [[nodiscard]] int redisplayCountForTest() const noexcept { return redisplayCount_; }

private:
    /// The current Edit Target index (0 A, 1 B), read from parameter 1001.
    [[nodiscard]] int editTargetIndex();
    /// 60 x EditControllerEx1::setParamNormalized from the selected mirror (no host notification).
    void redisplayEditTarget();
    /// FR-044 v1 load: setParamNormalized(id, the registered defaultNormalizedValue).
    void setToRegisteredDefault(Steinberg::Vst::ParamID id);

    std::unique_ptr<Krate::Plugins::PresetManager> presetManager_;
    std::array<Krate::DSP::MorphState, 2> mirror_{};  // FR-021: [A, B]
    CompareMode compare_ = CompareMode::AB;           // FR-031, set only by the processor's message
    bool inStateLoad_ = false;                        // suppresses the 1001 hook during setComponentState
    int redisplayCount_ = 0;
    // FR-025 Edit Target pull: a kMsgRequestSync is out and its reply (which ends with kMsgCompareMode) has not
    // arrived; pullRedisplayed_ once the switch has re-displayed, so a later Edit Target reply restarts again.
    bool pullAwaitingReply_ = false;
    bool pullRedisplayed_ = false;
};

}  // namespace Profundum
