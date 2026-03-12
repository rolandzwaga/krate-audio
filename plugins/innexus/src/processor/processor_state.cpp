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

    // Write state version -- v1 (flat format, pre-release)
    streamer.writeInt32(1);

    // --- M1 parameters ---
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
            for (size_t b = 0; b < Krate::DSP::kResidualBands; ++b)
                streamer.writeFloat(frame.bandEnergies[b]);
            streamer.writeFloat(frame.totalEnergy);
            streamer.writeInt8(frame.transientFlag ? static_cast<Steinberg::int8>(1)
                                                    : static_cast<Steinberg::int8>(0));
        }
    }
    else
    {
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

    // --- Harmonic Physics parameters ---
    streamer.writeFloat(warmth_.load(std::memory_order_relaxed));
    streamer.writeFloat(coupling_.load(std::memory_order_relaxed));
    streamer.writeFloat(stability_.load(std::memory_order_relaxed));
    streamer.writeFloat(entropy_.load(std::memory_order_relaxed));

    // --- Analysis Feedback Loop parameters ---
    streamer.writeFloat(feedbackAmount_.load(std::memory_order_relaxed));
    streamer.writeFloat(feedbackDecay_.load(std::memory_order_relaxed));

    // --- ADSR Envelope Detection parameters ---
    streamer.writeFloat(adsrAttackMs_.load(std::memory_order_relaxed));
    streamer.writeFloat(adsrDecayMs_.load(std::memory_order_relaxed));
    streamer.writeFloat(adsrSustainLevel_.load(std::memory_order_relaxed));
    streamer.writeFloat(adsrReleaseMs_.load(std::memory_order_relaxed));
    streamer.writeFloat(adsrAmount_.load(std::memory_order_relaxed));
    streamer.writeFloat(adsrTimeScale_.load(std::memory_order_relaxed));
    streamer.writeFloat(adsrAttackCurve_.load(std::memory_order_relaxed));
    streamer.writeFloat(adsrDecayCurve_.load(std::memory_order_relaxed));
    streamer.writeFloat(adsrReleaseCurve_.load(std::memory_order_relaxed));

    // Per-slot ADSR data (8 slots x 9 floats = 72 floats)
    for (int i = 0; i < 8; ++i)
    {
        const auto& slot = memorySlots_[static_cast<size_t>(i)];
        streamer.writeFloat(slot.adsrAttackMs);
        streamer.writeFloat(slot.adsrDecayMs);
        streamer.writeFloat(slot.adsrSustainLevel);
        streamer.writeFloat(slot.adsrReleaseMs);
        streamer.writeFloat(slot.adsrAmount);
        streamer.writeFloat(slot.adsrTimeScale);
        streamer.writeFloat(slot.adsrAttackCurve);
        streamer.writeFloat(slot.adsrDecayCurve);
        streamer.writeFloat(slot.adsrReleaseCurve);
    }

    // --- Partial Count parameter ---
    streamer.writeFloat(partialCount_.load(std::memory_order_relaxed));

    // --- Modulator Tempo Sync parameters ---
    streamer.writeFloat(mod1RateSync_.load(std::memory_order_relaxed));
    streamer.writeFloat(mod1NoteValue_.load(std::memory_order_relaxed));
    streamer.writeFloat(mod2RateSync_.load(std::memory_order_relaxed));
    streamer.writeFloat(mod2NoteValue_.load(std::memory_order_relaxed));

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

    if (version != 1)
        return Steinberg::kResultFalse;

    float floatVal = 0.0f;

    // --- M1 parameters ---
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
            loadSample(filePath);
        }
    }
    else if (pathLen == 0)
    {
        loadedFilePath_.clear();
    }

    // --- M2 parameters (FR-027) ---
    if (streamer.readFloat(floatVal))
        harmonicLevel_.store(std::clamp(floatVal, 0.0f, 2.0f) / 2.0f);

    if (streamer.readFloat(floatVal))
        residualLevel_.store(std::clamp(floatVal, 0.0f, 2.0f) / 2.0f);

    if (streamer.readFloat(floatVal))
        residualBrightness_.store((std::clamp(floatVal, -1.0f, 1.0f) + 1.0f) / 2.0f);

    if (streamer.readFloat(floatVal))
        transientEmphasis_.store(std::clamp(floatVal, 0.0f, 2.0f) / 2.0f);

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

        auto* analysis = currentAnalysis_.load(std::memory_order_acquire);
        auto* newAnalysis = analysis
            ? new SampleAnalysis(*analysis)
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

            if (!streamer.readFloat(floatVal))
            {
                readOk = false;
                break;
            }
            frame.totalEnergy = std::max(floatVal, 0.0f);

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
            if (analysisFFTSize > 0 && analysisHopSize > 0)
            {
                residualSynth_.prepare(
                    analysisFFTSize, analysisHopSize,
                    static_cast<float>(sampleRate_));
            }

            auto* old = currentAnalysis_.exchange(
                newAnalysis, std::memory_order_acq_rel);
            pendingDeletion_ = old;
        }
        else
        {
            delete newAnalysis;
        }
    }

    // --- M3 parameters (sidechain) ---
    Steinberg::int32 inputSourceInt = 0;
    Steinberg::int32 latencyModeInt = 0;
    if (streamer.readInt32(inputSourceInt))
        inputSource_.store(inputSourceInt > 0 ? 1.0f : 0.0f);
    if (streamer.readInt32(latencyModeInt))
    {
        latencyMode_.store(latencyModeInt > 0 ? 1.0f : 0.0f);
        auto restoredMode = latencyModeInt > 0
            ? LatencyMode::HighPrecision
            : LatencyMode::LowLatency;
        liveAnalysis_.setLatencyMode(restoredMode);
    }

    // --- M4 parameters (musical control) ---
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

    // --- M5 parameters (harmonic memory) ---
    Steinberg::int32 selectedSlot = 0;
    if (streamer.readInt32(selectedSlot))
    {
        selectedSlot = std::clamp(selectedSlot, static_cast<Steinberg::int32>(0),
                                 static_cast<Steinberg::int32>(7));
        memorySlot_.store(static_cast<float>(selectedSlot) / 7.0f,
                         std::memory_order_relaxed);
    }

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

    // --- M6 parameters (creative extensions) ---
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

    // --- Harmonic Physics parameters ---
    if (streamer.readFloat(floatVal))
        warmth_.store(std::clamp(floatVal, 0.0f, 1.0f));
    if (streamer.readFloat(floatVal))
        coupling_.store(std::clamp(floatVal, 0.0f, 1.0f));
    if (streamer.readFloat(floatVal))
        stability_.store(std::clamp(floatVal, 0.0f, 1.0f));
    if (streamer.readFloat(floatVal))
        entropy_.store(std::clamp(floatVal, 0.0f, 1.0f));

    // --- Analysis Feedback Loop parameters ---
    if (streamer.readFloat(floatVal))
        feedbackAmount_.store(std::clamp(floatVal, 0.0f, 1.0f));
    if (streamer.readFloat(floatVal))
        feedbackDecay_.store(std::clamp(floatVal, 0.0f, 1.0f));

    // --- ADSR Envelope Detection parameters ---
    if (streamer.readFloat(floatVal))
        adsrAttackMs_.store(std::clamp(floatVal, 1.0f, 5000.0f));
    if (streamer.readFloat(floatVal))
        adsrDecayMs_.store(std::clamp(floatVal, 1.0f, 5000.0f));
    if (streamer.readFloat(floatVal))
        adsrSustainLevel_.store(std::clamp(floatVal, 0.0f, 1.0f));
    if (streamer.readFloat(floatVal))
        adsrReleaseMs_.store(std::clamp(floatVal, 1.0f, 5000.0f));
    if (streamer.readFloat(floatVal))
        adsrAmount_.store(std::clamp(floatVal, 0.0f, 1.0f));
    if (streamer.readFloat(floatVal))
        adsrTimeScale_.store(std::clamp(floatVal, 0.25f, 4.0f));
    if (streamer.readFloat(floatVal))
        adsrAttackCurve_.store(std::clamp(floatVal, -1.0f, 1.0f));
    if (streamer.readFloat(floatVal))
        adsrDecayCurve_.store(std::clamp(floatVal, -1.0f, 1.0f));
    if (streamer.readFloat(floatVal))
        adsrReleaseCurve_.store(std::clamp(floatVal, -1.0f, 1.0f));

    // Per-slot ADSR data
    for (int i = 0; i < 8; ++i)
    {
        auto& slot = memorySlots_[static_cast<size_t>(i)];
        if (streamer.readFloat(floatVal))
            slot.adsrAttackMs = std::clamp(floatVal, 1.0f, 5000.0f);
        if (streamer.readFloat(floatVal))
            slot.adsrDecayMs = std::clamp(floatVal, 1.0f, 5000.0f);
        if (streamer.readFloat(floatVal))
            slot.adsrSustainLevel = std::clamp(floatVal, 0.0f, 1.0f);
        if (streamer.readFloat(floatVal))
            slot.adsrReleaseMs = std::clamp(floatVal, 1.0f, 5000.0f);
        if (streamer.readFloat(floatVal))
            slot.adsrAmount = std::clamp(floatVal, 0.0f, 1.0f);
        if (streamer.readFloat(floatVal))
            slot.adsrTimeScale = std::clamp(floatVal, 0.25f, 4.0f);
        if (streamer.readFloat(floatVal))
            slot.adsrAttackCurve = std::clamp(floatVal, -1.0f, 1.0f);
        if (streamer.readFloat(floatVal))
            slot.adsrDecayCurve = std::clamp(floatVal, -1.0f, 1.0f);
        if (streamer.readFloat(floatVal))
            slot.adsrReleaseCurve = std::clamp(floatVal, -1.0f, 1.0f);
    }

    // --- Partial Count parameter ---
    if (streamer.readFloat(floatVal))
        partialCount_.store(std::clamp(floatVal, 0.0f, 1.0f));

    // --- Modulator Tempo Sync parameters (graceful fallback for old states) ---
    if (streamer.readFloat(floatVal))
        mod1RateSync_.store(floatVal > 0.5f ? 1.0f : 0.0f);
    if (streamer.readFloat(floatVal))
        mod1NoteValue_.store(std::clamp(floatVal, 0.0f, 1.0f));
    if (streamer.readFloat(floatVal))
        mod2RateSync_.store(floatVal > 0.5f ? 1.0f : 0.0f);
    if (streamer.readFloat(floatVal))
        mod2NoteValue_.store(std::clamp(floatVal, 0.0f, 1.0f));

    return Steinberg::kResultOk;
}

} // namespace Innexus
