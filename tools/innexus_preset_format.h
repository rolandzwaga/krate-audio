#pragma once

// ==============================================================================
// Innexus Preset Format Header
// ==============================================================================
// Shared format definitions for the Innexus preset generator and compatibility
// tests. The serialize() method must produce byte-identical output to
// Processor::getState() in processor_state.cpp.
//
// Reference: plugins/innexus/src/processor/processor_state.cpp getState()
// Reference: tools/ruinae_preset_format.h (established pattern)
// ==============================================================================

#include <vector>
#include <cstdint>
#include <array>
#include <string>

namespace InnexusFormat {

// ==============================================================================
// Binary Writer (matches IBStreamer little-endian format)
// ==============================================================================

class BinaryWriter {
public:
    std::vector<uint8_t> data;

    void writeInt32(int32_t val) {
        auto bytes = reinterpret_cast<const uint8_t*>(&val);
        data.insert(data.end(), bytes, bytes + 4);
    }

    void writeFloat(float val) {
        auto bytes = reinterpret_cast<const uint8_t*>(&val);
        data.insert(data.end(), bytes, bytes + 4);
    }

    void writeInt8(int8_t val) {
        data.push_back(static_cast<uint8_t>(val));
    }
};

// ==============================================================================
// Constants
// ==============================================================================

static constexpr int32_t kStateVersion = 8;
static constexpr size_t kMaxPartials = 48;
static constexpr size_t kResidualBands = 16;

// ==============================================================================
// Preset State
// ==============================================================================

struct InnexusPresetState {
    // --- M1 parameters ---
    float releaseTimeMs = 100.0f;       // 20-5000ms
    float inharmonicityAmount = 1.0f;   // normalized 0-1, default 1.0
    float masterGain = 0.5f;            // normalized 0-1
    float bypass = 0.0f;               // 0 or 1

    // --- File path (empty for factory presets) ---
    std::string filePath;

    // --- M2 parameters (stored as plain values) ---
    float harmonicLevelPlain = 1.0f;    // plain 0.0-2.0, default 1.0
    float residualLevelPlain = 1.0f;    // plain 0.0-2.0, default 1.0
    float brightnessPlain = 0.0f;       // plain -1.0 to +1.0, default 0.0
    float transientEmphasisPlain = 0.0f; // plain 0.0-2.0, default 0.0

    // --- M2 residual frames (empty for factory presets) ---
    // We write frameCount=0 to indicate no residual data

    // --- M3 parameters ---
    int32_t inputSource = 0;            // 0=Sample, 1=Sidechain
    int32_t latencyMode = 0;            // 0=LowLatency, 1=HighPrecision

    // --- M4 parameters ---
    int8_t freeze = 0;                  // 0=off, 1=on
    float morphPosition = 0.0f;         // 0.0-1.0
    int32_t harmonicFilterType = 0;     // 0-4 (AllPass/OddOnly/EvenOnly/LowPartials/HighPartials)
    float responsiveness = 0.5f;        // 0.0-1.0

    // --- M5 parameters ---
    int32_t selectedSlot = 0;           // 0-7
    // Memory slots: all empty for factory presets

    // --- M6 parameters (all normalized 0.0-1.0) ---
    float timbralBlend = 1.0f;
    float stereoSpread = 0.0f;
    float evolutionEnable = 0.0f;       // 0 or 1
    float evolutionSpeed = 0.0f;        // normalized
    float evolutionDepth = 0.5f;
    float evolutionMode = 0.0f;         // 0=Cycle, 0.5=PingPong, 1.0=RandomWalk
    float mod1Enable = 0.0f;
    float mod1Waveform = 0.0f;          // 0=Sine, 0.25=Tri, 0.5=Sq, 0.75=Saw, 1.0=RSH
    float mod1Rate = 0.0f;              // normalized
    float mod1Depth = 0.0f;
    float mod1RangeStart = 0.0f;        // normalized (1-48 -> 0-1)
    float mod1RangeEnd = 1.0f;
    float mod1Target = 0.0f;            // 0=Amplitude, 0.5=Frequency, 1.0=Pan
    float mod2Enable = 0.0f;
    float mod2Waveform = 0.0f;
    float mod2Rate = 0.0f;
    float mod2Depth = 0.0f;
    float mod2RangeStart = 0.0f;
    float mod2RangeEnd = 1.0f;
    float mod2Target = 0.0f;
    float detuneSpread = 0.0f;
    float blendEnable = 0.0f;
    float blendSlotWeights[8] = {};
    float blendLiveWeight = 0.0f;

    // --- Spec A: Harmonic Physics ---
    float warmth = 0.0f;               // 0.0-1.0
    float coupling = 0.0f;             // 0.0-1.0
    float stability = 0.0f;            // 0.0-1.0
    float entropy = 0.0f;              // 0.0-1.0

    // --- Spec B: Analysis Feedback Loop ---
    float feedbackAmount = 0.0f;        // 0.0-1.0
    float feedbackDecay = 0.2f;         // 0.0-1.0

    std::vector<uint8_t> serialize() const {
        BinaryWriter w;

        // 1. State version
        w.writeInt32(kStateVersion);

        // --- M1 parameters ---
        w.writeFloat(releaseTimeMs);
        w.writeFloat(inharmonicityAmount);
        w.writeFloat(masterGain);
        w.writeFloat(bypass);

        // --- File path ---
        auto pathLen = static_cast<int32_t>(filePath.size());
        w.writeInt32(pathLen);
        if (pathLen > 0) {
            for (char c : filePath)
                w.data.push_back(static_cast<uint8_t>(c));
        }

        // --- M2 parameters (plain values) ---
        w.writeFloat(harmonicLevelPlain);
        w.writeFloat(residualLevelPlain);
        w.writeFloat(brightnessPlain);
        w.writeFloat(transientEmphasisPlain);

        // --- M2 residual frames (none for factory presets) ---
        w.writeInt32(0); // residualFrameCount = 0
        w.writeInt32(0); // analysisFFTSize
        w.writeInt32(0); // analysisHopSize

        // --- M3 parameters ---
        w.writeInt32(inputSource);
        w.writeInt32(latencyMode);

        // --- M4 parameters ---
        w.writeInt8(freeze);
        w.writeFloat(morphPosition);
        w.writeInt32(harmonicFilterType);
        w.writeFloat(responsiveness);

        // --- M5 parameters ---
        w.writeInt32(selectedSlot);
        // 8 empty memory slots
        for (int s = 0; s < 8; ++s)
            w.writeInt8(0); // not occupied

        // --- M6 parameters ---
        w.writeFloat(timbralBlend);
        w.writeFloat(stereoSpread);
        w.writeFloat(evolutionEnable);
        w.writeFloat(evolutionSpeed);
        w.writeFloat(evolutionDepth);
        w.writeFloat(evolutionMode);
        w.writeFloat(mod1Enable);
        w.writeFloat(mod1Waveform);
        w.writeFloat(mod1Rate);
        w.writeFloat(mod1Depth);
        w.writeFloat(mod1RangeStart);
        w.writeFloat(mod1RangeEnd);
        w.writeFloat(mod1Target);
        w.writeFloat(mod2Enable);
        w.writeFloat(mod2Waveform);
        w.writeFloat(mod2Rate);
        w.writeFloat(mod2Depth);
        w.writeFloat(mod2RangeStart);
        w.writeFloat(mod2RangeEnd);
        w.writeFloat(mod2Target);
        w.writeFloat(detuneSpread);
        w.writeFloat(blendEnable);
        for (int i = 0; i < 8; ++i)
            w.writeFloat(blendSlotWeights[i]);
        w.writeFloat(blendLiveWeight);

        // --- Spec A: Harmonic Physics ---
        w.writeFloat(warmth);
        w.writeFloat(coupling);
        w.writeFloat(stability);
        w.writeFloat(entropy);

        // --- Spec B: Analysis Feedback Loop ---
        w.writeFloat(feedbackAmount);
        w.writeFloat(feedbackDecay);

        return w.data;
    }
};

} // namespace InnexusFormat
