#pragma once

// ==============================================================================
// Innexus Preset Format Header (v2)
// ==============================================================================
// Shared format definitions for the Innexus preset generator and compatibility
// tests. The serialize() method must produce byte-identical output to
// Processor::getState() in processor_state.cpp.
//
// Reference: plugins/innexus/src/processor/processor_state.cpp getState()
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

    void writeInt64(int64_t val) {
        auto bytes = reinterpret_cast<const uint8_t*>(&val);
        data.insert(data.end(), bytes, bytes + 8);
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

static constexpr int32_t kStateVersion = 2;
static constexpr size_t kMaxPartials = 96;
static constexpr size_t kResidualBands = 16;
static constexpr int32_t kInstanceIdMarker = 0x4B524154; // "KRAT"

// ==============================================================================
// Preset State (v2 — includes all physical modelling & voice parameters)
// ==============================================================================

struct InnexusPresetState {
    // --- M1 parameters ---
    float releaseTimeMs = 100.0f;       // 20-5000ms
    float inharmonicityAmount = 1.0f;   // normalized 0-1, default 1.0
    float masterGain = 0.8f;            // normalized 0-1, default 0.8
    float bypass = 0.0f;               // 0 or 1

    // --- File path (empty for factory presets) ---
    std::string filePath;

    // --- M2 parameters (stored as plain values) ---
    float harmonicLevelPlain = 1.0f;    // plain 0.0-2.0, default 1.0
    float residualLevelPlain = 0.3f;    // plain 0.0-2.0, default 0.3
    float brightnessPlain = 0.0f;       // plain -1.0 to +1.0, default 0.0
    float transientEmphasisPlain = 0.0f; // plain 0.0-2.0, default 0.0

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

    // --- M6 parameters (all normalized 0.0-1.0) ---
    float stereoSpread = 0.0f;
    float evolutionEnable = 0.0f;       // 0 or 1
    float evolutionSpeed = 0.0f;        // normalized
    float evolutionDepth = 0.5f;
    float evolutionMode = 0.0f;         // 0=Cycle, 0.5=PingPong, 1.0=RandomWalk
    float mod1Enable = 0.0f;
    float mod1Waveform = 0.0f;          // 0=Sine, 0.25=Tri, 0.5=Sq, 0.75=Saw, 1.0=RSH
    float mod1Rate = 0.0f;              // normalized
    float mod1Depth = 0.0f;
    float mod1RangeStart = 0.0f;        // normalized (1-96 -> 0-1)
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

    // --- ADSR Envelope Detection ---
    float adsrAttackMs = 10.0f;         // 1-5000ms
    float adsrDecayMs = 100.0f;         // 1-5000ms
    float adsrSustainLevel = 1.0f;      // 0-1
    float adsrReleaseMs = 100.0f;       // 1-5000ms
    float adsrAmount = 0.0f;            // 0-1
    float adsrTimeScale = 1.0f;         // 0.25-4.0
    float adsrAttackCurve = 0.0f;       // -1 to +1
    float adsrDecayCurve = 0.0f;        // -1 to +1
    float adsrReleaseCurve = 0.0f;      // -1 to +1

    // --- Partial Count ---
    float partialCount = 0.0f;          // normalized: 0=48, 1/3=64, 2/3=80, 1=96

    // --- Modulator Tempo Sync ---
    float mod1RateSync = 1.0f;          // 0=off, 1=on (default on)
    float mod1NoteValue = 0.5f;         // normalized (21 note values, default 1/8)
    float mod2RateSync = 1.0f;
    float mod2NoteValue = 0.5f;

    // --- Voice Mode ---
    float voiceMode = 0.0f;             // 0=Mono, 0.5=4Voices, 1.0=8Voices

    // --- Physical Modelling: Modal Resonator (Spec 127) ---
    float physModelMix = 0.0f;          // 0-1: blend residual vs physical model
    float resonanceDecay = 0.6295f;     // normalized (log: 0.01*500^norm = 0.5s)
    float resonanceBrightness = 0.5f;   // 0-1
    float resonanceStretch = 0.0f;      // 0-1
    float resonanceScatter = 0.0f;      // 0-1

    // --- Impact Exciter (Spec 128) ---
    float exciterType = 0.0f;           // 0=Residual, 0.5=Impact, 1.0=Bow
    float impactHardness = 0.5f;        // 0-1
    float impactMass = 0.3f;            // 0-1
    float impactBrightness = 0.5f;      // normalized (0.5 = neutral, maps to 0.0 plain)
    float impactPosition = 0.13f;       // 0-1

    // --- Waveguide String (Spec 129) ---
    float resonanceType = 0.0f;         // 0=Modal, 1=Waveguide
    float waveguideStiffness = 0.0f;    // 0-1
    float waveguidePickPosition = 0.13f; // 0-1

    // --- Bow Exciter (Spec 130) ---
    float bowPressure = 0.3f;           // 0-1
    float bowSpeed = 0.5f;              // 0-1
    float bowPosition = 0.13f;          // 0-1
    float bowOversampling = 0.0f;       // 0=off, 1=on

    // --- Body Resonance (Spec 131) ---
    float bodySize = 0.5f;              // 0-1
    float bodyMaterial = 0.5f;          // 0-1
    float bodyMix = 0.0f;               // 0-1

    // --- Sympathetic Resonance (Spec 132) ---
    float sympatheticAmount = 0.0f;     // 0-1
    float sympatheticDecay = 0.5f;      // 0-1

    std::vector<uint8_t> serialize() const {
        BinaryWriter w;

        // 1. State version (v2)
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

        // --- M6 parameters (v2: no timbralBlend) ---
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

        // --- ADSR Envelope Detection ---
        w.writeFloat(adsrAttackMs);
        w.writeFloat(adsrDecayMs);
        w.writeFloat(adsrSustainLevel);
        w.writeFloat(adsrReleaseMs);
        w.writeFloat(adsrAmount);
        w.writeFloat(adsrTimeScale);
        w.writeFloat(adsrAttackCurve);
        w.writeFloat(adsrDecayCurve);
        w.writeFloat(adsrReleaseCurve);

        // Per-slot ADSR data (8 slots x 9 floats = 72 floats, all defaults)
        for (int s = 0; s < 8; ++s) {
            w.writeFloat(10.0f);   // adsrAttackMs
            w.writeFloat(100.0f);  // adsrDecayMs
            w.writeFloat(1.0f);    // adsrSustainLevel
            w.writeFloat(100.0f);  // adsrReleaseMs
            w.writeFloat(0.0f);    // adsrAmount
            w.writeFloat(1.0f);    // adsrTimeScale
            w.writeFloat(0.0f);    // adsrAttackCurve
            w.writeFloat(0.0f);    // adsrDecayCurve
            w.writeFloat(0.0f);    // adsrReleaseCurve
        }

        // --- Partial Count ---
        w.writeFloat(partialCount);

        // --- Modulator Tempo Sync ---
        w.writeFloat(mod1RateSync);
        w.writeFloat(mod1NoteValue);
        w.writeFloat(mod2RateSync);
        w.writeFloat(mod2NoteValue);

        // --- Voice Mode ---
        w.writeFloat(voiceMode);

        // --- Physical Modelling: Modal Resonator (Spec 127) ---
        w.writeFloat(physModelMix);
        w.writeFloat(resonanceDecay);
        w.writeFloat(resonanceBrightness);
        w.writeFloat(resonanceStretch);
        w.writeFloat(resonanceScatter);

        // --- Impact Exciter (Spec 128) ---
        w.writeFloat(exciterType);
        w.writeFloat(impactHardness);
        w.writeFloat(impactMass);
        w.writeFloat(impactBrightness);
        w.writeFloat(impactPosition);

        // --- Waveguide String (Spec 129) ---
        w.writeFloat(resonanceType);
        w.writeFloat(waveguideStiffness);
        w.writeFloat(waveguidePickPosition);

        // --- Bow Exciter (Spec 130) ---
        w.writeFloat(bowPressure);
        w.writeFloat(bowSpeed);
        w.writeFloat(bowPosition);
        w.writeFloat(bowOversampling);

        // --- Body Resonance (Spec 131) ---
        w.writeFloat(bodySize);
        w.writeFloat(bodyMaterial);
        w.writeFloat(bodyMix);

        // --- Sympathetic Resonance (Spec 132) ---
        w.writeFloat(sympatheticAmount);
        w.writeFloat(sympatheticDecay);

        // --- Instance ID marker (for SharedDisplayBridge) ---
        w.writeInt32(kInstanceIdMarker);
        w.writeInt64(0); // instanceId = 0 for factory presets

        return w.data;
    }
};

} // namespace InnexusFormat
