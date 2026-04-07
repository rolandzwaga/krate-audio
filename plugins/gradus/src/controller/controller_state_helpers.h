#pragma once

// ==============================================================================
// Controller: Shared State Loading Helpers
// ==============================================================================
// Template helpers for speed curve and MIDI delay state deserialization,
// shared between setComponentState() and loadComponentStateWithNotify().
// ==============================================================================

#include "controller.h"
#include "../plugin_ids.h"
#include "../parameters/arpeggiator_params.h"
#include "../ui/speed_curve_data.h"
#include "../ui/speed_curve_editor.h"

#include "base/source/fstreamer.h"

#include <algorithm>
#include <cmath>

namespace Gradus {

template<typename SetParamFn>
void Controller::loadSpeedCurvesFromStream(
    Steinberg::IBStreamer& streamer, SetParamFn setParam)
{
    for (int lane = 0; lane < 8; ++lane) {
        Steinberg::int32 enabledInt = 0;
        if (!streamer.readInt32(enabledInt)) break;

        Steinberg::int32 presetIdx = 0;
        if (!streamer.readInt32(presetIdx)) break;

        Steinberg::int32 numPoints = 0;
        if (!streamer.readInt32(numPoints)) break;
        numPoints = std::clamp(numPoints, Steinberg::int32{0}, Steinberg::int32{64});

        SpeedCurveData curve;
        curve.enabled = (enabledInt != 0);
        curve.presetIndex = presetIdx;
        curve.points.clear();
        curve.points.reserve(static_cast<size_t>(numPoints));

        bool ok = true;
        for (Steinberg::int32 p = 0; p < numPoints; ++p) {
            SpeedCurvePoint pt;
            if (!streamer.readFloat(pt.x) || !streamer.readFloat(pt.y) ||
                !streamer.readFloat(pt.cpLeftX) || !streamer.readFloat(pt.cpLeftY) ||
                !streamer.readFloat(pt.cpRightX) || !streamer.readFloat(pt.cpRightY)) {
                ok = false;
                break;
            }
            curve.points.push_back(pt);
        }
        if (!ok) break;

        auto laneIdx = static_cast<size_t>(lane);

        // Store for later application when editors are created
        pendingSpeedCurves_[laneIdx] = curve;
        hasPendingSpeedCurves_ = true;

        // Update speed curve editor if it already exists
        if (speedCurveEditors_[laneIdx])
            speedCurveEditors_[laneIdx]->setCurveData(curve);

        // Send baked table to processor
        sendSpeedCurveTable(laneIdx, curve);
    }
}

template<typename SetParamFn>
void Controller::loadMidiDelayFromStream(
    Steinberg::IBStreamer& streamer, SetParamFn setParam)
{
    Steinberg::int32 iv = 0;
    float fv = 0.0f;

    // Lane length
    if (!streamer.readInt32(iv)) return;
    setParam(kArpMidiDelayLaneLengthId,
        static_cast<double>(std::clamp(static_cast<int>(iv), 1, 32) - 1) / 31.0);

    // Per-step time mode (32 steps)
    for (int i = 0; i < 32; ++i) {
        if (!streamer.readInt32(iv)) return;
        setParam(static_cast<Steinberg::Vst::ParamID>(kArpMidiDelayTimeModeStep0Id + i),
            iv ? 1.0 : 0.0);
    }
    // Per-step delay time (32 steps, stored as normalized 0-1)
    for (int i = 0; i < 32; ++i) {
        if (!streamer.readFloat(fv)) return;
        setParam(static_cast<Steinberg::Vst::ParamID>(kArpMidiDelayTimeStep0Id + i),
            static_cast<double>(std::clamp(fv, 0.0f, 1.0f)));
    }
    // Per-step feedback (32 steps)
    for (int i = 0; i < 32; ++i) {
        if (!streamer.readInt32(iv)) return;
        setParam(static_cast<Steinberg::Vst::ParamID>(kArpMidiDelayFeedbackStep0Id + i),
            static_cast<double>(std::clamp(static_cast<int>(iv), 0, 16)) / 16.0);
    }
    // Per-step velocity decay (32 steps)
    for (int i = 0; i < 32; ++i) {
        if (!streamer.readFloat(fv)) return;
        setParam(static_cast<Steinberg::Vst::ParamID>(kArpMidiDelayVelDecayStep0Id + i),
            static_cast<double>(std::clamp(fv, 0.0f, 1.0f)));
    }
    // Per-step pitch shift (32 steps)
    for (int i = 0; i < 32; ++i) {
        if (!streamer.readInt32(iv)) return;
        setParam(static_cast<Steinberg::Vst::ParamID>(kArpMidiDelayPitchShiftStep0Id + i),
            static_cast<double>(std::clamp(static_cast<int>(iv), -24, 24) + 24) / 48.0);
    }
    // Per-step gate scaling (32 steps)
    for (int i = 0; i < 32; ++i) {
        if (!streamer.readFloat(fv)) return;
        setParam(static_cast<Steinberg::Vst::ParamID>(kArpMidiDelayGateScaleStep0Id + i),
            static_cast<double>(std::clamp(fv, 0.1f, 2.0f) - 0.1f) / 1.9);
    }
    // Per-step active flags (32 steps) — EOF-safe for pre-active presets
    for (int i = 0; i < 32; ++i) {
        if (!streamer.readInt32(iv)) return;
        setParam(static_cast<Steinberg::Vst::ParamID>(kArpMidiDelayActiveStep0Id + i),
            iv ? 1.0 : 0.0);
    }
    // Lane metadata
    if (!streamer.readFloat(fv)) return;
    {
        float speed = std::clamp(fv, 0.25f, 4.0f);
        int bestIdx = 3;
        float bestDist = 99.0f;
        for (int i = 0; i < kLaneSpeedCount; ++i) {
            float dist = std::abs(kLaneSpeedValues[i] - speed);
            if (dist < bestDist) { bestDist = dist; bestIdx = i; }
        }
        setParam(kArpMidiDelayLaneSpeedId,
            static_cast<double>(bestIdx) / static_cast<double>(kLaneSpeedCount - 1));
    }
    if (streamer.readFloat(fv))
        setParam(kArpMidiDelayLaneSwingId,
            static_cast<double>(std::clamp(fv, 0.0f, 75.0f)) / 75.0);
    if (streamer.readInt32(iv))
        setParam(kArpMidiDelayLaneJitterId,
            static_cast<double>(std::clamp(static_cast<int>(iv), 0, 4)) / 4.0);
    if (streamer.readFloat(fv))
        setParam(kArpMidiDelayLaneSpeedCurveDepthId,
            static_cast<double>(std::clamp(fv, 0.0f, 1.0f)));
}

} // namespace Gradus
