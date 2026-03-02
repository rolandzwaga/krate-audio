// ==============================================================================
// Controller: ADSR Display Wiring
// ==============================================================================
// Extracted from controller.cpp - handles wireAdsrDisplay(), syncAdsrDisplay(),
// syncAdsrParamToDisplay(), and wireEnvDisplayPlayback().
// ==============================================================================

#include "controller.h"
#include "plugin_ids.h"
#include "ui/adsr_display.h"

// Parameter pack headers (for envTimeFromNormalized, envCurveFromNormalized)
#include "parameters/amp_env_params.h"
#include "parameters/filter_env_params.h"
#include "parameters/mod_env_params.h"

namespace Ruinae {

void Controller::wireAdsrDisplay(Krate::Plugins::ADSRDisplay* display) {
    if (!display) return;

    auto tag = display->getTag();

    // Identify which envelope this display belongs to based on control-tag
    Krate::Plugins::ADSRDisplay** displayPtr = nullptr;
    uint32_t adsrBaseId = 0;
    uint32_t curveBaseId = 0;
    uint32_t bezierEnabledId = 0;
    uint32_t bezierBaseId = 0;

    if (tag == kAmpEnvAttackId) {
        displayPtr = &ampEnvDisplay_;
        adsrBaseId = kAmpEnvAttackId;
        curveBaseId = kAmpEnvAttackCurveId;
        bezierEnabledId = kAmpEnvBezierEnabledId;
        bezierBaseId = kAmpEnvBezierAttackCp1XId;
    } else if (tag == kFilterEnvAttackId) {
        displayPtr = &filterEnvDisplay_;
        adsrBaseId = kFilterEnvAttackId;
        curveBaseId = kFilterEnvAttackCurveId;
        bezierEnabledId = kFilterEnvBezierEnabledId;
        bezierBaseId = kFilterEnvBezierAttackCp1XId;
    } else if (tag == kModEnvAttackId) {
        displayPtr = &modEnvDisplay_;
        adsrBaseId = kModEnvAttackId;
        curveBaseId = kModEnvAttackCurveId;
        bezierEnabledId = kModEnvBezierEnabledId;
        bezierBaseId = kModEnvBezierAttackCp1XId;
    } else {
        return; // Unknown tag - not an envelope display
    }

    *displayPtr = display;

    // Configure parameter IDs
    display->setAdsrBaseParamId(adsrBaseId);
    display->setCurveBaseParamId(curveBaseId);
    display->setBezierEnabledParamId(bezierEnabledId);
    display->setBezierBaseParamId(bezierBaseId);

    // Wire performEdit callback (display -> host)
    display->setParameterCallback(
        [this](uint32_t paramId, float normalizedValue) {
            performEdit(paramId, static_cast<double>(normalizedValue));
        });

    // Wire beginEdit/endEdit for gesture management
    display->setBeginEditCallback(
        [this](uint32_t paramId) {
            beginEdit(paramId);
        });

    display->setEndEditCallback(
        [this](uint32_t paramId) {
            endEdit(paramId);
        });

    // Sync current parameter values to the display
    syncAdsrDisplay(display, adsrBaseId, curveBaseId, bezierEnabledId, bezierBaseId);

    // Wire playback state pointers if already available
    wireEnvDisplayPlayback();
}

void Controller::syncAdsrDisplay(Krate::Plugins::ADSRDisplay* display,
                                  uint32_t adsrBaseId, uint32_t curveBaseId,
                                  uint32_t bezierEnabledId, uint32_t bezierBaseId) {
    if (!display) return;

    // Sync ADSR time/level parameters
    auto* attackParam = getParameterObject(adsrBaseId);
    auto* decayParam = getParameterObject(adsrBaseId + 1);
    auto* sustainParam = getParameterObject(adsrBaseId + 2);
    auto* releaseParam = getParameterObject(adsrBaseId + 3);

    if (attackParam) {
        display->setAttackMs(envTimeFromNormalized(attackParam->getNormalized()));
    }
    if (decayParam) {
        display->setDecayMs(envTimeFromNormalized(decayParam->getNormalized()));
    }
    if (sustainParam) {
        display->setSustainLevel(static_cast<float>(sustainParam->getNormalized()));
    }
    if (releaseParam) {
        display->setReleaseMs(envTimeFromNormalized(releaseParam->getNormalized()));
    }

    // Sync curve amounts
    auto* attackCurveParam = getParameterObject(curveBaseId);
    auto* decayCurveParam = getParameterObject(curveBaseId + 1);
    auto* releaseCurveParam = getParameterObject(curveBaseId + 2);

    if (attackCurveParam) {
        display->setAttackCurve(envCurveFromNormalized(attackCurveParam->getNormalized()));
    }
    if (decayCurveParam) {
        display->setDecayCurve(envCurveFromNormalized(decayCurveParam->getNormalized()));
    }
    if (releaseCurveParam) {
        display->setReleaseCurve(envCurveFromNormalized(releaseCurveParam->getNormalized()));
    }

    // Sync Bezier enabled
    auto* bezierEnabledParam = getParameterObject(bezierEnabledId);
    if (bezierEnabledParam) {
        display->setBezierEnabled(bezierEnabledParam->getNormalized() >= 0.5);
    }

    // Sync Bezier control points (12 consecutive values: 3 segments x 4 values)
    for (int seg = 0; seg < 3; ++seg) {
        for (int idx = 0; idx < 4; ++idx) {
            auto paramId = static_cast<Steinberg::Vst::ParamID>(
                bezierBaseId + static_cast<uint32_t>(seg * 4 + idx));
            auto* param = getParameterObject(paramId);
            if (param) {
                int handle = idx / 2;  // 0=cp1, 1=cp2
                int axis = idx % 2;    // 0=x, 1=y
                display->setBezierHandleValue(seg, handle, axis,
                    static_cast<float>(param->getNormalized()));
            }
        }
    }
}

void Controller::syncAdsrParamToDisplay(
    Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue value,
    Krate::Plugins::ADSRDisplay* display,
    uint32_t adsrBaseId, uint32_t curveBaseId,
    uint32_t bezierEnabledId, uint32_t bezierBaseId) {

    if (!display) return;

    // ADSR time/level parameters
    if (tag == adsrBaseId) {
        display->setAttackMs(envTimeFromNormalized(value));
    } else if (tag == adsrBaseId + 1) {
        display->setDecayMs(envTimeFromNormalized(value));
    } else if (tag == adsrBaseId + 2) {
        display->setSustainLevel(static_cast<float>(value));
    } else if (tag == adsrBaseId + 3) {
        display->setReleaseMs(envTimeFromNormalized(value));
    }
    // Curve amounts
    else if (tag == curveBaseId) {
        display->setAttackCurve(envCurveFromNormalized(value));
    } else if (tag == curveBaseId + 1) {
        display->setDecayCurve(envCurveFromNormalized(value));
    } else if (tag == curveBaseId + 2) {
        display->setReleaseCurve(envCurveFromNormalized(value));
    }
    // Bezier enabled
    else if (tag == bezierEnabledId) {
        display->setBezierEnabled(value >= 0.5);
    }
    // Bezier control points (12 consecutive: 3 segments x 4 values)
    else if (tag >= bezierBaseId && tag < bezierBaseId + 12) {
        uint32_t offset = tag - bezierBaseId;
        int seg = static_cast<int>(offset / 4);
        int idx = static_cast<int>(offset % 4);
        int handle = idx / 2;  // 0=cp1, 1=cp2
        int axis = idx % 2;    // 0=x, 1=y
        display->setBezierHandleValue(seg, handle, axis,
            static_cast<float>(value));
    }
}

void Controller::wireEnvDisplayPlayback() {
    // Wire atomic pointers to each ADSRDisplay instance for playback visualization
    if (ampEnvDisplay_ && ampEnvOutputPtr_ && ampEnvStagePtr_ && envVoiceActivePtr_) {
        ampEnvDisplay_->setPlaybackStatePointers(
            ampEnvOutputPtr_, ampEnvStagePtr_, envVoiceActivePtr_);
    }
    if (filterEnvDisplay_ && filterEnvOutputPtr_ && filterEnvStagePtr_ && envVoiceActivePtr_) {
        filterEnvDisplay_->setPlaybackStatePointers(
            filterEnvOutputPtr_, filterEnvStagePtr_, envVoiceActivePtr_);
    }
    if (modEnvDisplay_ && modEnvOutputPtr_ && modEnvStagePtr_ && envVoiceActivePtr_) {
        modEnvDisplay_->setPlaybackStatePointers(
            modEnvOutputPtr_, modEnvStagePtr_, envVoiceActivePtr_);
    }
}

} // namespace Ruinae
