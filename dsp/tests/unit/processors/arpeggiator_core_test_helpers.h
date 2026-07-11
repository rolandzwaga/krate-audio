// arpeggiator_core_test_helpers.h
// Shared fixtures/helpers for the split arpeggiator_core_test_*.cpp files (D1).
// Auto-extracted from the former monolith. Helpers are 'inline' (not 'static') so
// a sibling that includes this header but doesn't use one won't trip C4505 under /W4.
#pragma once

#include <krate/dsp/processors/arpeggiator_core.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <utility>
#include <vector>
using namespace Krate::DSP;

inline std::vector<ArpEvent> collectEvents(ArpeggiatorCore& arp, BlockContext& ctx,
                                           size_t numBlocks) {
    std::vector<ArpEvent> allEvents;
    std::array<ArpEvent, 128> blockEvents;
    for (size_t b = 0; b < numBlocks; ++b) {
        size_t count = arp.processBlock(ctx, blockEvents);
        for (size_t i = 0; i < count; ++i) {
            ArpEvent evt = blockEvents[i];
            evt.sampleOffset += static_cast<int32_t>(b * ctx.blockSize);
            allEvents.push_back(evt);
        }
        ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
    }
    return allEvents;
}

inline std::vector<ArpEvent> filterNoteOns(const std::vector<ArpEvent>& events) {
    std::vector<ArpEvent> noteOns;
    for (const auto& e : events) {
        if (e.type == ArpEvent::Type::NoteOn) {
            noteOns.push_back(e);
        }
    }
    return noteOns;
}

inline std::vector<ArpEvent> filterNoteOffs(const std::vector<ArpEvent>& events) {
    std::vector<ArpEvent> noteOffs;
    for (const auto& e : events) {
        if (e.type == ArpEvent::Type::NoteOff) {
            noteOffs.push_back(e);
        }
    }
    return noteOffs;
}

inline size_t generateAndWriteBaseline(double bpm, const std::string& filePath,
                                        size_t minSteps = 1050) {
    ArpeggiatorCore arp;
    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 512;

    arp.prepare(kSampleRate, kBlockSize);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.setSwing(0.0f);
    arp.noteOn(60, 100);  // C4

    BlockContext ctx;
    ctx.sampleRate = kSampleRate;
    ctx.blockSize = kBlockSize;
    ctx.tempoBPM = bpm;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // Collect NoteOn events across many blocks
    std::vector<ArpEvent> noteOnEvents;
    std::array<ArpEvent, 128> blockEvents;

    // Calculate how many blocks we need for minSteps NoteOn events.
    // At 120 BPM, 1/8 note = 11025 samples. 1050 steps = ~11.6M samples.
    // At 512 block size = ~22600 blocks. Use generous margin.
    const size_t maxBlocks = 50000;

    for (size_t b = 0; b < maxBlocks && noteOnEvents.size() < minSteps; ++b) {
        size_t count = arp.processBlock(ctx, blockEvents);
        for (size_t i = 0; i < count; ++i) {
            if (blockEvents[i].type == ArpEvent::Type::NoteOn) {
                ArpEvent evt = blockEvents[i];
                // Adjust sampleOffset to absolute position
                evt.sampleOffset += static_cast<int32_t>(b * kBlockSize);
                noteOnEvents.push_back(evt);
            }
        }
        ctx.transportPositionSamples += static_cast<int64_t>(kBlockSize);
    }

    // Write binary file
    std::ofstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        return 0;
    }

    for (const auto& evt : noteOnEvents) {
        file.write(reinterpret_cast<const char*>(&evt.note), sizeof(uint8_t));
        file.write(reinterpret_cast<const char*>(&evt.velocity), sizeof(uint8_t));
        file.write(reinterpret_cast<const char*>(&evt.sampleOffset), sizeof(int32_t));
    }

    file.close();
    return noteOnEvents.size();
}

inline std::vector<ArpEvent> readBaselineFixture(const std::string& filePath) {
    std::vector<ArpEvent> events;
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        return events;
    }

    while (file.good()) {
        uint8_t note = 0;
        uint8_t velocity = 0;
        int32_t sampleOffset = 0;

        file.read(reinterpret_cast<char*>(&note), sizeof(uint8_t));
        file.read(reinterpret_cast<char*>(&velocity), sizeof(uint8_t));
        file.read(reinterpret_cast<char*>(&sampleOffset), sizeof(int32_t));

        if (file.good()) {
            ArpEvent evt;
            evt.type = ArpEvent::Type::NoteOn;
            evt.note = note;
            evt.velocity = velocity;
            evt.sampleOffset = sampleOffset;
            events.push_back(evt);
        }
    }

    return events;
}

inline std::vector<ArpEvent> collectEventsForSamples(
    ArpeggiatorCore& arp, BlockContext& ctx, size_t totalSamples) {
    std::vector<ArpEvent> allEvents;
    std::array<ArpEvent, 128> blockEvents;
    size_t samplesProcessed = 0;
    while (samplesProcessed < totalSamples) {
        size_t remaining = totalSamples - samplesProcessed;
        size_t thisBlock = std::min(remaining, ctx.blockSize);
        size_t savedBlockSize = ctx.blockSize;
        ctx.blockSize = thisBlock;
        size_t count = arp.processBlock(ctx, blockEvents);
        for (size_t i = 0; i < count; ++i) {
            ArpEvent evt = blockEvents[i];
            evt.sampleOffset += static_cast<int32_t>(samplesProcessed);
            allEvents.push_back(evt);
        }
        ctx.transportPositionSamples += static_cast<int64_t>(thisBlock);
        samplesProcessed += thisBlock;
        ctx.blockSize = savedBlockSize;
    }
    return allEvents;
}

inline std::vector<int32_t> findRatchetStepOffsets(
    const std::vector<ArpEvent>& events, size_t stepDuration) {
    auto noteOns = filterNoteOns(events);
    std::vector<int32_t> ratchetOffsets;
    size_t i = 0;
    while (i < noteOns.size()) {
        // Count NoteOns within a step-duration window
        int32_t windowStart = noteOns[i].sampleOffset;
        size_t count = 1;
        size_t j = i + 1;
        while (j < noteOns.size() &&
               noteOns[j].sampleOffset < windowStart + static_cast<int32_t>(stepDuration)) {
            ++count;
            ++j;
        }
        if (count > 1) {
            ratchetOffsets.push_back(windowStart);
        }
        i = j;  // skip past this window
    }
    return ratchetOffsets;
}

inline std::vector<ArpEvent> processBarWithTransport(
    ArpeggiatorCore& arp, BlockContext& ctx, size_t totalSamples)
{
    std::vector<ArpEvent> allEvents;
    size_t samplesProcessed = 0;
    while (samplesProcessed < totalSamples) {
        size_t blockSamples = std::min(ctx.blockSize, totalSamples - samplesProcessed);
        ctx.blockSize = blockSamples;
        std::array<ArpEvent, 128> events;
        size_t count = arp.processBlock(ctx, events);
        for (size_t i = 0; i < count; ++i) {
            ArpEvent e = events[i];
            e.sampleOffset += static_cast<int32_t>(samplesProcessed);
            allEvents.push_back(e);
        }
        samplesProcessed += blockSamples;
        ctx.transportPositionSamples += static_cast<int64_t>(blockSamples);
        ctx.projectTimeMusic +=
            static_cast<double>(blockSamples) / ctx.sampleRate * (ctx.tempoBPM / 60.0);
    }
    ctx.blockSize = 512;  // restore default
    return allEvents;
}

inline BlockContext makeLaneSpeedTestContext() {
    BlockContext ctx{};
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    return ctx;
}

