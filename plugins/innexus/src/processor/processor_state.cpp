// ==============================================================================
// Processor State Management (getState / setState)
// ==============================================================================

#include "processor.h"

#include "pluginterfaces/base/ibstream.h"
#include "base/source/fstreamer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Innexus {

// ==============================================================================
// State Management (T083: FR-056, SC-009)
// ==============================================================================
Steinberg::tresult PLUGIN_API Processor::getState(Steinberg::IBStream* state)
{
    if (!state)
        return Steinberg::kResultFalse;

    Steinberg::IBStreamer streamer(state, kLittleEndian);

    // Write state version -- Spec B: version 8 (analysis feedback loop)
    streamer.writeInt32(8);

    // --- M1 parameters (unchanged) ---
    streamer.writeFloat(releaseTimeMs_.load(std::memory_order_relaxed));
    streamer.writeFloat(inharmonicityAmount_.load(std::memory_order_relaxed));
    streamer.writeFloat(masterGain_.load(std::memory_order_relaxed));
    streamer.writeFloat(bypass_.load(std::memory_order_relaxed));

    // Write sample file path (FR-056)
    auto pathLen = static_cast<Steinberg::int32>(loadedFilePath_.size());
    streamer.writeInt32(pathLen);
    if (pathLen > 0)
    {
        state->write(
            const_cast<char*>(loadedFilePath_.data()),
            pathLen, nullptr);
    }

    // --- M2 parameters (FR-027) ---
    // Write plain parameter values for residual controls
    const float harmLevelNorm = harmonicLevel_.load(std::memory_order_relaxed);
    const float resLevelNorm = residualLevel_.load(std::memory_order_relaxed);
    const float brightnessNorm = residualBrightness_.load(std::memory_order_relaxed);
    const float transientEmpNorm = transientEmphasis_.load(std::memory_order_relaxed);

    // Convert normalized to plain for persistence (data-model.md)
    streamer.writeFloat(harmLevelNorm * 2.0f);           // plain 0.0-2.0
    streamer.writeFloat(resLevelNorm * 2.0f);            // plain 0.0-2.0
    streamer.writeFloat(brightnessNorm * 2.0f - 1.0f);   // plain -1.0 to +1.0
    streamer.writeFloat(transientEmpNorm * 2.0f);        // plain 0.0-2.0

    // --- M2 residual frames (FR-027) ---
    const SampleAnalysis* analysis =
        currentAnalysis_.load(std::memory_order_acquire);

    if (analysis && !analysis->residualFrames.empty())
    {
        auto frameCount = static_cast<Steinberg::int32>(analysis->residualFrames.size());
        streamer.writeInt32(frameCount);
        streamer.writeInt32(static_cast<Steinberg::int32>(analysis->analysisFFTSize));
        streamer.writeInt32(static_cast<Steinberg::int32>(analysis->analysisHopSize));

        for (const auto& frame : analysis->residualFrames)
        {
            // 16 floats: bandEnergies
            for (size_t b = 0; b < Krate::DSP::kResidualBands; ++b)
            {
                streamer.writeFloat(frame.bandEnergies[b]);
            }
            // 1 float: totalEnergy
            streamer.writeFloat(frame.totalEnergy);
            // 1 int8: transientFlag
            streamer.writeInt8(frame.transientFlag ? static_cast<Steinberg::int8>(1)
                                                    : static_cast<Steinberg::int8>(0));
        }
    }
    else
    {
        // No residual data
        streamer.writeInt32(0); // residualFrameCount = 0
        streamer.writeInt32(0); // analysisFFTSize
        streamer.writeInt32(0); // analysisHopSize
    }

    // --- M3 parameters (sidechain) ---
    streamer.writeInt32(static_cast<Steinberg::int32>(
        inputSource_.load(std::memory_order_relaxed) > 0.5f ? 1 : 0));
    streamer.writeInt32(static_cast<Steinberg::int32>(
        latencyMode_.load(std::memory_order_relaxed) > 0.5f ? 1 : 0));

    // --- M4 parameters (musical control) ---
    streamer.writeInt8(freeze_.load(std::memory_order_relaxed) > 0.5f
        ? static_cast<Steinberg::int8>(1) : static_cast<Steinberg::int8>(0));
    streamer.writeFloat(morphPosition_.load(std::memory_order_relaxed));
    streamer.writeInt32(static_cast<Steinberg::int32>(
        std::round(harmonicFilterType_.load(std::memory_order_relaxed) * 4.0f)));
    streamer.writeFloat(responsiveness_.load(std::memory_order_relaxed));

    // --- M5 parameters (harmonic memory) ---
    // Selected slot index (FR-020a)
    const int selectedSlot = std::clamp(
        static_cast<int>(std::round(memorySlot_.load(std::memory_order_relaxed) * 7.0f)),
        0, 7);
    streamer.writeInt32(static_cast<Steinberg::int32>(selectedSlot));

    // Write all 8 memory slots (FR-020b)
    for (int s = 0; s < 8; ++s)
    {
        const auto& slot = memorySlots_[static_cast<size_t>(s)];
        streamer.writeInt8(slot.occupied ? static_cast<Steinberg::int8>(1)
                                        : static_cast<Steinberg::int8>(0));

        if (slot.occupied)
        {
            const auto& snap = slot.snapshot;
            streamer.writeFloat(snap.f0Reference);
            streamer.writeInt32(static_cast<Steinberg::int32>(snap.numPartials));

            for (size_t i = 0; i < Krate::DSP::kMaxPartials; ++i)
                streamer.writeFloat(snap.relativeFreqs[i]);
            for (size_t i = 0; i < Krate::DSP::kMaxPartials; ++i)
                streamer.writeFloat(snap.normalizedAmps[i]);
            for (size_t i = 0; i < Krate::DSP::kMaxPartials; ++i)
                streamer.writeFloat(snap.phases[i]);
            for (size_t i = 0; i < Krate::DSP::kMaxPartials; ++i)
                streamer.writeFloat(snap.inharmonicDeviation[i]);

            for (size_t i = 0; i < Krate::DSP::kResidualBands; ++i)
                streamer.writeFloat(snap.residualBands[i]);

            streamer.writeFloat(snap.residualEnergy);
            streamer.writeFloat(snap.globalAmplitude);
            streamer.writeFloat(snap.spectralCentroid);
            streamer.writeFloat(snap.brightness);
        }
    }

    // --- M6 parameters (creative extensions) ---
    // 31 normalized float values in data-model.md v6 state layout order
    streamer.writeFloat(timbralBlend_.load(std::memory_order_relaxed));
    streamer.writeFloat(stereoSpread_.load(std::memory_order_relaxed));
    streamer.writeFloat(evolutionEnable_.load(std::memory_order_relaxed));
    streamer.writeFloat(evolutionSpeed_.load(std::memory_order_relaxed));
    streamer.writeFloat(evolutionDepth_.load(std::memory_order_relaxed));
    streamer.writeFloat(evolutionMode_.load(std::memory_order_relaxed));
    streamer.writeFloat(mod1Enable_.load(std::memory_order_relaxed));
    streamer.writeFloat(mod1Waveform_.load(std::memory_order_relaxed));
    streamer.writeFloat(mod1Rate_.load(std::memory_order_relaxed));
    streamer.writeFloat(mod1Depth_.load(std::memory_order_relaxed));
    streamer.writeFloat(mod1RangeStart_.load(std::memory_order_relaxed));
    streamer.writeFloat(mod1RangeEnd_.load(std::memory_order_relaxed));
    streamer.writeFloat(mod1Target_.load(std::memory_order_relaxed));
    streamer.writeFloat(mod2Enable_.load(std::memory_order_relaxed));
    streamer.writeFloat(mod2Waveform_.load(std::memory_order_relaxed));
    streamer.writeFloat(mod2Rate_.load(std::memory_order_relaxed));
    streamer.writeFloat(mod2Depth_.load(std::memory_order_relaxed));
    streamer.writeFloat(mod2RangeStart_.load(std::memory_order_relaxed));
    streamer.writeFloat(mod2RangeEnd_.load(std::memory_order_relaxed));
    streamer.writeFloat(mod2Target_.load(std::memory_order_relaxed));
    streamer.writeFloat(detuneSpread_.load(std::memory_order_relaxed));
    streamer.writeFloat(blendEnable_.load(std::memory_order_relaxed));
    for (int i = 0; i < 8; ++i)
        streamer.writeFloat(blendSlotWeights_[static_cast<size_t>(i)].load(
            std::memory_order_relaxed));
    streamer.writeFloat(blendLiveWeight_.load(std::memory_order_relaxed));

    // --- Spec A: Harmonic Physics parameters (v7) ---
    streamer.writeFloat(warmth_.load(std::memory_order_relaxed));
    streamer.writeFloat(coupling_.load(std::memory_order_relaxed));
    streamer.writeFloat(stability_.load(std::memory_order_relaxed));
    streamer.writeFloat(entropy_.load(std::memory_order_relaxed));

    // --- Spec B: Analysis Feedback Loop parameters (v8) ---
    streamer.writeFloat(feedbackAmount_.load(std::memory_order_relaxed));
    streamer.writeFloat(feedbackDecay_.load(std::memory_order_relaxed));

    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Processor::setState(Steinberg::IBStream* state)
{
    if (!state)
        return Steinberg::kResultFalse;

    Steinberg::IBStreamer streamer(state, kLittleEndian);

    // Read version
    Steinberg::int32 version = 0;
    if (!streamer.readInt32(version))
        return Steinberg::kResultFalse;

    if (version >= 1)
    {
        float floatVal = 0.0f;

        // --- M1 parameters (unchanged) ---
        if (streamer.readFloat(floatVal))
            releaseTimeMs_.store(std::clamp(floatVal, 20.0f, 5000.0f));

        if (streamer.readFloat(floatVal))
            inharmonicityAmount_.store(std::clamp(floatVal, 0.0f, 1.0f));

        if (streamer.readFloat(floatVal))
            masterGain_.store(std::clamp(floatVal, 0.0f, 1.0f));

        if (streamer.readFloat(floatVal))
            bypass_.store(floatVal > 0.5f ? 1.0f : 0.0f);

        // Read sample file path (FR-056)
        Steinberg::int32 pathLen = 0;
        if (streamer.readInt32(pathLen) && pathLen > 0 && pathLen < 4096)
        {
            std::string filePath(static_cast<size_t>(pathLen), '\0'); // NOLINT(bugprone-unused-local-non-trivial-variable) -- used on line below; clang-tidy can't resolve includes
            Steinberg::int32 bytesRead = 0;
            if (state->read(filePath.data(), pathLen, &bytesRead) ==
                Steinberg::kResultOk && bytesRead == pathLen)
            {
                // Trigger re-analysis (FR-056: restore analysis on session reload)
                loadSample(filePath);
            }
        }
        else if (pathLen == 0)
        {
            loadedFilePath_.clear();
        }

        // --- M2 parameters and residual frames (FR-027) ---
        if (version >= 2)
        {
            // Read 4 new plain parameter values and convert to normalized
            if (streamer.readFloat(floatVal))
            {
                // harmonicLevel: plain 0.0-2.0, normalized = plain / 2.0
                harmonicLevel_.store(
                    std::clamp(floatVal, 0.0f, 2.0f) / 2.0f);
            }

            if (streamer.readFloat(floatVal))
            {
                // residualLevel: plain 0.0-2.0, normalized = plain / 2.0
                residualLevel_.store(
                    std::clamp(floatVal, 0.0f, 2.0f) / 2.0f);
            }

            if (streamer.readFloat(floatVal))
            {
                // brightness: plain -1.0 to +1.0, normalized = (plain + 1.0) / 2.0
                residualBrightness_.store(
                    (std::clamp(floatVal, -1.0f, 1.0f) + 1.0f) / 2.0f);
            }

            if (streamer.readFloat(floatVal))
            {
                // transientEmphasis: plain 0.0-2.0, normalized = plain / 2.0
                transientEmphasis_.store(
                    std::clamp(floatVal, 0.0f, 2.0f) / 2.0f);
            }

            // Read residual frames
            Steinberg::int32 residualFrameCount = 0;
            Steinberg::int32 analysisFFTSizeInt = 0;
            Steinberg::int32 analysisHopSizeInt = 0;

            if (streamer.readInt32(residualFrameCount) &&
                streamer.readInt32(analysisFFTSizeInt) &&
                streamer.readInt32(analysisHopSizeInt) &&
                residualFrameCount > 0)
            {
                auto analysisFFTSize = static_cast<size_t>(analysisFFTSizeInt);
                auto analysisHopSize = static_cast<size_t>(analysisHopSizeInt);

                // Reconstruct analysis with residual frames
                auto* analysis = currentAnalysis_.load(std::memory_order_acquire);
                auto* newAnalysis = analysis
                    ? new SampleAnalysis(*analysis)   // preserve harmonic data
                    : new SampleAnalysis();

                newAnalysis->analysisFFTSize = analysisFFTSize;
                newAnalysis->analysisHopSize = analysisHopSize;
                newAnalysis->residualFrames.clear();
                newAnalysis->residualFrames.reserve(
                    static_cast<size_t>(residualFrameCount));

                bool readOk = true;
                for (Steinberg::int32 f = 0; f < residualFrameCount && readOk; ++f)
                {
                    Krate::DSP::ResidualFrame frame;

                    // Read 16 band energies
                    for (size_t b = 0; b < Krate::DSP::kResidualBands; ++b)
                    {
                        if (!streamer.readFloat(floatVal))
                        {
                            readOk = false;
                            break;
                        }
                        frame.bandEnergies[b] = std::max(floatVal, 0.0f);
                    }
                    if (!readOk) break;

                    // Read totalEnergy
                    if (!streamer.readFloat(floatVal))
                    {
                        readOk = false;
                        break;
                    }
                    frame.totalEnergy = std::max(floatVal, 0.0f);

                    // Read transientFlag (int8)
                    Steinberg::int8 transientByte = 0;
                    if (!streamer.readInt8(transientByte))
                    {
                        readOk = false;
                        break;
                    }
                    frame.transientFlag = (transientByte != 0);

                    newAnalysis->residualFrames.push_back(frame);
                }

                if (readOk && !newAnalysis->residualFrames.empty())
                {
                    // Prepare residual synth for playback
                    if (analysisFFTSize > 0 && analysisHopSize > 0)
                    {
                        residualSynth_.prepare(
                            analysisFFTSize, analysisHopSize,
                            static_cast<float>(sampleRate_));
                    }

                    // Publish reconstructed analysis
                    auto* old = currentAnalysis_.exchange(
                        newAnalysis, std::memory_order_acq_rel);
                    pendingDeletion_ = old;
                }
                else
                {
                    delete newAnalysis;
                }
            }
        }
        else
        {
            // Version 1: set residual parameters to defaults (normalized)
            harmonicLevel_.store(0.5f);      // plain 1.0
            residualLevel_.store(0.5f);      // plain 1.0
            residualBrightness_.store(0.5f); // plain 0.0 (neutral)
            transientEmphasis_.store(0.0f);  // plain 0.0 (no boost)
        }

        // --- M3 parameters (sidechain) ---
        if (version >= 3)
        {
            Steinberg::int32 inputSourceInt = 0;
            Steinberg::int32 latencyModeInt = 0;
            if (streamer.readInt32(inputSourceInt))
                inputSource_.store(inputSourceInt > 0 ? 1.0f : 0.0f);
            if (streamer.readInt32(latencyModeInt))
            {
                latencyMode_.store(latencyModeInt > 0 ? 1.0f : 0.0f);
                // T078: Apply loaded latency mode to the live analysis pipeline
                auto restoredMode = latencyModeInt > 0
                    ? LatencyMode::HighPrecision
                    : LatencyMode::LowLatency;
                liveAnalysis_.setLatencyMode(restoredMode);
            }
        }
        else
        {
            // Default to sample mode, low latency
            inputSource_.store(0.0f);   // Sample
            latencyMode_.store(0.0f);   // LowLatency
            // Ensure pipeline is in default low-latency mode
            liveAnalysis_.setLatencyMode(LatencyMode::LowLatency);
        }

        // --- M4 parameters (musical control) ---
        if (version >= 4)
        {
            Steinberg::int8 freezeState = 0;
            if (streamer.readInt8(freezeState))
                freeze_.store(freezeState ? 1.0f : 0.0f);

            float morphPos = 0.0f;
            if (streamer.readFloat(morphPos))
                morphPosition_.store(std::clamp(morphPos, 0.0f, 1.0f));

            Steinberg::int32 filterType = 0;
            if (streamer.readInt32(filterType))
                harmonicFilterType_.store(
                    std::clamp(static_cast<float>(filterType) / 4.0f, 0.0f, 1.0f));

            float resp = 0.5f;
            if (streamer.readFloat(resp))
                responsiveness_.store(std::clamp(resp, 0.0f, 1.0f));
        }
        else
        {
            // Default M4 values for older states
            freeze_.store(0.0f);
            morphPosition_.store(0.0f);
            harmonicFilterType_.store(0.0f);  // All-Pass
            responsiveness_.store(0.5f);
        }

        // --- M5 parameters (harmonic memory) ---
        if (version >= 5)
        {
            // Read selected slot index (FR-022)
            Steinberg::int32 selectedSlot = 0;
            if (streamer.readInt32(selectedSlot))
            {
                selectedSlot = std::clamp(selectedSlot, static_cast<Steinberg::int32>(0),
                                         static_cast<Steinberg::int32>(7));
                memorySlot_.store(static_cast<float>(selectedSlot) / 7.0f,
                                 std::memory_order_relaxed);
            }

            // Read all 8 memory slots (FR-022)
            for (int s = 0; s < 8; ++s)
            {
                auto& slot = memorySlots_[static_cast<size_t>(s)];
                Steinberg::int8 occupiedByte = 0;
                if (!streamer.readInt8(occupiedByte))
                {
                    slot.occupied = false;
                    continue;
                }

                slot.occupied = (occupiedByte != 0);
                if (!slot.occupied)
                    continue;

                auto& snap = slot.snapshot;
                bool readOk = true;

                readOk = readOk && streamer.readFloat(floatVal);
                if (readOk) snap.f0Reference = floatVal;

                Steinberg::int32 numPartials = 0;
                readOk = readOk && streamer.readInt32(numPartials);
                if (readOk) snap.numPartials = static_cast<int>(
                    std::clamp(numPartials, static_cast<Steinberg::int32>(0),
                               static_cast<Steinberg::int32>(Krate::DSP::kMaxPartials)));

                for (size_t i = 0; i < Krate::DSP::kMaxPartials && readOk; ++i)
                {
                    readOk = streamer.readFloat(floatVal);
                    if (readOk) snap.relativeFreqs[i] = floatVal;
                }
                for (size_t i = 0; i < Krate::DSP::kMaxPartials && readOk; ++i)
                {
                    readOk = streamer.readFloat(floatVal);
                    if (readOk) snap.normalizedAmps[i] = floatVal;
                }
                for (size_t i = 0; i < Krate::DSP::kMaxPartials && readOk; ++i)
                {
                    readOk = streamer.readFloat(floatVal);
                    if (readOk) snap.phases[i] = floatVal;
                }
                for (size_t i = 0; i < Krate::DSP::kMaxPartials && readOk; ++i)
                {
                    readOk = streamer.readFloat(floatVal);
                    if (readOk) snap.inharmonicDeviation[i] = floatVal;
                }

                for (size_t i = 0; i < Krate::DSP::kResidualBands && readOk; ++i)
                {
                    readOk = streamer.readFloat(floatVal);
                    if (readOk) snap.residualBands[i] = floatVal;
                }

                readOk = readOk && streamer.readFloat(floatVal);
                if (readOk) snap.residualEnergy = floatVal;

                readOk = readOk && streamer.readFloat(floatVal);
                if (readOk) snap.globalAmplitude = floatVal;

                readOk = readOk && streamer.readFloat(floatVal);
                if (readOk) snap.spectralCentroid = floatVal;

                readOk = readOk && streamer.readFloat(floatVal);
                if (readOk) snap.brightness = floatVal;

                if (!readOk)
                {
                    slot.occupied = false;
                    slot.snapshot = Krate::DSP::HarmonicSnapshot{};
                }
            }
        }
        else
        {
            // Default M5 values for v4 and older states (FR-021)
            memorySlot_.store(0.0f, std::memory_order_relaxed);
            for (auto& slot : memorySlots_)
            {
                slot.occupied = false;
                slot.snapshot = Krate::DSP::HarmonicSnapshot{};
            }
        }

        // --- M6 parameters (creative extensions) ---
        if (version >= 6)
        {
            // Read 31 normalized float values in data-model.md v6 state layout order
            if (streamer.readFloat(floatVal))
                timbralBlend_.store(std::clamp(floatVal, 0.0f, 1.0f));
            if (streamer.readFloat(floatVal))
                stereoSpread_.store(std::clamp(floatVal, 0.0f, 1.0f));
            if (streamer.readFloat(floatVal))
                evolutionEnable_.store(floatVal > 0.5f ? 1.0f : 0.0f);
            if (streamer.readFloat(floatVal))
                evolutionSpeed_.store(std::clamp(floatVal, 0.0f, 1.0f));
            if (streamer.readFloat(floatVal))
                evolutionDepth_.store(std::clamp(floatVal, 0.0f, 1.0f));
            if (streamer.readFloat(floatVal))
                evolutionMode_.store(std::clamp(floatVal, 0.0f, 1.0f));
            if (streamer.readFloat(floatVal))
                mod1Enable_.store(floatVal > 0.5f ? 1.0f : 0.0f);
            if (streamer.readFloat(floatVal))
                mod1Waveform_.store(std::clamp(floatVal, 0.0f, 1.0f));
            if (streamer.readFloat(floatVal))
                mod1Rate_.store(std::clamp(floatVal, 0.0f, 1.0f));
            if (streamer.readFloat(floatVal))
                mod1Depth_.store(std::clamp(floatVal, 0.0f, 1.0f));
            if (streamer.readFloat(floatVal))
                mod1RangeStart_.store(std::clamp(floatVal, 0.0f, 1.0f));
            if (streamer.readFloat(floatVal))
                mod1RangeEnd_.store(std::clamp(floatVal, 0.0f, 1.0f));
            if (streamer.readFloat(floatVal))
                mod1Target_.store(std::clamp(floatVal, 0.0f, 1.0f));
            if (streamer.readFloat(floatVal))
                mod2Enable_.store(floatVal > 0.5f ? 1.0f : 0.0f);
            if (streamer.readFloat(floatVal))
                mod2Waveform_.store(std::clamp(floatVal, 0.0f, 1.0f));
            if (streamer.readFloat(floatVal))
                mod2Rate_.store(std::clamp(floatVal, 0.0f, 1.0f));
            if (streamer.readFloat(floatVal))
                mod2Depth_.store(std::clamp(floatVal, 0.0f, 1.0f));
            if (streamer.readFloat(floatVal))
                mod2RangeStart_.store(std::clamp(floatVal, 0.0f, 1.0f));
            if (streamer.readFloat(floatVal))
                mod2RangeEnd_.store(std::clamp(floatVal, 0.0f, 1.0f));
            if (streamer.readFloat(floatVal))
                mod2Target_.store(std::clamp(floatVal, 0.0f, 1.0f));
            if (streamer.readFloat(floatVal))
                detuneSpread_.store(std::clamp(floatVal, 0.0f, 1.0f));
            if (streamer.readFloat(floatVal))
                blendEnable_.store(floatVal > 0.5f ? 1.0f : 0.0f);
            for (int i = 0; i < 8; ++i)
            {
                if (streamer.readFloat(floatVal))
                    blendSlotWeights_[static_cast<size_t>(i)].store(
                        std::clamp(floatVal, 0.0f, 1.0f));
            }
            if (streamer.readFloat(floatVal))
                blendLiveWeight_.store(std::clamp(floatVal, 0.0f, 1.0f));

            // Update evolution engine waypoints and blender weights after state load
            evolutionEngine_.updateWaypoints(memorySlots_);
            for (int i = 0; i < 8; ++i)
                harmonicBlender_.setSlotWeight(
                    i, blendSlotWeights_[static_cast<size_t>(i)].load(
                        std::memory_order_relaxed));
            harmonicBlender_.setLiveWeight(
                blendLiveWeight_.load(std::memory_order_relaxed));
        }
        else
        {
            // Default M6 values for v5 and older states
            timbralBlend_.store(1.0f);
            stereoSpread_.store(0.0f);
            evolutionEnable_.store(0.0f);
            evolutionSpeed_.store(0.0f);
            evolutionDepth_.store(0.5f);
            evolutionMode_.store(0.0f);
            mod1Enable_.store(0.0f);
            mod1Waveform_.store(0.0f);
            mod1Rate_.store(0.0f);
            mod1Depth_.store(0.0f);
            mod1RangeStart_.store(0.0f);
            mod1RangeEnd_.store(1.0f);
            mod1Target_.store(0.0f);
            mod2Enable_.store(0.0f);
            mod2Waveform_.store(0.0f);
            mod2Rate_.store(0.0f);
            mod2Depth_.store(0.0f);
            mod2RangeStart_.store(0.0f);
            mod2RangeEnd_.store(1.0f);
            mod2Target_.store(0.0f);
            detuneSpread_.store(0.0f);
            blendEnable_.store(0.0f);
            for (auto& w : blendSlotWeights_)
                w.store(0.0f);
            blendLiveWeight_.store(0.0f);

            // Update evolution engine waypoints for v5 state
            evolutionEngine_.updateWaypoints(memorySlots_);
        }

        // --- Spec A: Harmonic Physics parameters (v7) ---
        // Default all physics params first, then overwrite from stream if v7+
        warmth_.store(0.0f);
        coupling_.store(0.0f);
        stability_.store(0.0f);
        entropy_.store(0.0f);

        if (version >= 7)
        {
            if (streamer.readFloat(floatVal))
                warmth_.store(std::clamp(floatVal, 0.0f, 1.0f));
            if (streamer.readFloat(floatVal))
                coupling_.store(std::clamp(floatVal, 0.0f, 1.0f));
            if (streamer.readFloat(floatVal))
                stability_.store(std::clamp(floatVal, 0.0f, 1.0f));
            if (streamer.readFloat(floatVal))
                entropy_.store(std::clamp(floatVal, 0.0f, 1.0f));
        }

        // --- Spec B: Analysis Feedback Loop parameters (v8) ---
        // Default both params first, then overwrite from stream if v8+
        feedbackAmount_.store(0.0f);
        feedbackDecay_.store(0.2f);

        if (version >= 8)
        {
            if (streamer.readFloat(floatVal))
                feedbackAmount_.store(std::clamp(floatVal, 0.0f, 1.0f));
            if (streamer.readFloat(floatVal))
                feedbackDecay_.store(std::clamp(floatVal, 0.0f, 1.0f));
        }
    }

    return Steinberg::kResultOk;
}

} // namespace Innexus
