#pragma once
#include "plugin_ids.h"
#include "controller/parameter_helpers.h"
#include "parameters/dropdown_mappings.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "base/source/fstreamer.h"
#include <algorithm>
#include <atomic>
#include <cstdio>

namespace Ruinae {

// =============================================================================
// Harmonizer Parameter Struct (E-001)
// =============================================================================

struct RuinaeHarmonizerParams {
    // Global parameters
    std::atomic<int>   harmonyMode{0};       // 0=Chromatic, 1=Scalic
    std::atomic<int>   key{0};               // 0=C, 1=C#, ..., 11=B
    std::atomic<int>   scale{0};             // ScaleType enum (0-8)
    std::atomic<int>   pitchShiftMode{0};    // PitchMode enum (0-3)
    std::atomic<bool>  formantPreserve{false};
    std::atomic<int>   numVoices{4};         // 1-4 (default 4)
    std::atomic<float> dryLevelDb{0.0f};     // -60 to +6 dB (default 0 dB)
    std::atomic<float> wetLevelDb{-6.0f};    // -60 to +6 dB (default -6 dB)

    // Per-voice parameters (4 voices)
    std::atomic<int>   voiceInterval[4]{{0}, {0}, {0}, {0}};       // -24 to +24 steps
    std::atomic<float> voiceLevelDb[4]{{0.0f}, {0.0f}, {0.0f}, {0.0f}};  // -60 to +6 dB
    std::atomic<float> voicePan[4]{{0.0f}, {0.0f}, {0.0f}, {0.0f}};      // -1 to +1
    std::atomic<float> voiceDelayMs[4]{{0.0f}, {0.0f}, {0.0f}, {0.0f}};  // 0 to 50 ms
    std::atomic<float> voiceDetuneCents[4]{{0.0f}, {0.0f}, {0.0f}, {0.0f}}; // -50 to +50 cents
};

// =============================================================================
// Parameter Change Handler (denormalization) -- T012
// =============================================================================

inline void handleHarmonizerParamChange(
    RuinaeHarmonizerParams& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {

    // Global parameters (2800-2807)
    switch (id) {
        case kHarmonizerHarmonyModeId:
            params.harmonyMode.store(
                std::clamp(static_cast<int>(value * (kHarmonyModeCount - 1) + 0.5), 0, kHarmonyModeCount - 1),
                std::memory_order_relaxed); return;
        case kHarmonizerKeyId:
            params.key.store(
                std::clamp(static_cast<int>(value * (kHarmonizerKeyCount - 1) + 0.5), 0, kHarmonizerKeyCount - 1),
                std::memory_order_relaxed); return;
        case kHarmonizerScaleId:
            params.scale.store(
                std::clamp(static_cast<int>(value * (kHarmonizerScaleCount - 1) + 0.5), 0, kHarmonizerScaleCount - 1),
                std::memory_order_relaxed); return;
        case kHarmonizerPitchShiftModeId:
            params.pitchShiftMode.store(
                std::clamp(static_cast<int>(value * (kHarmonizerPitchModeCount - 1) + 0.5), 0, kHarmonizerPitchModeCount - 1),
                std::memory_order_relaxed); return;
        case kHarmonizerFormantPreserveId:
            params.formantPreserve.store(value >= 0.5, std::memory_order_relaxed); return;
        case kHarmonizerNumVoicesId:
            params.numVoices.store(
                std::clamp(static_cast<int>(value * (kHarmonizerNumVoicesCount - 1) + 0.5), 0, kHarmonizerNumVoicesCount - 1) + 1,
                std::memory_order_relaxed); return;
        case kHarmonizerDryLevelId:
            // 0-1 -> -60 to +6 dB
            params.dryLevelDb.store(
                std::clamp(static_cast<float>(value * 66.0 - 60.0), -60.0f, 6.0f),
                std::memory_order_relaxed); return;
        case kHarmonizerWetLevelId:
            // 0-1 -> -60 to +6 dB
            params.wetLevelDb.store(
                std::clamp(static_cast<float>(value * 66.0 - 60.0), -60.0f, 6.0f),
                std::memory_order_relaxed); return;
        default: break;
    }

    // Per-voice parameters: determine voice index from ID range
    // Voice 1: 2810-2814 -> voiceIndex 0
    // Voice 2: 2820-2824 -> voiceIndex 1
    // Voice 3: 2830-2834 -> voiceIndex 2
    // Voice 4: 2840-2844 -> voiceIndex 3
    int voiceIndex = -1;
    Steinberg::Vst::ParamID offsetInVoice = 0;

    if (id >= kHarmonizerVoice1IntervalId && id <= kHarmonizerVoice1DetuneId) {
        voiceIndex = 0;
        offsetInVoice = id - kHarmonizerVoice1IntervalId;
    } else if (id >= kHarmonizerVoice2IntervalId && id <= kHarmonizerVoice2DetuneId) {
        voiceIndex = 1;
        offsetInVoice = id - kHarmonizerVoice2IntervalId;
    } else if (id >= kHarmonizerVoice3IntervalId && id <= kHarmonizerVoice3DetuneId) {
        voiceIndex = 2;
        offsetInVoice = id - kHarmonizerVoice3IntervalId;
    } else if (id >= kHarmonizerVoice4IntervalId && id <= kHarmonizerVoice4DetuneId) {
        voiceIndex = 3;
        offsetInVoice = id - kHarmonizerVoice4IntervalId;
    }

    if (voiceIndex < 0) return;

    auto vi = static_cast<size_t>(voiceIndex);
    switch (offsetInVoice) {
        case 0: // Interval: 0-1 -> round(norm * 48) - 24
            params.voiceInterval[vi].store(
                std::clamp(static_cast<int>(value * 48.0 + 0.5) - 24, -24, 24),
                std::memory_order_relaxed); break;
        case 1: // Level: 0-1 -> -60 to +6 dB
            params.voiceLevelDb[vi].store(
                std::clamp(static_cast<float>(value * 66.0 - 60.0), -60.0f, 6.0f),
                std::memory_order_relaxed); break;
        case 2: // Pan: 0-1 -> -1 to +1
            params.voicePan[vi].store(
                std::clamp(static_cast<float>(value * 2.0 - 1.0), -1.0f, 1.0f),
                std::memory_order_relaxed); break;
        case 3: // Delay: 0-1 -> 0 to 50 ms
            params.voiceDelayMs[vi].store(
                std::clamp(static_cast<float>(value * 50.0), 0.0f, 50.0f),
                std::memory_order_relaxed); break;
        case 4: // Detune: 0-1 -> -50 to +50 cents
            params.voiceDetuneCents[vi].store(
                std::clamp(static_cast<float>(value * 100.0 - 50.0), -50.0f, 50.0f),
                std::memory_order_relaxed); break;
        default: break;
    }
}

// =============================================================================
// Parameter Registration (T027)
// =============================================================================

inline void registerHarmonizerParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;

    // Normalized defaults per data-model.md E-002:
    // dryLevel: 0.909 (0 dB), wetLevel: 0.818 (-6 dB)
    // voice intervals: 0.5 (0 steps), voice levels: 0.909 (0 dB)
    // voice pans: 0.5 (center), voice delays: 0.0 (0 ms), voice detunes: 0.5 (0 cents)
    constexpr double kDefaultDryLevelNorm = 60.0 / 66.0;  // ~0.909
    constexpr double kDefaultWetLevelNorm = 54.0 / 66.0;  // ~0.818
    constexpr double kDefaultLevelNorm = 60.0 / 66.0;     // ~0.909
    constexpr double kDefaultPanNorm = 0.5;                // center
    constexpr double kDefaultDelayNorm = 0.0;              // 0 ms
    constexpr double kDefaultDetuneNorm = 0.5;             // 0 cents

    // --- Global dropdown params ---
    parameters.addParameter(createDropdownParameter(
        STR16("Harmony Mode"), kHarmonizerHarmonyModeId,
        {STR16("Chromatic"), STR16("Scalic")}));

    parameters.addParameter(createDropdownParameter(
        STR16("Harmonizer Key"), kHarmonizerKeyId,
        {STR16("C"), STR16("C#"), STR16("D"), STR16("Eb"),
         STR16("E"), STR16("F"), STR16("F#"), STR16("G"),
         STR16("Ab"), STR16("A"), STR16("Bb"), STR16("B")}));

    parameters.addParameter(createDropdownParameter(
        STR16("Harmonizer Scale"), kHarmonizerScaleId,
        {STR16("Major"), STR16("Natural Minor"), STR16("Harmonic Minor"),
         STR16("Melodic Minor"), STR16("Dorian"), STR16("Mixolydian"),
         STR16("Phrygian"), STR16("Lydian"), STR16("Chromatic"),
         STR16("Locrian"), STR16("Major Pentatonic"), STR16("Minor Pentatonic"),
         STR16("Blues"), STR16("Whole Tone"),
         STR16("Diminished (W-H)"), STR16("Diminished (H-W)")}));

    parameters.addParameter(createDropdownParameter(
        STR16("Pitch Shift Mode"), kHarmonizerPitchShiftModeId,
        {STR16("Simple"), STR16("Granular"),
         STR16("Phase Vocoder"), STR16("Pitch Sync")}));

    // --- Toggle ---
    parameters.addParameter(STR16("Formant Preserve"), STR16(""), 1, 0.0,
        ParameterInfo::kCanAutomate, kHarmonizerFormantPreserveId);

    // --- NumVoices dropdown (1-4, default 4) ---
    parameters.addParameter(createDropdownParameterWithDefault(
        STR16("Num Voices"), kHarmonizerNumVoicesId, 3,
        {STR16("1"), STR16("2"), STR16("3"), STR16("4")}));

    // --- Continuous global params ---
    parameters.addParameter(STR16("Harmonizer Dry Level"), STR16("dB"), 0, kDefaultDryLevelNorm,
        ParameterInfo::kCanAutomate, kHarmonizerDryLevelId);
    parameters.addParameter(STR16("Harmonizer Wet Level"), STR16("dB"), 0, kDefaultWetLevelNorm,
        ParameterInfo::kCanAutomate, kHarmonizerWetLevelId);

    // --- Per-voice params (4 voices) ---
    constexpr ParamID voiceIntervalIds[] = {
        kHarmonizerVoice1IntervalId, kHarmonizerVoice2IntervalId,
        kHarmonizerVoice3IntervalId, kHarmonizerVoice4IntervalId};
    constexpr ParamID voiceLevelIds[] = {
        kHarmonizerVoice1LevelId, kHarmonizerVoice2LevelId,
        kHarmonizerVoice3LevelId, kHarmonizerVoice4LevelId};
    constexpr ParamID voicePanIds[] = {
        kHarmonizerVoice1PanId, kHarmonizerVoice2PanId,
        kHarmonizerVoice3PanId, kHarmonizerVoice4PanId};
    constexpr ParamID voiceDelayIds[] = {
        kHarmonizerVoice1DelayId, kHarmonizerVoice2DelayId,
        kHarmonizerVoice3DelayId, kHarmonizerVoice4DelayId};
    constexpr ParamID voiceDetuneIds[] = {
        kHarmonizerVoice1DetuneId, kHarmonizerVoice2DetuneId,
        kHarmonizerVoice3DetuneId, kHarmonizerVoice4DetuneId};

    const char* voiceNames[] = {"V1", "V2", "V3", "V4"};

    for (int v = 0; v < 4; ++v) {
        // Interval: StringListParameter with 49 entries (-24..+24), default index 24 (0 steps)
        // COptionMenu requires StringListParameter (kIsList flag) to populate dropdown entries.
        Steinberg::Vst::String128 title;
        char titleBuf[64];

        {
            snprintf(titleBuf, sizeof(titleBuf), "%s Interval", voiceNames[v]);
            Steinberg::UString(title, 128).fromAscii(titleBuf);
            auto* intervalParam = new Steinberg::Vst::StringListParameter(
                title, voiceIntervalIds[v], nullptr,
                ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
            for (int step = -24; step <= 24; ++step) {
                char label[32];
                if (step > 0)
                    snprintf(label, sizeof(label), "+%d steps", step);
                else if (step < 0)
                    snprintf(label, sizeof(label), "%d steps", step);
                else
                    snprintf(label, sizeof(label), "0 steps");
                Steinberg::Vst::String128 labelStr;
                Steinberg::UString(labelStr, 128).fromAscii(label);
                intervalParam->appendString(labelStr);
            }
            // Default index 24 maps to "0 steps"
            auto defaultNorm = intervalParam->toNormalized(24.0);
            intervalParam->setNormalized(defaultNorm);
            intervalParam->getInfo().defaultNormalizedValue = defaultNorm;
            parameters.addParameter(intervalParam);
        }

        snprintf(titleBuf, sizeof(titleBuf), "%s Level", voiceNames[v]);
        Steinberg::UString(title, 128).fromAscii(titleBuf);
        parameters.addParameter(title, STR16("dB"), 0,
            kDefaultLevelNorm, ParameterInfo::kCanAutomate, voiceLevelIds[v]);

        snprintf(titleBuf, sizeof(titleBuf), "%s Pan", voiceNames[v]);
        Steinberg::UString(title, 128).fromAscii(titleBuf);
        parameters.addParameter(title, STR16(""), 0,
            kDefaultPanNorm, ParameterInfo::kCanAutomate, voicePanIds[v]);

        snprintf(titleBuf, sizeof(titleBuf), "%s Delay", voiceNames[v]);
        Steinberg::UString(title, 128).fromAscii(titleBuf);
        parameters.addParameter(title, STR16("ms"), 0,
            kDefaultDelayNorm, ParameterInfo::kCanAutomate, voiceDelayIds[v]);

        snprintf(titleBuf, sizeof(titleBuf), "%s Detune", voiceNames[v]);
        Steinberg::UString(title, 128).fromAscii(titleBuf);
        parameters.addParameter(title, STR16("ct"), 0,
            kDefaultDetuneNorm, ParameterInfo::kCanAutomate, voiceDetuneIds[v]);
    }
}

// =============================================================================
// Display Formatting (T028)
// =============================================================================

inline Steinberg::tresult formatHarmonizerParam(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    char8 text[32];

    // Global dB params
    if (id == kHarmonizerDryLevelId || id == kHarmonizerWetLevelId) {
        float dB = static_cast<float>(value * 66.0 - 60.0);
        snprintf(text, sizeof(text), "%.1f dB", dB);
        UString(string, 128).fromAscii(text);
        return kResultOk;
    }

    // Dropdowns: return kResultFalse so the host uses the StringListParameter string
    if (id == kHarmonizerHarmonyModeId || id == kHarmonizerKeyId ||
        id == kHarmonizerScaleId || id == kHarmonizerPitchShiftModeId ||
        id == kHarmonizerFormantPreserveId || id == kHarmonizerNumVoicesId) {
        return kResultFalse;
    }

    // Per-voice params: determine offset within voice block
    Steinberg::Vst::ParamID offset = 0;
    if (id >= kHarmonizerVoice1IntervalId && id <= kHarmonizerVoice1DetuneId)
        offset = id - kHarmonizerVoice1IntervalId;
    else if (id >= kHarmonizerVoice2IntervalId && id <= kHarmonizerVoice2DetuneId)
        offset = id - kHarmonizerVoice2IntervalId;
    else if (id >= kHarmonizerVoice3IntervalId && id <= kHarmonizerVoice3DetuneId)
        offset = id - kHarmonizerVoice3IntervalId;
    else if (id >= kHarmonizerVoice4IntervalId && id <= kHarmonizerVoice4DetuneId)
        offset = id - kHarmonizerVoice4IntervalId;
    else
        return kResultFalse;

    switch (offset) {
        case 0: { // Interval: show "+N steps" / "0 steps" / "-N steps"
            int steps = static_cast<int>(value * 48.0 + 0.5) - 24;
            if (steps > 0)
                snprintf(text, sizeof(text), "+%d steps", steps);
            else if (steps < 0)
                snprintf(text, sizeof(text), "%d steps", steps);
            else
                snprintf(text, sizeof(text), "0 steps");
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case 1: { // Level: dB
            float dB = static_cast<float>(value * 66.0 - 60.0);
            snprintf(text, sizeof(text), "%.1f dB", dB);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case 2: { // Pan: L/C/R with value
            float pan = static_cast<float>(value * 2.0 - 1.0);
            if (pan < -0.01f)
                snprintf(text, sizeof(text), "%.0fL", -pan * 100.0f);
            else if (pan > 0.01f)
                snprintf(text, sizeof(text), "%.0fR", pan * 100.0f);
            else
                snprintf(text, sizeof(text), "C");
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case 3: { // Delay: ms
            float ms = static_cast<float>(value * 50.0);
            snprintf(text, sizeof(text), "%.1f ms", ms);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case 4: { // Detune: cents
            float cents = static_cast<float>(value * 100.0 - 50.0);
            snprintf(text, sizeof(text), "%+.1f ct", cents);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        default: break;
    }
    return kResultFalse;
}

// =============================================================================
// State Save/Load (E-004)
// =============================================================================

inline void saveHarmonizerParams(const RuinaeHarmonizerParams& params, Steinberg::IBStreamer& streamer) {
    // Global ints
    streamer.writeInt32(params.harmonyMode.load(std::memory_order_relaxed));
    streamer.writeInt32(params.key.load(std::memory_order_relaxed));
    streamer.writeInt32(params.scale.load(std::memory_order_relaxed));
    streamer.writeInt32(params.pitchShiftMode.load(std::memory_order_relaxed));
    streamer.writeInt32(params.formantPreserve.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeInt32(params.numVoices.load(std::memory_order_relaxed));
    // Global floats
    streamer.writeFloat(params.dryLevelDb.load(std::memory_order_relaxed));
    streamer.writeFloat(params.wetLevelDb.load(std::memory_order_relaxed));
    // Per-voice (4 voices)
    for (int v = 0; v < 4; ++v) {
        auto vi = static_cast<size_t>(v);
        streamer.writeInt32(params.voiceInterval[vi].load(std::memory_order_relaxed));
        streamer.writeFloat(params.voiceLevelDb[vi].load(std::memory_order_relaxed));
        streamer.writeFloat(params.voicePan[vi].load(std::memory_order_relaxed));
        streamer.writeFloat(params.voiceDelayMs[vi].load(std::memory_order_relaxed));
        streamer.writeFloat(params.voiceDetuneCents[vi].load(std::memory_order_relaxed));
    }
}

inline bool loadHarmonizerParams(RuinaeHarmonizerParams& params, Steinberg::IBStreamer& streamer) {
    Steinberg::int32 iv = 0; float fv = 0.0f;
    // Global ints
    if (!streamer.readInt32(iv))
        return false;
    params.harmonyMode.store(iv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv))
        return false;
    params.key.store(iv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv))
        return false;
    params.scale.store(iv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv))
        return false;
    params.pitchShiftMode.store(iv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv))
        return false;
    params.formantPreserve.store(iv != 0, std::memory_order_relaxed);
    if (!streamer.readInt32(iv))
        return false;
    params.numVoices.store(iv, std::memory_order_relaxed);
    // Global floats
    if (!streamer.readFloat(fv))
        return false;
    params.dryLevelDb.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv))
        return false;
    params.wetLevelDb.store(fv, std::memory_order_relaxed);
    // Per-voice (4 voices)
    for (int v = 0; v < 4; ++v) {
        auto vi = static_cast<size_t>(v);
        if (!streamer.readInt32(iv))
            return false;
        params.voiceInterval[vi].store(iv, std::memory_order_relaxed);
        if (!streamer.readFloat(fv))
            return false;
        params.voiceLevelDb[vi].store(fv, std::memory_order_relaxed);
        if (!streamer.readFloat(fv))
            return false;
        params.voicePan[vi].store(fv, std::memory_order_relaxed);
        if (!streamer.readFloat(fv))
            return false;
        params.voiceDelayMs[vi].store(fv, std::memory_order_relaxed);
        if (!streamer.readFloat(fv))
            return false;
        params.voiceDetuneCents[vi].store(fv, std::memory_order_relaxed);
    }
    return true;
}

// =============================================================================
// Controller State Restore (T030)
// =============================================================================

template<typename SetParamFunc>
inline void loadHarmonizerParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    Steinberg::int32 iv = 0; float fv = 0.0f;
    // Global ints
    if (streamer.readInt32(iv)) setParam(kHarmonizerHarmonyModeId, static_cast<double>(iv) / (kHarmonyModeCount - 1));
    if (streamer.readInt32(iv)) setParam(kHarmonizerKeyId, static_cast<double>(iv) / (kHarmonizerKeyCount - 1));
    if (streamer.readInt32(iv)) setParam(kHarmonizerScaleId, static_cast<double>(iv) / (kHarmonizerScaleCount - 1));
    if (streamer.readInt32(iv)) setParam(kHarmonizerPitchShiftModeId, static_cast<double>(iv) / (kHarmonizerPitchModeCount - 1));
    if (streamer.readInt32(iv)) setParam(kHarmonizerFormantPreserveId, iv != 0 ? 1.0 : 0.0);
    if (streamer.readInt32(iv)) setParam(kHarmonizerNumVoicesId, static_cast<double>(iv - 1) / (kHarmonizerNumVoicesCount - 1));
    // Global floats: dB -> norm: (plain + 60) / 66
    if (streamer.readFloat(fv)) setParam(kHarmonizerDryLevelId, static_cast<double>((fv + 60.0f) / 66.0f));
    if (streamer.readFloat(fv)) setParam(kHarmonizerWetLevelId, static_cast<double>((fv + 60.0f) / 66.0f));
    // Per-voice (4 voices)
    constexpr Steinberg::Vst::ParamID voiceIntervalIds[] = {
        kHarmonizerVoice1IntervalId, kHarmonizerVoice2IntervalId,
        kHarmonizerVoice3IntervalId, kHarmonizerVoice4IntervalId};
    constexpr Steinberg::Vst::ParamID voiceLevelIds[] = {
        kHarmonizerVoice1LevelId, kHarmonizerVoice2LevelId,
        kHarmonizerVoice3LevelId, kHarmonizerVoice4LevelId};
    constexpr Steinberg::Vst::ParamID voicePanIds[] = {
        kHarmonizerVoice1PanId, kHarmonizerVoice2PanId,
        kHarmonizerVoice3PanId, kHarmonizerVoice4PanId};
    constexpr Steinberg::Vst::ParamID voiceDelayIds[] = {
        kHarmonizerVoice1DelayId, kHarmonizerVoice2DelayId,
        kHarmonizerVoice3DelayId, kHarmonizerVoice4DelayId};
    constexpr Steinberg::Vst::ParamID voiceDetuneIds[] = {
        kHarmonizerVoice1DetuneId, kHarmonizerVoice2DetuneId,
        kHarmonizerVoice3DetuneId, kHarmonizerVoice4DetuneId};
    for (int v = 0; v < 4; ++v) {
        if (streamer.readInt32(iv)) setParam(voiceIntervalIds[v], static_cast<double>(iv + 24) / 48.0);
        if (streamer.readFloat(fv)) setParam(voiceLevelIds[v], static_cast<double>((fv + 60.0f) / 66.0f));
        if (streamer.readFloat(fv)) setParam(voicePanIds[v], static_cast<double>((fv + 1.0f) / 2.0f));
        if (streamer.readFloat(fv)) setParam(voiceDelayIds[v], static_cast<double>(fv / 50.0f));
        if (streamer.readFloat(fv)) setParam(voiceDetuneIds[v], static_cast<double>((fv + 50.0f) / 100.0f));
    }
}

} // namespace Ruinae
