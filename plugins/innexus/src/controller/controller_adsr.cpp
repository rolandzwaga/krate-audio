// ==============================================================================
// Edit Controller Implementation
// ==============================================================================

#include "controller.h"
#include "dsp/harmonic_snapshot_json.h"
#include "parameters/innexus_params.h"
#include "parameters/note_value_ui.h"
#include "plugin_ids.h"
#include "preset/innexus_preset_config.h"
#include "update/innexus_update_config.h"
#include "version.h"

#include "controller/views/harmonic_display_view.h"
#include "controller/views/confidence_indicator_view.h"
#include "controller/views/memory_slot_status_view.h"
#include "controller/views/evolution_position_view.h"
#include "controller/views/modulator_activity_view.h"
#include "controller/modulator_sub_controller.h"
#include "controller/adsr_expanded_overlay.h"
#include "controller/sample_drop_target.h"
#include "ui/adsr_display.h"
#include "ui/preset_browser_view.h"
#include "ui/save_preset_dialog_view.h"
#include "ui/update_banner_view.h"
#include "display/shared_display_bridge.h"
#include "display/display_bridge_log.h"

#include "vstgui/uidescription/uiattributes.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/cfileselector.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cviewcontainer.h"
#include "pluginterfaces/base/ibstream.h"
#include "public.sdk/source/common/memorystream.h"
#include "base/source/fstreamer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <vector>

namespace Innexus {

// ==============================================================================
// Controller ADSR display wiring + expanded overlay
// ==============================================================================


// ==============================================================================
// setParamNormalized — forward ADSR param changes to ADSRDisplay
// ==============================================================================
Steinberg::tresult PLUGIN_API Controller::setParamNormalized(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value)
{
    auto result = EditControllerEx1::setParamNormalized(id, value);

    // Forward ADSR parameter changes to both display views
    if (result == Steinberg::kResultOk
        && id >= kAdsrAttackId && id <= kAdsrReleaseCurveId)
    {
        auto norm = static_cast<float>(value);
        forwardAdsrParamToDisplay(adsrDisplayView_, id, norm);

        if (adsrExpandedOverlay_)
            forwardAdsrParamToDisplay(adsrExpandedOverlay_->getDisplay(), id, norm);
    }

    return result;
}

// ==============================================================================
// wireAdsrDisplay — wire parameter callbacks to an ADSRDisplay instance
// ==============================================================================
void Controller::wireAdsrDisplay(Krate::Plugins::ADSRDisplay* display)
{
    if (!display)
        return;

    display->setAdsrBaseParamId(kAdsrAttackId);
    display->setCurveBaseParamId(kAdsrAttackCurveId);

    display->setParameterCallback(
        [this](uint32_t paramId, float normalizedValue) {
            // ADSRDisplay uses cube-root normalization for time params:
            //   norm_cube = cbrt(ms / 10000)
            // The processor expects log normalization:
            //   norm_log = log(ms / kMin) / log(kMax / kMin)
            // Convert cube-root → ms → log for time parameters.
            double corrected = static_cast<double>(normalizedValue);
            if (paramId == kAdsrAttackId || paramId == kAdsrDecayId
                || paramId == kAdsrReleaseId) {
                constexpr float kMin = 1.0f;
                constexpr float kMax = 5000.0f;
                // Cube-root → plain ms
                float ms = normalizedValue * normalizedValue * normalizedValue * 10000.0f;
                ms = std::clamp(ms, kMin, kMax);
                // Plain ms → log-normalized
                corrected = static_cast<double>(
                    std::log(ms / kMin) / std::log(kMax / kMin));
            }
            performEdit(paramId, corrected);
            setParamNormalized(paramId, corrected);
        });
    display->setBeginEditCallback(
        [this](uint32_t paramId) { beginEdit(paramId); });
    display->setEndEditCallback(
        [this](uint32_t paramId) { endEdit(paramId); });

    if (adsrOutputPtr_ && adsrStagePtr_ && adsrActivePtr_)
        display->setPlaybackStatePointers(adsrOutputPtr_, adsrStagePtr_, adsrActivePtr_);
}

// ==============================================================================
// forwardAdsrParamToDisplay — forward a single ADSR parameter to a display
// ==============================================================================
void Controller::forwardAdsrParamToDisplay(Krate::Plugins::ADSRDisplay* display,
                                           Steinberg::Vst::ParamID id,
                                           float norm)
{
    if (!display)
        return;

    // Time params use log mapping: ms = kMin * pow(kMax/kMin, norm)
    // This matches the processor's denormalization in processParameterChanges.
    constexpr float kMin = 1.0f;
    constexpr float kMax = 5000.0f;

    switch (id) {
        case kAdsrAttackId:
            display->setAttackMs(kMin * std::pow(kMax / kMin, norm));
            break;
        case kAdsrDecayId:
            display->setDecayMs(kMin * std::pow(kMax / kMin, norm));
            break;
        case kAdsrSustainId:
            display->setSustainLevel(norm);
            break;
        case kAdsrReleaseId:
            display->setReleaseMs(kMin * std::pow(kMax / kMin, norm));
            break;
        case kAdsrAttackCurveId:
            display->setAttackCurve(norm * 2.0f - 1.0f);
            break;
        case kAdsrDecayCurveId:
            display->setDecayCurve(norm * 2.0f - 1.0f);
            break;
        case kAdsrReleaseCurveId:
            display->setReleaseCurve(norm * 2.0f - 1.0f);
            break;
        default:
            break;
    }
}

// ==============================================================================
// openAdsrExpandedOverlay / closeAdsrExpandedOverlay
// ==============================================================================
void Controller::openAdsrExpandedOverlay()
{
    if (adsrExpandedOverlay_ && !adsrExpandedOverlay_->isOpen()) {
        // Sync expanded display with current parameter values
        if (auto* display = adsrExpandedOverlay_->getDisplay()) {
            constexpr float kMin = 1.0f;
            constexpr float kMax = 5000.0f;
            auto normToMs = [&](Steinberg::Vst::ParamID id) -> float {
                auto norm = static_cast<float>(getParamNormalized(id));
                return kMin * std::pow(kMax / kMin, norm);
            };
            auto normToCurve = [&](Steinberg::Vst::ParamID id) -> float {
                return static_cast<float>(getParamNormalized(id)) * 2.0f - 1.0f;
            };

            display->setAttackMs(normToMs(kAdsrAttackId));
            display->setDecayMs(normToMs(kAdsrDecayId));
            display->setSustainLevel(
                static_cast<float>(getParamNormalized(kAdsrSustainId)));
            display->setReleaseMs(normToMs(kAdsrReleaseId));
            display->setAttackCurve(normToCurve(kAdsrAttackCurveId));
            display->setDecayCurve(normToCurve(kAdsrDecayCurveId));
            display->setReleaseCurve(normToCurve(kAdsrReleaseCurveId));
        }
        adsrExpandedOverlay_->open();
    }
}

void Controller::closeAdsrExpandedOverlay()
{
    if (adsrExpandedOverlay_ && adsrExpandedOverlay_->isOpen())
        adsrExpandedOverlay_->close();
}
} // namespace Innexus
