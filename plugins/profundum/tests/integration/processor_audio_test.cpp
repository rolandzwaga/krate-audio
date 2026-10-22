// ==============================================================================
// Profundum_ProcessorRendersHeldNote (Phase 10, plan 4.1 / 4.3)
// ==============================================================================
// T014: HeldNote (SC-005) and Ceiling (SC-006). T017: WrapperTransparency
// (SC-008).
//
// No bit-exact float golden (FR-065): level bars, the limiter's own ceiling
// expression and explicit counts only.
// ==============================================================================

#include "profundum_test_fixture.h"

#include "plugin_ids.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

using namespace ProfundumPluginTest;

namespace {

constexpr double kFs = 48000.0;
constexpr std::size_t kBlock = 512;
constexpr float kVelocity = 100.0f / 127.0f;

using Scripted = ProcessorFixture::ScriptedEvent;

Scripted noteOnAt(Steinberg::int64 sample, Steinberg::int16 pitch) {
    Scripted s;
    s.sample = sample;
    s.kind = Scripted::Kind::NoteOn;
    s.pitch = pitch;
    s.velocity = kVelocity;
    return s;
}

Scripted noteOffAt(Steinberg::int64 sample, Steinberg::int16 pitch) {
    Scripted s;
    s.sample = sample;
    s.kind = Scripted::Kind::NoteOff;
    s.pitch = pitch;
    return s;
}

Scripted paramAt(Steinberg::int64 sample, Steinberg::Vst::ParamID id, double value) {
    Scripted s;
    s.sample = sample;
    s.kind = Scripted::Kind::Param;
    s.id = id;
    s.value = value;
    return s;
}

std::size_t nonFiniteCount(std::span<const float> x) noexcept {
    std::size_t n = 0;
    for (const float v : x) {
        if (!isFiniteBits(v)) {
            ++n;
        }
    }
    return n;
}

}  // namespace

TEST_CASE("Profundum_ProcessorRendersHeldNote", "[profundum][processor][audio]") {
    SECTION("HeldNote") {  // SC-005
        ProcessorFixture f;
        REQUIRE(f.prepare(kFs) == Steinberg::kResultOk);

        constexpr std::size_t kBlocks = 188;  // 2 s at 512
        constexpr std::size_t kTotal = kBlocks * kBlock;
        const std::vector<Scripted> script{noteOnAt(0, kE1)};
        f.renderScript(script, kTotal, kBlock);
        REQUIRE(f.capturedL.size() == kTotal);
        REQUIRE(f.capturedR.size() == kTotal);

        const double rmsL = rmsDb(f.capturedL, 24000, 96000);
        const double rmsR = rmsDb(f.capturedR, 24000, 96000);
        const float peak = std::max(peakOf(f.capturedL), peakOf(f.capturedR));
        WARN("SC-005 HeldNote: peak = " << peak << ", RMS [0.5 s, 2 s) L = " << rmsL << " dBFS, R = " << rmsR
                                        << " dBFS");
        REQUIRE(nonFiniteCount(f.capturedL) == 0u);
        REQUIRE(nonFiniteCount(f.capturedR) == 0u);
        REQUIRE(rmsL >= -40.0);
        REQUIRE(rmsR >= -40.0);
    }

    SECTION("Ceiling") {  // SC-006
        ProcessorFixture f;
        REQUIRE(f.prepare(kFs) == Steinberg::kResultOk);

        constexpr std::size_t kLoudBlocks = 375;   // 4 s at 512
        constexpr std::size_t kLowBlocks = 188;    // a further 2 s
        constexpr std::size_t kSwitchBlock = kLoudBlocks;
        constexpr std::size_t kSwitchSample = kSwitchBlock * kBlock;  // a block start
        constexpr std::size_t kTotal = (kLoudBlocks + kLowBlocks) * kBlock;

        std::vector<Scripted> script;
        script.push_back(paramAt(0, ::Profundum::kVoiceModeId, 1.0));
        script.push_back(paramAt(0, ::Profundum::kPolyphonyId, 1.0));
        script.push_back(paramAt(0, ::Profundum::kMacroWeightId, 1.0));
        script.push_back(paramAt(0, ::Profundum::kMacroDarkBrightId, 1.0));
        script.push_back(paramAt(0, ::Profundum::kMacroTightLooseId, 1.0));
        script.push_back(paramAt(0, ::Profundum::kMacroCleanViolentId, 1.0));
        script.push_back(paramAt(0, ::Profundum::kOutputGainId, 1.0));
        for (const Steinberg::int16 pitch : kChord8) {
            script.push_back(noteOnAt(0, pitch));
        }
        script.push_back(
            paramAt(static_cast<Steinberg::int64>(kSwitchSample), ::Profundum::kOutputCeilingId, 0.0));

        f.renderScript(script, kTotal, kBlock);
        REQUIRE(f.capturedL.size() == kTotal);
        REQUIRE(f.capturedR.size() == kTotal);

        // The limiter's own expression (true_peak_limiter.h:136), so `<=` needs no tolerance.
        const float ceilMinus1Db = std::pow(10.0f, -1.0f * 0.05f);
        const float ceilMinus1Db2 = std::pow(10.0f, -12.0f * 0.05f);

        const std::span<const float> allL{f.capturedL};
        const std::span<const float> allR{f.capturedR};
        const float peakL = peakOf(allL.first(kSwitchSample));
        const float peakR = peakOf(allR.first(kSwitchSample));
        const std::size_t lowFrom = kSwitchSample + 64;
        const float lowPeakL = peakOf(allL.subspan(lowFrom));
        const float lowPeakR = peakOf(allR.subspan(lowFrom));
        WARN("SC-006 Ceiling: 4 s peak L = " << peakL << ", R = " << peakR << " (bar " << ceilMinus1Db
                                             << ", non-vacuity >= " << 0.5f * ceilMinus1Db << "); after the -12 dBTP switch"
                                             << " (from sample " << lowFrom << ") peak L = " << lowPeakL
                                             << ", R = " << lowPeakR << " (bar " << ceilMinus1Db2 << ")");

        REQUIRE(nonFiniteCount(allL) == 0u);
        REQUIRE(nonFiniteCount(allR) == 0u);
        REQUIRE(peakL <= ceilMinus1Db);
        REQUIRE(peakR <= ceilMinus1Db);
        // Non-vacuity: the chord must drive the limiter; make the script louder if not, never move the bar.
        REQUIRE(std::max(peakL, peakR) >= 0.5f * ceilMinus1Db);
        REQUIRE(lowPeakL <= ceilMinus1Db2);
        REQUIRE(lowPeakR <= ceilMinus1Db2);
    }

    SECTION("WrapperTransparency") {  // SC-008, plan 4.3: the wrapper adds only the gain
        constexpr std::size_t kTotal = 144000;             // 3 s at 48 kHz
        constexpr std::size_t kModeSample = 100 * kBlock;  // 51 200, a block start
        constexpr std::array<Steinberg::int64, 4> kChordOn{60011, 60013, 61007, 70001};
        constexpr Steinberg::int64 kChordOff = 120019;
        // quantiseVelocity(100 / 127) in processor.cpp: 100.
        static constexpr std::uint8_t kMidiVelocity = 100;

        // Arm (a): the processor at block 512, registered defaults.
        std::vector<Scripted> script;
        script.push_back(noteOnAt(1000, 28));
        script.push_back(noteOnAt(9001, 33));
        script.push_back(noteOffAt(20003, 28));
        script.push_back(noteOffAt(40017, 33));
        script.push_back(paramAt(static_cast<Steinberg::int64>(kModeSample), ::Profundum::kVoiceModeId, 1.0));
        for (std::size_t i = 0; i < kChord4.size(); ++i) {
            script.push_back(noteOnAt(kChordOn[i], kChord4[i]));
        }
        for (const Steinberg::int16 pitch : kChord4) {
            script.push_back(noteOffAt(kChordOff, pitch));
        }

        ProcessorFixture f;
        REQUIRE(f.prepare(kFs) == Steinberg::kResultOk);
        f.renderScript(script, kTotal, kBlock);
        REQUIRE(f.capturedL.size() == kTotal);
        REQUIRE(f.capturedR.size() == kTotal);

        // Arm (b): a bare engine, the same notes at the same samples, and
        // setVoiceMode(Poly) right before sample 51 200 - no other setter.
        using Engine = Krate::DSP::ProfundumEngine;
        const auto on = [](std::size_t at, Steinberg::int16 pitch) {
            DirectEvent e;
            e.sample = at;
            e.kind = DirectEvent::Kind::NoteOn;
            e.note = static_cast<std::uint8_t>(pitch);
            e.velocity = kMidiVelocity;
            return e;
        };
        const auto off = [](std::size_t at, Steinberg::int16 pitch) {
            DirectEvent e;
            e.sample = at;
            e.kind = DirectEvent::Kind::NoteOff;
            e.note = static_cast<std::uint8_t>(pitch);
            return e;
        };
        std::vector<DirectEvent> direct;
        direct.push_back(on(1000, 28));
        direct.push_back(on(9001, 33));
        direct.push_back(off(20003, 28));
        direct.push_back(off(40017, 33));
        DirectEvent mode;
        mode.sample = kModeSample;
        mode.kind = DirectEvent::Kind::VoiceMode;
        mode.mode = Engine::VoiceMode::Poly;
        direct.push_back(mode);
        for (std::size_t i = 0; i < kChord4.size(); ++i) {
            direct.push_back(on(static_cast<std::size_t>(kChordOn[i]), kChord4[i]));
        }
        for (const Steinberg::int16 pitch : kChord4) {
            direct.push_back(off(static_cast<std::size_t>(kChordOff), pitch));
        }

        DirectEngine d(kFs);
        d.render(direct, kTotal);
        REQUIRE(d.outL.size() == kTotal);
        REQUIRE(d.outR.size() == kTotal);

        const double rmsA = rmsDb(f.capturedL, 0, kTotal);
        const std::size_t misL = mismatches(f.capturedL, d.outL);
        const std::size_t misR = mismatches(f.capturedR, d.outR);
        WARN("SC-008 WrapperTransparency: arm (a) RMS L [0, 3 s) = "
             << rmsA << " dBFS; mismatches L = " << misL << ", R = " << misR
             << "; max |a - b| L = " << maxAbsDiff(f.capturedL, d.outL)
             << ", R = " << maxAbsDiff(f.capturedR, d.outR));
        // Precondition: a non-vacuous signal.
        REQUIRE(rmsA >= -40.0);
        REQUIRE(misL == 0u);
        REQUIRE(misR == 0u);
    }
}
