// Profundum Phase 13 preset real-time safety tests (specs/profundum-phase13-presets-release/tasks.md)
//
// Placeholder created by T007 (Deviation 1). Cases written by T020: Profundum_FactoryPresetLoadRtSafe,
// Profundum_PresetRenderGuard, Profundum_PresetLoadUnderHeldNote (FR-037, FR-046, FR-048; SC-015, SC-027, SC-028).
//
// T020 (plan 5.5 "Per-push guard", 5.10). Every case runs on the test-local preset T (T018's mechanics preset:
// endpoint A = B = auStateAs() with Anchor level 1, Sine, LOCK, Morph Position 0) AND on every allPresets() entry
// (empty before G22), and prints presets=<n>. Per push, never [long].
//   - Profundum_FactoryPresetLoadRtSafe (FR-037, SC-015): prepare at 48 kHz / block 512 holding E1; setState(Comp)
//     outside the measured region; ONE process(512) inside TestHelpers::AllocationScope reads 0 allocations.
//   - Profundum_PresetRenderGuard (FR-046, SC-027): E1, 48 kHz, block 512, note-off at 0.5 s, total 0.75 s; stored
//     and at max Mass (the FR-041 four-block prelude before setupProcessing); 0 non-finite; true peak <= -0.9 dBTP;
//     the case's wall clock printed and <= 15 s (R-11: split by preset index, _0of4 .. _3of4; two shards read 14.34 / 13.79 s alone, 16.02 / 15.68 s under load).
//   - Profundum_PresetLoadUnderHeldNote (FR-048, SC-028): a default-state processor holds E1 from 0; setState(Comp)
//     at L = 12288 between two process() calls; render to L + 100 ms + 4096 + 512; 0 non-finite;
//     step(L, L + 20 ms) <= 2 max(step(L - 4096, L), step(L + 100 ms, L + 100 ms + 4096)); worst ratio printed.
//     Non-vacuity: T's render with a 0.5 full-scale step injected at L + 100 fails the bound.
//
// Recorded choices (T020 notes):
//   - The measured RT block calls Processor::process directly with a stack ProcessData over the fixture's
//     pre-sized channel buffers (no capture insert, no Catch2 assertion inside the scope), as
//     unit/phase11_alloc_test.cpp does. A teeth arm (one vector allocation inside a scope) proves the counter is
//     live in this binary.
//   - Plan 5.6 "Lanes": every per-push SOLID-active preset is checked L == R bitwise from s0 (544 at 48 kHz) on its
//     render-guard render (stored variant).

#include <catch2/catch_test_macros.hpp>

#include "preset_render_support.h"
#include "profundum_preset_host.h"
#include "profundum_test_fixture.h"

#include "plugin_ids.h"

#include <allocation_detector.h>
#include <enable_ftz_daz.h>
#include <low_frequency_metrics.h>

#include <pluginterfaces/vst/ivstaudioprocessor.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace ProfundumPluginTest;

namespace {

namespace RtPd = ::Profundum::PresetDefs;

constexpr double kRtFs = 48000.0;
constexpr std::size_t kRtBlock = 512;
constexpr float kRtTruePeakBarDb = -0.9f;   // FR-041 / FR-046 bar
constexpr double kRtGuardSecondsBar = 15.0; // SC-027 wall-clock bar (R-11)

/// One preset under test: a label and its Comp bytes.
struct RtPreset {
    std::string name;
    std::vector<std::uint8_t> comp;
};

[[nodiscard]] std::vector<std::uint8_t> rtBytesOf(const StateBytes& b) {
    std::vector<std::uint8_t> out(b.size());
    if (!b.empty()) {
        std::memcpy(out.data(), b.data(), b.size());
    }
    return out;
}

[[nodiscard]] StateBytes rtStateOf(std::span<const std::uint8_t> comp) {
    StateBytes out(comp.size());
    if (!comp.empty()) {
        std::memcpy(out.data(), comp.data(), comp.size());
    }
    return out;
}

/// T: T018's mechanics preset. Endpoint A = B = A_s with Anchor level 1, Sine, LOCK, over a fresh processor's
/// stream (every other field the registered default, so the P-6 pins hold; Morph Position 0).
[[nodiscard]] std::vector<std::uint8_t> rtPresetT() {
    Krate::DSP::MorphState a = auRd::auStateAs();
    a.anchor.level = 1.0f;
    a.anchor.waveform = Krate::DSP::AnchorOscillator::Waveform::Sine;
    a.anchor.mode = Krate::DSP::AnchorOscillator::Mode::Lock;
    REQUIRE(Krate::DSP::isValidMorphState(a));
    auto fresh = std::make_unique<ProcessorFixture>();
    std::vector<std::uint8_t> comp = rtBytesOf(withEndpoints(writeV2Stream(*fresh->proc), a, a));
    REQUIRE(comp.size() == ::Profundum::kStateV3Bytes);
    REQUIRE(decodePresetState(comp).ok);
    return comp;
}

/// T first, then every library definition (C-4 build). Prints presets=<n> (the library count).
/// With `shards` > 1, builds only the presets whose index % shards == shard (T is index 0): a shard pays for its own
/// C-4 builds, not for the whole library's (R-11).
[[nodiscard]] std::vector<RtPreset> rtPresetsUnderTest(std::size_t shard = 0, std::size_t shards = 1) {
    const std::vector<RtPd::ProfundumPresetDef>& defs = RtPd::allPresets();
    std::cout << "presets=" << defs.size() << "\n";
    std::vector<RtPreset> out;
    out.reserve(defs.size() / shards + 1u);
    if (shard == 0) {
        out.push_back(RtPreset{.name = "T (test-local mechanics preset)", .comp = rtPresetT()});
    }
    for (std::size_t k = 1; k <= defs.size(); ++k) {
        if (k % shards == shard) {
            out.push_back(RtPreset{.name = std::string(defs[k - 1].name), .comp = presetCompOf(defs[k - 1])});
        }
    }
    return out;
}

[[nodiscard]] float rtVelocity() { return static_cast<float>(auRd::kAuVelocity) / 127.0f; }

/// The FR-041 four parameter-only blocks (plan 5.5), on an initialised, not yet prepared processor.
void rtMaxMassPrelude(ProcessorFixture& fx) {
    namespace P = ::Profundum;
    MultiParamChanges pc;
    pc.reserve(2);
    const auto block = [&fx, &pc](std::initializer_list<std::pair<Steinberg::Vst::ParamID, double>> points) {
        pc.clear();
        for (const auto& p : points) {
            pc.queueFor(p.first).addTestPoint(0, p.second);
        }
        REQUIRE(fx.processNoOutputs(&pc) == Steinberg::kResultOk);
    };
    block({{P::kMorphEditTargetId, 0.0}, {P::kMassId, 1.0}});
    block({{P::kMorphEditTargetId, 1.0}, {P::kMassId, 1.0}});
    block({{P::kMorphEditTargetId, 0.0}});
    block({{P::kModWheelId, 1.0}});
}

[[nodiscard]] std::size_t rtNonFinite(const std::vector<float>& L, const std::vector<float>& R) {
    std::size_t n = 0;
    for (const float x : L) {
        n += presetFloatIsFinite(x) ? 0u : 1u;
    }
    for (const float x : R) {
        n += presetFloatIsFinite(x) ? 0u : 1u;
    }
    return n;
}

// -----------------------------------------------------------------------------------------------------------------
// FR-037: RT-safe load
// -----------------------------------------------------------------------------------------------------------------

struct RtLoadReading {
    std::size_t allocations = 0;
    Steinberg::tresult processResult = Steinberg::kResultFalse;
    std::size_t nonFinite = 0;
};

/// Plan 5.10 "RT-safe load": prepare at block 512, hold E1 (note-on at 0, 8 blocks rendered), setState(Comp)
/// outside the measured region, then ONE process(512) inside the scope.
[[nodiscard]] RtLoadReading rtLoadUnderScope(std::span<const std::uint8_t> comp) {
    ProcessorFixture fx;
    REQUIRE(fx.prepare(kRtFs, static_cast<Steinberg::int32>(kRtBlock)) == Steinberg::kResultOk);
    using Ev = ProcessorFixture::ScriptedEvent;
    const std::array<Ev, 1> hold{Ev{.sample = 0,
                                    .kind = Ev::Kind::NoteOn,
                                    .pitch = static_cast<Steinberg::int16>(auRd::kAuE1),
                                    .velocity = rtVelocity()}};
    fx.renderScript(hold, 8u * kRtBlock, kRtBlock);

    const StateBytes bytes = rtStateOf(comp);
    REQUIRE(loadStateBytes(*fx.proc, bytes) == Steinberg::kResultOk);   // outside the measured region

    std::array<float*, 2> channels{fx.audioL(), fx.audioR()};
    Steinberg::Vst::AudioBusBuffers bus{};
    bus.numChannels = 2;
    bus.silenceFlags = 0;
    bus.channelBuffers32 = channels.data();
    Steinberg::Vst::ProcessData data{};
    data.processMode = Steinberg::Vst::kRealtime;
    data.symbolicSampleSize = Steinberg::Vst::kSample32;
    data.numSamples = static_cast<Steinberg::int32>(kRtBlock);
    data.numInputs = 0;
    data.inputs = nullptr;
    data.numOutputs = 1;
    data.outputs = &bus;
    data.inputParameterChanges = nullptr;
    data.outputParameterChanges = nullptr;
    data.inputEvents = nullptr;
    data.outputEvents = nullptr;
    data.processContext = nullptr;
    ::Profundum::Processor& proc = *fx.proc;

    RtLoadReading out;
    {
        TestHelpers::AllocationScope scope;
        out.processResult = proc.process(data);
        out.allocations = TestHelpers::AllocationDetector::instance().getAllocationCount();
    }
    for (std::size_t i = 0; i < kRtBlock; ++i) {
        out.nonFinite += presetFloatIsFinite(fx.audioL()[i]) ? 0u : 1u;
        out.nonFinite += presetFloatIsFinite(fx.audioR()[i]) ? 0u : 1u;
    }
    return out;
}

// -----------------------------------------------------------------------------------------------------------------
// FR-046: per-push render guard
// -----------------------------------------------------------------------------------------------------------------

struct RtGuardRender {
    std::vector<float> L, R;
};

/// Plan 5.5 "Per-push guard": setState(Comp) -> the optional FR-041 prelude -> setupProcessing(48 kHz, 512) -> E1
/// on at 0 (velocity 100 / 127), off at llround(0.5 fs), total llround(0.75 fs).
[[nodiscard]] RtGuardRender rtGuardRender(std::span<const std::uint8_t> comp, bool maxMass) {
    ProcessorFixture fx;
    const StateBytes bytes = rtStateOf(comp);
    REQUIRE(loadStateBytes(*fx.proc, bytes) == Steinberg::kResultOk);
    if (maxMass) {
        rtMaxMassPrelude(fx);
    }
    REQUIRE(fx.prepare(kRtFs, static_cast<Steinberg::int32>(kRtBlock)) == Steinberg::kResultOk);
    const auto noteOff = static_cast<std::size_t>(std::llround(0.5 * kRtFs));
    const auto total = static_cast<std::size_t>(std::llround(0.75 * kRtFs));
    using Ev = ProcessorFixture::ScriptedEvent;
    const std::array<Ev, 2> script{
        Ev{.sample = 0,
           .kind = Ev::Kind::NoteOn,
           .pitch = static_cast<Steinberg::int16>(auRd::kAuE1),
           .velocity = rtVelocity()},
        Ev{.sample = static_cast<Steinberg::int64>(noteOff),
           .kind = Ev::Kind::NoteOff,
           .pitch = static_cast<Steinberg::int16>(auRd::kAuE1)}};
    fx.renderScript(script, total, kRtBlock);
    REQUIRE(fx.capturedL.size() == total);
    REQUIRE(fx.capturedR.size() == total);
    RtGuardRender out;
    out.L = std::move(fx.capturedL);
    out.R = std::move(fx.capturedR);
    return out;
}

// -----------------------------------------------------------------------------------------------------------------
// FR-048: load under a held note
// -----------------------------------------------------------------------------------------------------------------

constexpr std::size_t kRtLoadAt = 24u * kRtBlock;   // L = 12288
static_assert(kRtLoadAt == 12288u, "plan 5.10: L = 24 x 512");
constexpr std::size_t kRtRefSpan = 4096u;           // 2^12

[[nodiscard]] std::size_t rtMs(double ms) { return static_cast<std::size_t>(std::llround(ms * kRtFs / 1000.0)); }

/// Plan 5.10 step 1-3: a default-state processor holds E1 from 0; setState(Comp) at L between two process() calls;
/// rendered to L + 100 ms + 2^12 + 512.
[[nodiscard]] RtGuardRender rtHeldLoadRender(std::span<const std::uint8_t> comp) {
    ProcessorFixture fx;
    REQUIRE(fx.prepare(kRtFs, static_cast<Steinberg::int32>(kRtBlock)) == Steinberg::kResultOk);
    using Ev = ProcessorFixture::ScriptedEvent;
    const std::array<Ev, 1> hold{Ev{.sample = 0,
                                    .kind = Ev::Kind::NoteOn,
                                    .pitch = static_cast<Steinberg::int16>(auRd::kAuE1),
                                    .velocity = rtVelocity()}};
    fx.renderScript(hold, kRtLoadAt, kRtBlock);
    REQUIRE(fx.capturedL.size() == kRtLoadAt);

    const StateBytes bytes = rtStateOf(comp);
    REQUIRE(loadStateBytes(*fx.proc, bytes) == Steinberg::kResultOk);   // between two process() calls

    const std::size_t total = kRtLoadAt + rtMs(100.0) + kRtRefSpan + kRtBlock;
    fx.renderScript(std::span<const Ev>{}, total - kRtLoadAt, kRtBlock);
    REQUIRE(fx.capturedL.size() == total);
    REQUIRE(fx.capturedR.size() == total);
    RtGuardRender out;
    out.L = std::move(fx.capturedL);
    out.R = std::move(fx.capturedR);
    return out;
}

/// step(a, b) = max over n in [a, b), ch in {L, R} of |y[n] - y[n-1]| (a >= 1).
[[nodiscard]] double rtStep(const RtGuardRender& r, std::size_t a, std::size_t b) {
    REQUIRE(a >= 1u);
    REQUIRE(b <= r.L.size());
    REQUIRE(b <= r.R.size());
    double worst = 0.0;
    for (std::size_t n = a; n < b; ++n) {
        worst = std::max(worst, std::abs(static_cast<double>(r.L[n]) - static_cast<double>(r.L[n - 1])));
        worst = std::max(worst, std::abs(static_cast<double>(r.R[n]) - static_cast<double>(r.R[n - 1])));
    }
    return worst;
}

struct RtHeldGate {
    double transient = 0.0;   // step(L, L + 20 ms)
    double reference = 0.0;   // max(step(L - 4096, L), step(L + 100 ms, L + 100 ms + 4096))
    double ratio = 0.0;       // transient / reference (+inf on a zero reference with a nonzero transient)
    bool pass = false;
};

[[nodiscard]] RtHeldGate rtHeldGate(const RtGuardRender& r) {
    RtHeldGate g;
    const std::size_t L = kRtLoadAt;
    const std::size_t after = L + rtMs(100.0);
    g.transient = rtStep(r, L, L + rtMs(20.0));
    g.reference = std::max(rtStep(r, L - kRtRefSpan, L), rtStep(r, after, after + kRtRefSpan));
    if (g.reference > 0.0) {
        g.ratio = g.transient / g.reference;
    } else {
        g.ratio = g.transient > 0.0 ? std::numeric_limits<double>::infinity() : 0.0;
    }
    g.pass = g.transient <= 2.0 * g.reference;
    return g;
}

}  // namespace

// =================================================================================================================

TEST_CASE("Profundum_FactoryPresetLoadRtSafe", "[profundum][preset][rt]") {
    enableFTZDAZ();

    // Teeth: the allocation counter is live in this binary (test_main.cpp includes the operator overrides).
    {
        static volatile std::uintptr_t sink = 0;   // the pointer escapes, so the allocation cannot be elided
        std::size_t teeth = 0;
        {
            TestHelpers::AllocationScope scope;
            std::vector<int> v(16, 0);
            sink = reinterpret_cast<std::uintptr_t>(v.data());
            teeth = TestHelpers::AllocationDetector::instance().getAllocationCount();
        }
        INFO("allocations of one std::vector<int>(16) inside a scope: " << teeth);
        REQUIRE(teeth > 0u);
    }

    const std::vector<RtPreset> presets = rtPresetsUnderTest();
    std::size_t worst = 0;
    for (const RtPreset& p : presets) {
        const RtLoadReading r = rtLoadUnderScope(p.comp);
        std::cout << "  rt-load " << p.name << ": allocations=" << r.allocations << " nonFinite=" << r.nonFinite
                  << "\n";
        worst = std::max(worst, r.allocations);
        INFO("preset " << p.name << ": allocations in the first process(512) after setState = " << r.allocations);
        CHECK(r.processResult == Steinberg::kResultOk);
        CHECK(r.allocations == 0u);
        CHECK(r.nonFinite == 0u);
    }
    std::cout << "rt-load worst allocations=" << worst << " over " << presets.size() << " presets\n";
}

namespace {

/// R-11 split (plan §12; T027, 2026-10-24): shard `shard` of `shards` takes the presets whose index % shards == shard.
/// Every shard prints its own wall-clock line and checks it against kRtGuardSecondsBar.
void runRenderGuardShard(std::size_t shard, std::size_t shards) {
    enableFTZDAZ();
    const auto t0 = std::chrono::steady_clock::now();

    const std::vector<RtPreset> presets = rtPresetsUnderTest(shard, shards);
    REQUIRE(!presets.empty());
    const std::size_t s0 = presetSolidSettleSample(kRtFs);
    REQUIRE(s0 == 544u);
    float worstPeak = -200.0f;
    std::string worstName;
    for (const RtPreset& p : presets) {
        const DecodedPresetState d = decodePresetState(p.comp);
        REQUIRE(d.ok);
        for (const bool maxMass : {false, true}) {
            const RtGuardRender r = rtGuardRender(p.comp, maxMass);
            const std::size_t nonFinite = rtNonFinite(r.L, r.R);
            const float peak = Krate::DSP::TestUtils::measureTruePeakDb(r.L.data(), r.R.data(), r.L.size(), kRtFs);
            const char* variant = maxMass ? "max Mass" : "stored";
            std::cout << "  guard " << p.name << " [" << variant << "]: truePeak=" << peak
                      << " dBTP nonFinite=" << nonFinite << "\n";
            if (peak > worstPeak) {
                worstPeak = peak;
                worstName = p.name + " [" + variant + "]";
            }
            INFO("preset " << p.name << " [" << variant << "]: true peak " << peak << " dBTP, " << nonFinite
                           << " non-finite samples");
            CHECK(nonFinite == 0u);
            CHECK(peak <= kRtTruePeakBarDb);

            // Plan 5.6 "Lanes": a SOLID-active preset reads L == R bitwise from s0 on its stored guard render.
            if (!maxMass && presetSolidActive(d)) {
                std::size_t differing = 0;
                for (std::size_t i = s0; i < r.L.size(); ++i) {
                    if (std::bit_cast<std::uint32_t>(r.L[i]) != std::bit_cast<std::uint32_t>(r.R[i])) {
                        ++differing;
                    }
                }
                INFO("SOLID-active preset " << p.name << ": " << differing << " samples with L != R from " << s0);
                CHECK(differing == 0u);
            }
        }
    }

    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "render-guard worst truePeak=" << worstPeak << " dBTP (" << worstName << "); wall clock " << seconds
              << " s over " << presets.size() << " presets (shard " << shard << " of " << shards << ")\n";
    CHECK(seconds <= kRtGuardSecondsBar);
}

}  // namespace

TEST_CASE("Profundum_PresetRenderGuard_0of8", "[profundum][preset][rt]") { runRenderGuardShard(0, 8); }
TEST_CASE("Profundum_PresetRenderGuard_1of8", "[profundum][preset][rt]") { runRenderGuardShard(1, 8); }
TEST_CASE("Profundum_PresetRenderGuard_2of8", "[profundum][preset][rt]") { runRenderGuardShard(2, 8); }
TEST_CASE("Profundum_PresetRenderGuard_3of8", "[profundum][preset][rt]") { runRenderGuardShard(3, 8); }
TEST_CASE("Profundum_PresetRenderGuard_4of8", "[profundum][preset][rt]") { runRenderGuardShard(4, 8); }
TEST_CASE("Profundum_PresetRenderGuard_5of8", "[profundum][preset][rt]") { runRenderGuardShard(5, 8); }
TEST_CASE("Profundum_PresetRenderGuard_6of8", "[profundum][preset][rt]") { runRenderGuardShard(6, 8); }
TEST_CASE("Profundum_PresetRenderGuard_7of8", "[profundum][preset][rt]") { runRenderGuardShard(7, 8); }

TEST_CASE("Profundum_PresetLoadUnderHeldNote", "[profundum][preset][rt]") {
    enableFTZDAZ();

    const std::vector<RtPreset> presets = rtPresetsUnderTest();
    double worstRatio = 0.0;
    std::string worstName;
    for (std::size_t k = 0; k < presets.size(); ++k) {
        const RtPreset& p = presets[k];
        RtGuardRender r = rtHeldLoadRender(p.comp);
        const std::size_t nonFinite = rtNonFinite(r.L, r.R);
        const RtHeldGate g = rtHeldGate(r);
        std::cout << "  held-load " << p.name << ": step(L, L+20ms)=" << g.transient << " reference=" << g.reference
                  << " ratio=" << g.ratio << " nonFinite=" << nonFinite << "\n";
        if (g.ratio > worstRatio || worstName.empty()) {
            worstRatio = std::max(worstRatio, g.ratio);
            worstName = p.name;
        }
        INFO("preset " << p.name << ": step(L, L + 20 ms) = " << g.transient << ", reference = " << g.reference
                       << ", ratio = " << g.ratio << " (bar 2), " << nonFinite << " non-finite samples");
        CHECK(nonFinite == 0u);
        CHECK(g.pass);

        // Non-vacuity on T (index 0): a 0.5 full-scale step injected at L + 100 fails the bound.
        if (k == 0u) {
            for (std::size_t i = kRtLoadAt + 100u; i < r.L.size(); ++i) {
                r.L[i] += 0.5f;
                r.R[i] += 0.5f;
            }
            const RtHeldGate bad = rtHeldGate(r);
            INFO("T with a 0.5 step at L + 100: step(L, L + 20 ms) = " << bad.transient
                                                                       << ", reference = " << bad.reference);
            CHECK_FALSE(bad.pass);
        }
    }
    std::cout << "held-load worst ratio=" << worstRatio << " (" << worstName << ") over " << presets.size()
              << " presets\n";
}
