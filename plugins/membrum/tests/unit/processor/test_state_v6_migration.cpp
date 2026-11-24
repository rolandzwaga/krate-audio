// ==============================================================================
// State Round-Trip Tests -- Membrum Phase 6 UI (spec 141)
//
// Covers FR-082 (macros after a load: offsets on the loaded base values, fix
// plan T1.22), FR-084 (bit-exact round-trip of state blobs including macros),
// and session-scoped param exclusion (kUiModeId never in IBStream).
//
// kCurrentStateVersion (6) and the released version 5 are read; the
// version-5 migration is pinned in test_macro_offsets.cpp.
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "controller/controller.h"
#include "dsp/macro_offsets.h"
#include "dsp/pad_config.h"
#include "plugin_ids.h"
#include "processor/processor.h"
#include "voice_pool/voice_pool.h"

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/common/memorystream.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using Catch::Approx;

namespace {

// ------------------------------------------------------------------------------
// Tiny fixture: a freshly initialised Processor ready for get/setState.
// ------------------------------------------------------------------------------
struct V6Fixture
{
    Membrum::Processor processor;

    V6Fixture()
    {
        processor.initialize(nullptr);
        ProcessSetup setup{};
        setup.processMode = kRealtime;
        setup.symbolicSampleSize = kSample32;
        setup.maxSamplesPerBlock = 128;
        setup.sampleRate = 44100.0;
        processor.setupProcessing(setup);
        processor.setActive(true);
    }

    ~V6Fixture()
    {
        processor.setActive(false);
        processor.terminate();
    }

    // MB-001: a load on an active processor applies in the next process()
    // block. One block without output buses.
    void processEmptyBlock()
    {
        ProcessData data{};
        data.processMode = kRealtime;
        data.symbolicSampleSize = kSample32;
        data.numSamples = 128;
        processor.process(data);
    }
};

// Append raw bytes helper.
void appendBytes(std::vector<std::uint8_t>& buf, const void* p, std::size_t n)
{
    const auto* b = static_cast<const std::uint8_t*>(p);
    buf.insert(buf.end(), b, b + n);
}

// Load bytes into a MemoryStream.
void loadIntoStream(MemoryStream& stream, const std::vector<std::uint8_t>& bytes)
{
    int32 written = 0;
    stream.write(const_cast<std::uint8_t*>(bytes.data()),
                 static_cast<int32>(bytes.size()), &written);
    stream.seek(0, IBStream::kIBSeekSet, nullptr);
}

// Read all bytes from a MemoryStream into a vector.
std::vector<std::uint8_t> readAllBytes(MemoryStream& stream)
{
    int64 size = 0;
    stream.seek(0, IBStream::kIBSeekEnd, &size);
    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    std::vector<std::uint8_t> out(static_cast<std::size_t>(size));
    int32 got = 0;
    stream.read(out.data(), static_cast<int32>(size), &got);
    return out;
}

} // namespace

// ==============================================================================
// kCurrentStateVersion sanity check (6 since fix plan T1.22).
// ==============================================================================

TEST_CASE("State (FR-080): kCurrentStateVersion is 6",
          "[phase6_state][state_v6][state]")
{
    STATIC_REQUIRE(Membrum::kCurrentStateVersion == 6);
}

// ==============================================================================
// FR-084: round-trip preserves macro values bit-exact.
// ==============================================================================

TEST_CASE("State (FR-084): round-trip preserves non-default macros",
          "[phase6_state][state_v6][state]")
{
    V6Fixture fx;

    // Seed each pad's macro fields with distinct, non-neutral values.
    // We use small, finitely-representable float32 fractions so the
    // round-trip comparison is exact within 1e-7 tolerance.
    auto& pads = fx.processor.voicePoolForTest().padConfigsArray();
    for (int pad = 0; pad < Membrum::kNumPads; ++pad)
    {
        const float base = 0.01f + static_cast<float>(pad) * (0.95f / 31.0f);
        pads[static_cast<std::size_t>(pad)].macroTightness  = base;
        pads[static_cast<std::size_t>(pad)].macroBrightness = 1.0f - base;
        pads[static_cast<std::size_t>(pad)].macroBodySize   = base * 0.5f + 0.25f;
        pads[static_cast<std::size_t>(pad)].macroPunch      = 0.75f - base * 0.25f;
        pads[static_cast<std::size_t>(pad)].macroComplexity = 0.5f + (base - 0.5f) * 0.5f;
    }

    // Snapshot expected values.
    std::array<std::array<float, 5>, Membrum::kNumPads> expected{};
    for (int pad = 0; pad < Membrum::kNumPads; ++pad)
    {
        const auto& p = pads[static_cast<std::size_t>(pad)];
        expected[static_cast<std::size_t>(pad)] = {
            p.macroTightness,
            p.macroBrightness,
            p.macroBodySize,
            p.macroPunch,
            p.macroComplexity,
        };
    }

    // First save, then load into fx -- this reapplies macros so the underlying
    // params are self-consistent with the macro-derived targets. Any
    // subsequent save-load-save pair must then be byte-exact (FR-084).
    // MB-001: the pads were written straight into the engine; republish them
    // for getState.
    fx.processor.publishPadConfigsForTest();
    MemoryStream s0;
    REQUIRE(fx.processor.getState(&s0) == kResultOk);

    V6Fixture fx1;
    s0.seek(0, IBStream::kIBSeekSet, nullptr);
    REQUIRE(fx1.processor.setState(&s0) == kResultOk);
    fx1.processEmptyBlock();

    // Verify reloaded PadConfigs carry the expected macro values.
    for (int pad = 0; pad < Membrum::kNumPads; ++pad)
    {
        const auto& p = fx1.processor.voicePoolForTest().padConfig(pad);
        const auto& e = expected[static_cast<std::size_t>(pad)];
        CHECK(p.macroTightness  == Approx(e[0]).margin(1e-7f));
        CHECK(p.macroBrightness == Approx(e[1]).margin(1e-7f));
        CHECK(p.macroBodySize   == Approx(e[2]).margin(1e-7f));
        CHECK(p.macroPunch      == Approx(e[3]).margin(1e-7f));
        CHECK(p.macroComplexity == Approx(e[4]).margin(1e-7f));
    }

    // Now do a save -> load -> save cycle and verify byte-exact equality.
    MemoryStream s1;
    REQUIRE(fx1.processor.getState(&s1) == kResultOk);
    auto blob1 = readAllBytes(s1);

    V6Fixture fx2;
    s1.seek(0, IBStream::kIBSeekSet, nullptr);
    REQUIRE(fx2.processor.setState(&s1) == kResultOk);

    MemoryStream s2;
    REQUIRE(fx2.processor.getState(&s2) == kResultOk);
    auto blob2 = readAllBytes(s2);
    CHECK(blob1 == blob2);
}

TEST_CASE("State (FR-084): blob wraps macros + sound slots + master gain, no overrides",
          "[phase6_state][state_v6][state]")
{
    V6Fixture fx;
    MemoryStream stream;
    REQUIRE(fx.processor.getState(&stream) == kResultOk);
    auto bytes = readAllBytes(stream);

    // Layout (see state_codec.h, wire-coupling update):
    //   12 (header) + 32 * 482 (per-pad block: 4 exc + 4 body + 59*8 sound + 2 routing)
    //   + 4 (selPad) + 32 (4 globals) + 256 (32 coupling amounts)
    //   + 1280 (160 macros) + 8 (master gain) = 17016 bytes.
    CHECK(bytes.size() == 17016u);

    // Version field is the first 4 bytes and must equal kCurrentStateVersion.
    int32 version = 0;
    std::memcpy(&version, bytes.data(), sizeof(version));
    CHECK(version == Membrum::kCurrentStateVersion);
}

// ==============================================================================
// Version rejection: kCurrentStateVersion and the released version 5 are
// accepted; every other version (unreleased or future) returns kResultFalse.
// ==============================================================================

TEST_CASE("State: setState rejects every version it cannot map",
          "[phase6_state][state_v6][state]")
{
    V6Fixture fx;
    for (int32 v : {0, 1, 2, 3, 4, 7, 8, 9, 10, 11, 12, 13, 14, 15, 99, -1})
    {
        std::vector<std::uint8_t> buf;
        appendBytes(buf, &v, sizeof(v));
        // Pad out with zeros to look like a plausibly-sized blob.
        buf.resize(128, std::uint8_t{0});

        MemoryStream stream;
        loadIntoStream(stream, buf);
        INFO("rejecting version " << v);
        CHECK(fx.processor.setState(&stream) == kResultFalse);
    }
}

// ==============================================================================
// Session-scoped parameter exclusion: kUiModeId is never written into IBStream
// by the Processor. (Controller resets it on load.)
// ==============================================================================

TEST_CASE("State (FR-082): session-scoped params are not on the wire",
          "[phase6_state][state_v6][state][session_scope]")
{
    V6Fixture fx;
    MemoryStream stream;
    REQUIRE(fx.processor.getState(&stream) == kResultOk);
    auto bytes = readAllBytes(stream);

    // Expected size: 17016 bytes (see FR-084 test, wire-coupling update). If
    // kUiModeId had leaked into the processor blob it would add 4 bytes.
    CHECK(bytes.size() == 17016u);
}

// ==============================================================================
// FR-082 (fix plan T1.22): after a load, a non-neutral macro offsets the
// loaded base value. The stored couplingAmount stays the base on every
// save-load cycle; the pad plays base + the Complexity offset.
// ==============================================================================

TEST_CASE("State: macro-driven couplingAmount survives save-load",
          "[phase6_state][state_v6][state]")
{
    V6Fixture fx;

    auto& pads = fx.processor.voicePoolForTest().padConfigsArray();
    pads[0].couplingAmount = 0.5f;
    pads[0].macroComplexity = 1.0f;
    const float renderedCoupling = Membrum::applyMacroOffsets(pads[0]).couplingAmount;
    REQUIRE(renderedCoupling > 0.5f); // Complexity=1.0 should boost above base.

    // MB-001: the pad was written straight into the engine; republish it.
    fx.processor.publishPadConfigsForTest();
    MemoryStream stream;
    REQUIRE(fx.processor.getState(&stream) == kResultOk);

    V6Fixture fx2;
    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    REQUIRE(fx2.processor.setState(&stream) == kResultOk);
    fx2.processEmptyBlock();

    const auto& p0 = fx2.processor.voicePoolForTest().padConfig(0);
    CHECK(p0.couplingAmount  == 0.5f);  // the stored base, not re-baked
    CHECK(p0.macroComplexity == Approx(1.0f).margin(1e-7f));
    CHECK(Membrum::applyMacroOffsets(p0).couplingAmount ==
          Approx(renderedCoupling).margin(1e-6f));
}

// (The version-5 migration, its sound and its macro positions, is pinned in
// test_macro_offsets.cpp.)
