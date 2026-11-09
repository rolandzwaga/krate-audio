// =============================================================================
// Controller <-> processor state codec (fix plan T1.17)
// =============================================================================
// The controller writes user presets (createComponentStateStream), reads
// processor streams (setComponentState / parseComponentState) and maps
// crossover and sweep-frequency normalized values to Hz for the host and the
// dividers. These cases pin all of that against the processor, which is the
// reference: D-085, D-117, D-053, D-052, D-086, D-051, D-101 (frequency half).
//
// The reference mappings below are written out here on purpose (double
// precision, straight from the processor's documented formulas), so the cases
// do not test the codec against itself.
// =============================================================================

#include "../../test_support/disrumpo_rig.h"

#include "controller/controller.h"
#include "controller/views/spectrum_display.h"
#include "plugin_ids.h"
#include "state_codec.h"

#include "midi/midi_cc_manager.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"
#include "vstgui/uidescription/uiattributes.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace Disrumpo;
using namespace Disrumpo::TestSupport;

namespace {

// Processor stream: version, 3 globals, band count, per band gain/pan/solo/bypass/mute, then crossovers.
constexpr size_t kCrossoverOffset = 4 + 3 * 4 + 4 + static_cast<size_t>(kMaxBands) * (4 + 4 + 3);

double refCrossoverHzToNorm(double hz) {
    return (std::log10(hz) - std::log10(20.0)) / (std::log10(20000.0) - std::log10(20.0));
}
double refCrossoverNormToHz(double norm) {
    return std::pow(10.0, std::log10(20.0) + norm * (std::log10(20000.0) - std::log10(20.0)));
}
double refSweepHzToNorm(double hz) {
    return (std::log2(hz) - std::log2(20.0)) / (std::log2(20000.0) - std::log2(20.0));
}
double refSweepNormToHz(double norm) {
    return std::pow(2.0, std::log2(20.0) + norm * (std::log2(20000.0) - std::log2(20.0)));
}

std::vector<uint8_t> drain(Steinberg::IBStream* s) {
    Steinberg::int64 size = 0;
    s->seek(0, Steinberg::IBStream::kIBSeekEnd, &size);
    s->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    std::vector<uint8_t> out(static_cast<size_t>(size));
    Steinberg::int32 got = 0;
    if (size > 0) s->read(out.data(), static_cast<Steinberg::int32>(size), &got);
    return out;
}

/// A MemoryStream holding a copy of `bytes`, positioned at 0. Caller releases.
Steinberg::MemoryStream* streamOf(std::vector<uint8_t> bytes) {
    auto* s = new Steinberg::MemoryStream();
    Steinberg::int32 written = 0;
    s->write(bytes.data(), static_cast<Steinberg::int32>(bytes.size()), &written);
    s->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    return s;
}

float readFloatAt(const std::vector<uint8_t>& b, size_t off) {
    float v = 0.0f;
    std::memcpy(&v, b.data() + off, sizeof v);
    return v;
}

void writeFloatAt(std::vector<uint8_t>& b, size_t off, float v) { std::memcpy(b.data() + off, &v, sizeof v); }

double parseString128(const Steinberg::Vst::String128 s) {
    std::string narrow;
    for (int i = 0; i < 128 && s[i] != 0; ++i) narrow.push_back(static_cast<char>(s[i]));
    return std::strtod(narrow.c_str(), nullptr);
}

// Component chunk ("Comp") of a .vstpreset file.
std::vector<uint8_t> readPresetComponent(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    REQUIRE(f.good());
    const std::vector<uint8_t> file((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    REQUIRE(file.size() > size_t{48});
    REQUIRE(std::memcmp(file.data(), "VST3", 4) == 0);
    int64_t listOffset = 0;
    std::memcpy(&listOffset, file.data() + 40, sizeof listOffset);
    const auto lo = static_cast<size_t>(listOffset);
    REQUIRE(std::memcmp(file.data() + lo, "List", 4) == 0);
    uint32_t entries = 0;
    std::memcpy(&entries, file.data() + lo + 4, sizeof entries);
    for (uint32_t i = 0; i < entries; ++i) {
        const size_t e = lo + 8 + static_cast<size_t>(i) * 20;
        if (std::memcmp(file.data() + e, "Comp", 4) == 0) {
            int64_t off = 0;
            int64_t size = 0;
            std::memcpy(&off, file.data() + e + 4, sizeof off);
            std::memcpy(&size, file.data() + e + 12, sizeof size);
            REQUIRE(static_cast<size_t>(off + size) <= file.size());
            return {file.begin() + static_cast<std::ptrdiff_t>(off),
                    file.begin() + static_cast<std::ptrdiff_t>(off + size)};
        }
    }
    FAIL("no Comp chunk in " << path);
    return {};
}

constexpr Steinberg::Vst::ParamID kSourceId = makeRoutingParamId(0, 0);
constexpr Steinberg::Vst::ParamID kNodesId = makeBandParamId(0, BandParamType::kBandActiveNodes);
constexpr Steinberg::Vst::ParamID kModeId = makeBandParamId(0, BandParamType::kBandMorphMode);
constexpr Steinberg::Vst::ParamID kSmoothingId = makeBandParamId(0, BandParamType::kBandMorphSmoothing);
constexpr Steinberg::Vst::ParamID kCrossover0Id = makeCrossoverParamId(0);
constexpr Steinberg::Vst::ParamID kSweepFreqId = makeSweepParamId(SweepParamType::kSweepFrequency);

}  // namespace

TEST_CASE("Controller preset stream round-trips through the processor", "[disrumpo][state][D-117][D-085]") {
    Controller c;
    REQUIRE(c.initialize(nullptr) == Steinberg::kResultOk);
    c.setParamNormalized(kSourceId, 1.0);           // index 13 of 14: Transient
    c.setParamNormalized(kNodesId, 0.0);           // 1 node
    c.setParamNormalized(kModeId, 1.0);            // mode 2 (2D Radial)
    c.setParamNormalized(kSmoothingId, 0.2);       // 100 ms
    c.setParamNormalized(kCrossover0Id, refCrossoverHzToNorm(1000.0));

    auto* presetStream = c.createComponentStateStream();
    REQUIRE(presetStream != nullptr);
    const auto preset = drain(presetStream);
    presetStream->release();

    std::vector<uint8_t> session;
    {
        Rig rig(44100.0);
        REQUIRE(setStateBytes(*rig.p, preset));
        session = getStateBytes(*rig.p);
    }
    REQUIRE(session.size() > kCrossoverOffset + 4);
    const double processorCrossoverHz = readFloatAt(session, kCrossoverOffset);

    Controller c2;
    REQUIRE(c2.initialize(nullptr) == Steinberg::kResultOk);
    auto* sessionStream = streamOf(session);
    REQUIRE(c2.setComponentState(sessionStream) == Steinberg::kResultOk);
    sessionStream->release();

    const auto source = std::lround(c2.getParamNormalized(kSourceId) * 13.0);
    const auto nodes = std::lround(c2.getParamNormalized(kNodesId) * 3.0) + 1;
    const auto mode = std::lround(c2.getParamNormalized(kModeId) * 2.0);
    const double smoothingMs = c2.getParamNormalized(kSmoothingId) * 500.0;
    const double crossoverHz = refCrossoverNormToHz(c2.getParamNormalized(kCrossover0Id));
    std::printf("T1.17 round trip: source %ld, nodes %ld, mode %ld, smoothing %.3f ms, crossover processor %.2f Hz, "
                "controller %.2f Hz\n",
                source, nodes, mode, smoothingMs, processorCrossoverHz, crossoverHz);

    CHECK(source == 13);
    CHECK(nodes == 1);
    CHECK(mode == 2);
    CHECK(std::fabs(smoothingMs - 100.0) < 0.5);  // continuous parameter: 0.5 ms tolerance
    CHECK(std::fabs(processorCrossoverHz - 1000.0) < 1.0);
    CHECK(std::fabs(crossoverHz - 1000.0) < 1.0);

    c2.terminate();
    c.terminate();
}

TEST_CASE("Crossover normalized<->Hz mapping is log10 everywhere", "[disrumpo][state][D-086][D-051]") {
    Rig rig(48000.0);
    Controller c;
    REQUIRE(c.initialize(nullptr) == Steinberg::kResultOk);

    SECTION("parseComponentState of a 1000 Hz crossover gives the processor's log norm") {
        auto bytes = getStateBytes(*rig.p);
        REQUIRE(bytes.size() > kCrossoverOffset + 4);
        writeFloatAt(bytes, kCrossoverOffset, 1000.0f);
        auto* s = streamOf(bytes);
        REQUIRE(c.setComponentState(s) == Steinberg::kResultOk);
        s->release();
        const double norm = c.getParamNormalized(kCrossover0Id);
        std::printf("T1.17 parse 1000 Hz -> norm %.5f (want %.5f)\n", norm, refCrossoverHzToNorm(1000.0));
        CHECK(std::fabs(norm - refCrossoverHzToNorm(1000.0)) < 1e-3);
    }

    SECTION("Codec, processor and controller agree at 20/100/1000/10000/20000 Hz") {
        auto* xParam = c.getParameterObject(kCrossover0Id);
        auto* sParam = c.getParameterObject(kSweepFreqId);
        REQUIRE(xParam != nullptr);
        REQUIRE(sParam != nullptr);
        for (const double hz : {20.0, 100.0, 1000.0, 10000.0, 20000.0}) {
            const double xNorm = refCrossoverHzToNorm(hz);
            const double sNorm = refSweepHzToNorm(hz);

            rig.apply({{kCrossover0Id, xNorm}, {kSweepFreqId, sNorm}});
            const double procXHz = readFloatAt(getStateBytes(*rig.p), kCrossoverOffset);
            const double procSHz = rig.p->getSweepProcessor().getTargetFrequency();

            const double ctlXNorm = xParam->toNormalized(hz);
            const double ctlXHz = xParam->toPlain(xNorm);
            const double ctlSNorm = sParam->toNormalized(hz);
            const double ctlSHz = sParam->toPlain(sNorm);

            const double codecXNorm = crossoverHzToNorm(static_cast<float>(hz));
            const double codecXHz = crossoverNormToHz(static_cast<float>(xNorm));
            const double codecSNorm = sweepFreqHzToNorm(static_cast<float>(hz));
            const double codecSHz = sweepFreqNormToHz(static_cast<float>(sNorm));

            std::printf("T1.17 %7.0f Hz: crossover norm ref %.5f ctl %.5f codec %.5f | Hz proc %.3f ctl %.3f codec %.3f"
                        " || sweep norm ref %.5f ctl %.5f codec %.5f | Hz proc %.3f ctl %.3f codec %.3f\n",
                        hz, xNorm, ctlXNorm, codecXNorm, procXHz, ctlXHz, codecXHz, sNorm, ctlSNorm, codecSNorm,
                        procSHz, ctlSHz, codecSHz);
            CHECK(std::fabs(ctlXNorm - xNorm) < 1e-3);
            CHECK(std::fabs(codecXNorm - xNorm) < 1e-3);
            CHECK(std::fabs(procXHz - hz) < 1.0);
            CHECK(std::fabs(ctlXHz - hz) < 1.0);
            CHECK(std::fabs(codecXHz - hz) < 1.0);
            CHECK(std::fabs(ctlSNorm - sNorm) < 1e-3);
            CHECK(std::fabs(codecSNorm - sNorm) < 1e-3);
            CHECK(std::fabs(procSHz - hz) < 1.0);
            CHECK(std::fabs(ctlSHz - hz) < 1.0);
            CHECK(std::fabs(codecSHz - hz) < 1.0);
        }
        // Range ends are exact (fast-math safe: a branch, not cancelling arithmetic).
        CHECK(crossoverNormToHz(0.0f) == 20.0f);
        CHECK(crossoverNormToHz(1.0f) == 20000.0f);
        CHECK(sweepFreqNormToHz(0.0f) == 20.0f);
        CHECK(sweepFreqNormToHz(1.0f) == 20000.0f);
        CHECK(crossoverHzToNorm(20.0f) == 0.0f);
        CHECK(crossoverHzToNorm(20000.0f) == 1.0f);
        CHECK(sweepFreqHzToNorm(20.0f) == 0.0f);
        CHECK(sweepFreqHzToNorm(20000.0f) == 1.0f);
    }

    SECTION("createCustomView seeds the spectrum dividers at the Hz the processor runs") {
        c.setParamNormalized(makeGlobalParamId(GlobalParamType::kGlobalBandCount), 1.0);  // 4 bands
        c.setParamNormalized(kCrossover0Id, refCrossoverHzToNorm(1000.0));
        VSTGUI::UIAttributes attributes;
        auto* view = c.createCustomView("SpectrumDisplay", attributes, nullptr, nullptr);
        REQUIRE(view != nullptr);
        auto* display = dynamic_cast<SpectrumDisplay*>(view);
        REQUIRE(display != nullptr);
        const double dividerHz = display->getCrossoverFrequency(0);
        std::printf("T1.17 divider 0 seeded at %.2f Hz (want 1000)\n", dividerHz);
        CHECK(std::fabs(dividerHz - 1000.0) < 1.0);
        view->forget();
    }

    c.terminate();
}

TEST_CASE("Crossover and sweep frequency host text shows the Hz the DSP runs", "[disrumpo][state][D-101]") {
    Rig rig(48000.0);
    Controller c;
    REQUIRE(c.initialize(nullptr) == Steinberg::kResultOk);
    for (const double norm : {0.25, 0.5, 0.75}) {
        rig.apply({{kCrossover0Id, norm}, {kSweepFreqId, norm}});
        const double procXHz = readFloatAt(getStateBytes(*rig.p), kCrossoverOffset);
        const double procSHz = rig.p->getSweepProcessor().getTargetFrequency();

        Steinberg::Vst::String128 xText{};
        Steinberg::Vst::String128 sText{};
        REQUIRE(c.getParamStringByValue(kCrossover0Id, norm, xText) == Steinberg::kResultTrue);
        REQUIRE(c.getParamStringByValue(kSweepFreqId, norm, sText) == Steinberg::kResultTrue);
        const double xShown = parseString128(xText);
        const double sShown = parseString128(sText);
        std::printf("T1.17 text norm %.2f: crossover shows %.2f Hz, DSP %.2f Hz | sweep shows %.2f Hz, DSP %.2f Hz\n",
                    norm, xShown, procXHz, sShown, procSHz);
        CHECK(std::fabs(xShown - procXHz) < 1.0);
        CHECK(std::fabs(sShown - procSHz) < 1.0);
    }
    c.terminate();
}

TEST_CASE("Controller getState/setState round-trips the MIDI CC blob", "[disrumpo][state][D-117]") {
    Krate::Plugins::MidiCCManager source;
    source.addGlobalMapping(74, kSweepFreqId);
    source.addGlobalMapping(1, kCrossover0Id, true);
    auto blob = source.serializeGlobalMappings();
    REQUIRE(!blob.empty());

    auto* in = new Steinberg::MemoryStream();
    {
        Steinberg::IBStreamer w(in, kLittleEndian);
        REQUIRE(w.writeInt32(2));
        REQUIRE(w.writeDouble(0.0));
        REQUIRE(w.writeDouble(0.0));
        REQUIRE(w.writeInt32(static_cast<Steinberg::int32>(blob.size())));
        Steinberg::int32 written = 0;
        in->write(blob.data(), static_cast<Steinberg::int32>(blob.size()), &written);
        in->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    }

    Controller c;
    REQUIRE(c.initialize(nullptr) == Steinberg::kResultOk);
    REQUIRE(c.setState(in) == Steinberg::kResultOk);
    in->release();

    auto* out = new Steinberg::MemoryStream();
    REQUIRE(c.getState(out) == Steinberg::kResultOk);
    const auto saved = drain(out);
    out->release();
    // Version 3 (T1.18): the global blob, then an empty per-preset blob (size 4, count 0).
    REQUIRE(saved.size() == 4 + 8 + 8 + 4 + blob.size() + 4 + 4);
    CHECK(std::memcmp(saved.data() + 24, blob.data(), blob.size()) == 0);

    Controller c2;
    REQUIRE(c2.initialize(nullptr) == Steinberg::kResultOk);
    auto* again = streamOf(saved);
    REQUIRE(c2.setState(again) == Steinberg::kResultOk);
    again->release();

    Steinberg::Vst::ParamID id = 0;
    REQUIRE(c2.getMidiControllerAssignment(0, 0, 74, id) == Steinberg::kResultTrue);
    CHECK(id == kSweepFreqId);
    REQUIRE(c2.getMidiControllerAssignment(0, 0, 1, id) == Steinberg::kResultTrue);
    CHECK(id == kCrossover0Id);

    c2.terminate();
    c.terminate();
}

// T4.01 (D-088, D-091): every modulation, routing and sweep parameter, plus the
// band Morph Smoothing, set through processParameterChanges, saved by getState
// and read back by the controller lands on the normalized value it was set to
// (list parameters on the index the processor rounds to). A range copy in the
// decode that drifts from the one in the state codec breaks this.
// Left out: the custom-curve parameters (the processor does not apply them
// live, follow-up 19) and Sweep MIDI Learn / MIDI CC (controller-only, not in
// the processor state). LFO phase is circular: 360 deg is saved as 0 deg.
TEST_CASE("Mod and sweep params round-trip processParameterChanges -> getState -> controller",
          "[disrumpo][state][D-088][D-091]") {
    Controller ref;
    REQUIRE(ref.initialize(nullptr) == Steinberg::kResultOk);
    std::vector<Steinberg::Vst::ParameterInfo> infos;
    for (Steinberg::int32 i = 0; i < ref.getParameterCount(); ++i) {
        Steinberg::Vst::ParameterInfo info{};
        REQUIRE(ref.getParameterInfo(i, info) == Steinberg::kResultTrue);
        const auto id = info.id;
        const bool smoothing = id == makeBandParamId(0, BandParamType::kBandMorphSmoothing) ||
                               id == makeBandParamId(1, BandParamType::kBandMorphSmoothing) ||
                               id == makeBandParamId(2, BandParamType::kBandMorphSmoothing) ||
                               id == makeBandParamId(3, BandParamType::kBandMorphSmoothing);
        const bool curveParam = id >= makeSweepParamId(SweepParamType::kSweepCustomCurvePointCount) &&
                                id <= makeSweepParamId(SweepParamType::kSweepCustomCurveP7Y);
        const bool controllerOnly = id == makeSweepParamId(SweepParamType::kSweepMidiLearnActive) ||
                                    id == makeSweepParamId(SweepParamType::kSweepMidiCCNumber);
        if ((isModulationParamId(id) || isSweepParamId(id) || smoothing) && !curveParam && !controllerOnly)
            infos.push_back(info);
    }
    ref.terminate();
    REQUIRE(infos.size() > size_t{60});

    int checked = 0;
    int mismatches = 0;
    for (const double v : {0.0, 0.5, 1.0}) {
        Rig rig(48000.0);
        PList changes;
        for (const auto& info : infos) changes.emplace_back(info.id, v);
        rig.apply(changes);
        const auto session = getStateBytes(*rig.p);

        Controller c;
        REQUIRE(c.initialize(nullptr) == Steinberg::kResultOk);
        auto* s = streamOf(session);
        REQUIRE(c.setComponentState(s) == Steinberg::kResultOk);
        s->release();

        for (const auto& info : infos) {
            const double want = info.stepCount > 0
                ? static_cast<double>(std::lround(v * info.stepCount)) / static_cast<double>(info.stepCount)
                : v;
            const double got = c.getParamNormalized(info.id);
            const bool phase = info.id == makeModParamId(ModParamType::kLFO1Phase) ||
                               info.id == makeModParamId(ModParamType::kLFO2Phase);
            const double err = phase ? std::fabs(std::remainder(got - want, 1.0)) : std::fabs(got - want);
            ++checked;
            if (err >= 1e-5) {
                ++mismatches;
                std::string title;
                for (int k = 0; k < 128 && info.title[k] != 0; ++k) title.push_back(static_cast<char>(info.title[k]));
                std::printf("T4.01 0x%04X %s at %.1f: controller reads %.6f, want %.6f\n",
                            static_cast<unsigned>(info.id), title.c_str(), v, got, want);
            }
        }
        c.terminate();
    }
    std::printf("T4.01 round trip: %d of %d (param, norm) pairs off by >= 1e-5\n", mismatches, checked);
    CHECK(mismatches == 0);
}

// Diagnostic for follow-up 15 (not a gate): save -> load -> save byte equality on
// a fresh processor and three factory presets. Prints the differing offsets.
TEST_CASE("Probe: whole-stream save-load-save byte equality", "[.][t117-ulp-probe]") {
    const auto report = [](const char* name, const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
        std::string offsets;
        int diffs = 0;
        for (size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
            if (a[i] == b[i]) continue;
            ++diffs;
            if (diffs <= 24) offsets += " " + std::to_string(i);
        }
        std::printf("T1.17 ULP probe %s: sizes %zu/%zu, %d differing bytes:%s\n", name, a.size(), b.size(), diffs,
                    offsets.c_str());
    };
    {
        Rig rig(44100.0);
        const auto s1 = getStateBytes(*rig.p);
        REQUIRE(setStateBytes(*rig.p, s1));
        const auto s2 = getStateBytes(*rig.p);
        report("fresh", s1, s2);
    }
    for (const char* name : {"Bass/Dirty_Sub.vstpreset", "Drums/Rectified_Drums.vstpreset",
                             "Morph/Radial_Blend.vstpreset"}) {
        Rig rig(44100.0);
        REQUIRE(setStateBytes(*rig.p, readPresetComponent(std::string(DISRUMPO_RESOURCES_DIR "/presets/") + name)));
        const auto s1 = getStateBytes(*rig.p);
        REQUIRE(setStateBytes(*rig.p, s1));
        const auto s2 = getStateBytes(*rig.p);
        report(name, s1, s2);
    }
}
