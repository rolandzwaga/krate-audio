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
#include <array>
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
    // MB-087: mirror the fields no parameter holds, for the preset saves.
    for (std::size_t p = 0; p < padParamless_.size(); ++p)
        padParamless_[p] =
            Membrum::ControllerState::paramlessFieldsFromSnapshot(kit.pads[p].sound);

    selectedPadIndex_ = kit.selectedPadIndex;
    // The sync requests kRefreshPadGrid: the grid takes the loaded enable
    // flags, category glyphs, CG / BUS indicators and (audit finding 6) the
    // loaded selection, on the UI thread (MB-157).
    syncGlobalProxyFromPad(selectedPadIndex_);

    // MB-103: a project load replaces every sound, so no browser-loaded
    // preset name is true any more.
    clearPresetBrowserEntries();
    requestViewRefresh(kRefreshPresetBrowserNames);

    return kResultOk;
}

void Controller::clearPresetBrowserEntries() noexcept
{
    lastKitPresetName_.clear();
    lastKitPresetPath_.clear();
    for (auto& name : padPresetNames_)
        name.clear();
    for (auto& path : padPresetPaths_)
        path.clear();
}

// ==============================================================================
// Kit Preset StateProvider / LoadProvider (FR-052, T033)
// ==============================================================================

IBStream* Controller::kitPresetStateProvider()
{
    // Build the kit snapshot via the shared encoder, then layer on the
    // session-scoped uiMode that only kit-presets persist (FR-030 / FR-072).
    auto* stream = new MemoryStream();

    auto kit = Membrum::ControllerState::buildSnapshot(
        *this, selectedPadIndex_, padParamless_);
    kit.hasSession = true;
    kit.uiMode     = (getParamNormalized(kUiModeId) >= 0.5) ? 1 : 0;

    Membrum::State::writeKitBlob(stream, kit);
    stream->seek(0, IBStream::kIBSeekSet, nullptr);
    return stream;
}

bool Controller::kitPresetLoadProvider(IBStream* stream,
                                       const Krate::Plugins::PresetInfo& info)
{
    const bool ok = kitPresetLoadProvider(stream);
    kitPresetLoadFailed_ = !ok;
    if (ok)
    {
        // MB-103: the kit replaced every pad's sound, so no pad entry holds.
        clearPresetBrowserEntries();
        lastKitPresetName_ = info.name;
        lastKitPresetPath_ = info.path;
        requestViewRefresh(kRefreshPresetBrowserNames);
    }
    return ok;
}

bool Controller::padPresetLoadProvider(IBStream* stream,
                                       const Krate::Plugins::PresetInfo& info)
{
    const bool ok = padPresetLoadProvider(stream);
    padPresetLoadFailed_ = !ok;
    if (ok)
    {
        const auto pad = static_cast<std::size_t>(std::clamp(selectedPadIndex_, 0, kNumPads - 1));
        padPresetNames_[pad] = info.name;
        padPresetPaths_[pad] = info.path;
        requestViewRefresh(kRefreshPresetBrowserNames);
    }
    return ok;
}

bool Controller::kitPresetLoadProvider(IBStream* stream)
{
    if (!stream)
        return false;

    // Read the whole stream first (an IBStream may not seek): the processor
    // gets these bytes, and the controller mirror below parses the same buffer.
    std::vector<char> bytes;
    {
        constexpr int32 kChunkSize = 4096;
        std::array<char, kChunkSize> chunk{};
        int32 bytesRead = 0;
        while (stream->read(chunk.data(), kChunkSize, &bytesRead) == kResultOk &&
               bytesRead > 0)
        {
            bytes.insert(bytes.end(), chunk.data(), chunk.data() + bytesRead);
        }
    }
    if (bytes.empty())
        return false;

    MemoryStream memStream(bytes.data(), static_cast<TSize>(bytes.size()));
    Membrum::State::KitSnapshot kit;
    if (Membrum::State::readKitBlob(&memStream, kit) != kResultOk)
        return false;

    // One preset-load path (R-202): the processor parses and applies these
    // bytes exactly as in setState (voice and coupling flushes, every pad
    // field including noiseLayerGain and the body-damping sentinel, macro
    // cache resync), keeping its selected pad (FR-052).
    if (auto msg = Steinberg::owned(allocateMessage()))
    {
        msg->setMessageID("LoadPresetState");
        if (auto* attrs = msg->getAttributes())
        {
            attrs->setBinary("bytes", bytes.data(),
                             static_cast<Steinberg::uint32>(bytes.size()));
            sendMessage(msg);
        }
    }

    // The message above is the processor's only apply (MB-152): mirror the
    // values into the controller's parameters with the plain setter, as
    // setComponentState does. No begin / perform / endEdit: a replay would
    // apply the kit a second time and put every parameter into the host's
    // undo and automation. The host learns of the change below.
    Membrum::ControllerState::ParamSetter setDirect =
        [this](Steinberg::Vst::ParamID id, double v) {
            Steinberg::Vst::EditControllerEx1::setParamNormalized(id, v);
        };
    Membrum::ControllerState::applySnapshot(
        kit, setDirect, {.applySelectedPad = false});
    // MB-087: mirror the fields no parameter holds, for the preset saves.
    for (std::size_t p = 0; p < padParamless_.size(); ++p)
        padParamless_[p] =
            Membrum::ControllerState::paramlessFieldsFromSnapshot(kit.pads[p].sound);

    // Session-scoped uiMode: kit presets restore it when present, but the
    // controller-side write is direct (no performEdit) because the processor
    // never consumes uiMode.
    if (kit.hasSession)
    {
        EditControllerEx1::setParamNormalized(kUiModeId,
            kit.uiMode >= 1 ? 1.0 : 0.0);
    }

    // Kit preset deliberately preserves the user's current selectedPadIndex_
    // (FR-052), so the global proxy sync uses the EXISTING value. The sync
    // also refreshes the pad grid (kRefreshPadGrid).
    syncGlobalProxyFromPad(selectedPadIndex_);

    // A preset change inside the plugin: the host re-reads every parameter
    // value and marks the project modified.
    if (auto* handler = getComponentHandler())
        handler->restartComponent(kParamValuesChanged);
    setDirty(static_cast<TBool>(true));

    return true;
}

// ==============================================================================
// Pad Preset StateProvider / LoadProvider (FR-060 through FR-063, T040)
// ==============================================================================

IBStream* Controller::padPresetStateProvider()
{
    auto* stream = new MemoryStream();

    const int pad = selectedPadIndex_;
    const auto full = Membrum::ControllerState::buildPadSnapshotFromParams(
        *this, pad, padParamless_[static_cast<std::size_t>(pad)]);

    // Project the per-pad snapshot down to the per-pad preset slice (no choke,
    // bus, coupling or macros). The sound array has the same 59 slots; the
    // enable flag (51) and pan (56) are written but ignored on load (FR-061).
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

    // Read the whole stream first (an IBStream may not seek): the processor
    // gets these bytes, and the controller mirror below parses the same buffer.
    std::vector<char> bytes;
    {
        constexpr int32 kChunkSize = 4096;
        std::array<char, kChunkSize> chunk{};
        int32 bytesRead = 0;
        while (stream->read(chunk.data(), kChunkSize, &bytesRead) == kResultOk &&
               bytesRead > 0)
        {
            bytes.insert(bytes.end(), chunk.data(), chunk.data() + bytesRead);
        }
    }
    if (bytes.empty())
        return false;

    MemoryStream memStream(bytes.data(), static_cast<TSize>(bytes.size()));
    Membrum::State::PadPresetSnapshot preset;
    if (Membrum::State::readPadPresetBlob(&memStream, preset) != kResultOk)
        return false;

    // One pad-preset apply (R-203): the processor parses these bytes and
    // applies them to the target pad through State::applyPadPresetSnapshot
    // (noiseLayerGain, the body-damping sentinel; FR-061 keeps routing,
    // coupling, macros, enabled and pan).
    if (auto msg = Steinberg::owned(allocateMessage()))
    {
        msg->setMessageID("LoadPadPreset");
        if (auto* attrs = msg->getAttributes())
        {
            attrs->setBinary("bytes", bytes.data(),
                             static_cast<Steinberg::uint32>(bytes.size()));
            attrs->setInt("pad", selectedPadIndex_);
            sendMessage(msg);
        }
    }

    // The message above is the processor's only apply (MB-152): mirror the
    // values into the controller's parameters with the plain setter, no
    // begin / perform / endEdit replay. The host learns of the change below.
    Membrum::ControllerState::ParamSetter setDirect =
        [this](Steinberg::Vst::ParamID id, double v) {
            Steinberg::Vst::EditControllerEx1::setParamNormalized(id, v);
        };

    Membrum::ControllerState::applyPadPresetSnapshotToParams(
        selectedPadIndex_, preset, setDirect);
    padParamless_[static_cast<std::size_t>(selectedPadIndex_)] =
        Membrum::ControllerState::paramlessFieldsFromSnapshot(preset.sound);

    syncGlobalProxyFromPad(selectedPadIndex_);

    // A preset change inside the plugin: the host re-reads every parameter
    // value and marks the project modified.
    if (auto* handler = getComponentHandler())
        handler->restartComponent(kParamValuesChanged);
    setDirty(static_cast<TBool>(true));
    return true;
}
} // namespace Membrum
