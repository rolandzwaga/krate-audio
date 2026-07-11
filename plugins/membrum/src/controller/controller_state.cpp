// ==============================================================================
// Membrum Controller -- Phase 4 per-pad parameter registration + proxy logic
// ==============================================================================

#include "controller.h"
#include "plugin_ids.h"
#include "dsp/pad_config.h"
#include "dsp/exciter_type.h"
#include "dsp/body_model_type.h"
#include "state/state_codec.h"
#include "controller_state_codec.h"
#include "ui/pad_grid_view.h"
#include "ui/kit_meters_view.h"
#include "ui/polyphony_slider.h"
#include "ui/pitch_envelope_display.h"  // shared PitchEnvelopeDisplay (Krate::Plugins)
#include "ui/xy_morph_pad.h"             // shared XYMorphPad (Krate::Plugins)
#include "ui/adsr_display.h"             // shared ADSRDisplay   (Krate::Plugins)
#include "ui/adsr_expanded_overlay.h"    // Membrum::UI::ADSRExpandedOverlayView
#include "ui/membrum_buttons.h"          // Membrum::UI::IconExpandActionButton + shared OutlineActionButton
#include "preset/membrum_preset_config.h"

#include "preset/preset_manager.h"
#include "preset/preset_info.h"
#include "ui/preset_browser_view.h"
#include "ui/save_preset_dialog_view.h"

#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "base/source/fstring.h"

#include "vstgui/plugin-bindings/vst3editor.h"
#include "vstgui/uidescription/uiattributes.h"

#include "../ui/membrum_buttons.h"
#include "../ui/preset_inline_browser_view.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/cframe.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

namespace Membrum {
using namespace Steinberg;
using namespace Steinberg::Vst;

// ==============================================================================
// Controller state + kit/pad preset providers
// ==============================================================================


// ==============================================================================
// setComponentState -- Phase 4: read state and sync all parameter values
// ==============================================================================

tresult PLUGIN_API Controller::setComponentState(IBStream* state)
{
    if (!state)
        return kResultFalse;

    Membrum::State::KitSnapshot kit;
    if (Membrum::State::readKitBlob(state, kit) != kResultOk)
        return kResultFalse;

    // Phase 6 (T027): session-scoped kUiModeId always resets to Acoustic
    // (0.0) on state load. Kit presets may re-override it via a separate
    // preset-load callback (not through IBStream). uiMode lives outside
    // the snapshot helper because each load path has its own semantics
    // (host load forces 0; preset load reads from `kit.hasSession`).
    EditControllerEx1::setParamNormalized(kUiModeId, 0.0);

    // Host-driven path: the processor has already consumed its own state via
    // IComponent::setState, so we just mirror the values into the
    // controller's parameter objects -- no performEdit chain.
    Membrum::ControllerState::ParamSetter setDirect =
        [this](Steinberg::Vst::ParamID id, double v) {
            Steinberg::Vst::EditControllerEx1::setParamNormalized(id, v);
        };
    Membrum::ControllerState::applySnapshot(
        kit, setDirect, {.applySelectedPad = true});

    selectedPadIndex_ = kit.selectedPadIndex;
    pushKitEnabledToGrid(kit);
    syncGlobalProxyFromPad(selectedPadIndex_);
    // Audit finding 6: a loaded project may select a non-zero pad; move the
    // grid highlight to match instead of leaving it on the last clicked cell.
    if (padGridView_ != nullptr)
        padGridView_->setSelectedPadIndex(selectedPadIndex_);

    return kResultOk;
}

// ==============================================================================
// Kit Preset StateProvider / LoadProvider (FR-052, T033)
// ==============================================================================

IBStream* Controller::kitPresetStateProvider()
{
    // Build the kit snapshot via the shared encoder, then layer on the
    // session-scoped uiMode that only kit-presets persist (FR-030 / FR-072).
    auto* stream = new MemoryStream();

    auto kit = Membrum::ControllerState::buildSnapshot(*this, selectedPadIndex_);
    kit.hasSession = true;
    kit.uiMode     = (getParamNormalized(kUiModeId) >= 0.5) ? 1 : 0;

    Membrum::State::writeKitBlob(stream, kit);
    stream->seek(0, IBStream::kIBSeekSet, nullptr);
    return stream;
}

bool Controller::kitPresetLoadProvider(IBStream* stream)
{
    if (!stream)
        return false;

    Membrum::State::KitSnapshot kit;
    if (Membrum::State::readKitBlob(stream, kit) != kResultOk)
        return false;

    // Kit preset load bypasses the host's preset-load mechanism, so the
    // processor never sees the new values unless we notify the host through
    // beginEdit / performEdit / endEdit. Plain setParamNormalized only
    // updates the controller-side Parameter objects (UI).
    Membrum::ControllerState::ParamSetter setAndNotify =
        [this](Steinberg::Vst::ParamID id, double v) {
            const double clamped = std::clamp(v, 0.0, 1.0);
            Steinberg::Vst::EditControllerEx1::setParamNormalized(id, clamped);
            beginEdit(id);
            performEdit(id, clamped);
            endEdit(id);
        };
    Membrum::ControllerState::applySnapshot(
        kit, setAndNotify, {.applySelectedPad = false});

    // Session-scoped uiMode: kit presets restore it when present, but the
    // controller-side write is direct (no performEdit) because the processor
    // never consumes uiMode.
    if (kit.hasSession)
    {
        EditControllerEx1::setParamNormalized(kUiModeId,
            kit.uiMode >= 1 ? 1.0 : 0.0);
    }

    pushKitEnabledToGrid(kit);

    // Kit preset deliberately preserves the user's current selectedPadIndex_
    // (FR-052), so the global proxy sync uses the EXISTING value.
    syncGlobalProxyFromPad(selectedPadIndex_);

    return true;
}

// ==============================================================================
// Pad Preset StateProvider / LoadProvider (FR-060 through FR-063, T040)
// ==============================================================================

IBStream* Controller::padPresetStateProvider()
{
    auto* stream = new MemoryStream();

    const int pad = selectedPadIndex_;
    const auto full =
        Membrum::ControllerState::buildPadSnapshotFromParams(*this, pad);

    // Project the per-pad snapshot down to the narrower per-pad preset slice.
    // PadPresetSnapshot::sound is intentionally one slot shorter than
    // PadSnapshot::sound (the Phase 8F enable-toggle slot at index 51 is
    // kit-level only and never persisted in per-pad presets).
    Membrum::State::PadPresetSnapshot preset;
    preset.exciterType = full.exciterType;
    preset.bodyModel   = full.bodyModel;
    std::copy_n(full.sound.begin(), preset.sound.size(), preset.sound.begin());

    Membrum::State::writePadPresetBlob(stream, preset);
    stream->seek(0, IBStream::kIBSeekSet, nullptr);
    return stream;
}

bool Controller::padPresetLoadProvider(IBStream* stream)
{
    if (!stream)
        return false;

    Membrum::State::PadPresetSnapshot preset;
    if (Membrum::State::readPadPresetBlob(stream, preset) != kResultOk)
        return false;

    // Per-pad preset load bypasses the host, so we need begin/perform/end
    // around every write so the processor sees the new values.
    Membrum::ControllerState::ParamSetter setAndNotify =
        [this](Steinberg::Vst::ParamID id, double v) {
            const double clamped = std::clamp(v, 0.0, 1.0);
            Steinberg::Vst::EditControllerEx1::setParamNormalized(id, clamped);
            beginEdit(id);
            performEdit(id, clamped);
            endEdit(id);
        };

    Membrum::ControllerState::applyPadPresetSnapshotToParams(
        selectedPadIndex_, preset, setAndNotify);

    syncGlobalProxyFromPad(selectedPadIndex_);
    return true;
}
} // namespace Membrum
