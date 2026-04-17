// ==============================================================================
// State v6 Round-Trip Tests -- Membrum Phase 6 UI (spec 141)
//
// Covers FR-080 (v6 kCurrentStateVersion), FR-082 (MacroMapper::reapplyAll
// after load), FR-084 (bit-exact round-trip of v6 blobs including macros),
// and session-scoped param exclusion (kUiModeId never in
// IBStream).
//
// Pre-v6 migration tests were removed when the Membrum state format was
// unified and backwards compatibility dropped (plugin has not shipped).
// ==============================================================================
//
// Binary layout reference (per specs/141-membrum-phase6-ui/contracts/
// state_v6_migration.md):
//   v5 payload:
//     [int32 version][int32 maxPoly][int32 stealPolicy]
//     32 x ([int32 exciter][int32 body][34 x float64 sound][uint8 cg][uint8 ob])
//     [int32 selectedPadIndex]
//     [4 x float64 global coupling (gc, sb, tr, cd)]
//     [32 x float64 per-pad couplingAmount]
//     [uint16 overrideCount][N x (uint8 src, uint8 dst, float32 coeff)]
//   v6 appends:
//     [160 x float64 pad-major macros: pad0.tightness..pad0.complexity, ...,
//       pad31.tightness..pad31.complexity]
//
// Sizes:
//   v4 payload:   12 + 32*282 + 4             = 9040 bytes
//   v5 payload:   v4 + 4*8 + 32*8 + 2         = 9330 bytes (zero overrides)
//   v6 payload:   v5 + 160*8                  = 10610 bytes (zero overrides)
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "controller/controller.h"
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
// FR-080: kCurrentStateVersion (updated to 8 in Phase 8A for per-mode damping).
// ==============================================================================

TEST_CASE("State v6/v7/v8 (FR-080): kCurrentStateVersion is 8",
          "[phase6_state][state_v6][state]")
{
    STATIC_REQUIRE(Membrum::kCurrentStateVersion == 8);
}

// ==============================================================================
// FR-084: v6 round-trip preserves macro values bit-exact.
// ==============================================================================

TEST_CASE("State v6 (FR-084): round-trip preserves non-default macros",
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
    MemoryStream s0;
    REQUIRE(fx.processor.getState(&s0) == kResultOk);

    V6Fixture fx1;
    s0.seek(0, IBStream::kIBSeekSet, nullptr);
    REQUIRE(fx1.processor.setState(&s0) == kResultOk);

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

TEST_CASE("State v8 (FR-084): v8 blob wraps macros + Phase 7/8A sound slots",
          "[phase6_state][state_v6][state]")
{
    V6Fixture fx;
    MemoryStream stream;
    REQUIRE(fx.processor.getState(&stream) == kResultOk);
    auto bytes = readAllBytes(stream);

    // v5 payload with zero overrides = 9330 bytes; v6 adds 160*8 = 1280 bytes
    // of macros -> 10610. v7 extends each pad's sound array from 34 to 42
    // doubles -> +32*8*8 = +2048 bytes -> 12658. v8 adds 2 more sound slots
    // per pad -> +32*2*8 = +512 bytes -> 13170.
    CHECK(bytes.size() == 13170u);

    // Version field is the first 4 bytes and must equal 8.
    int32 version = 0;
    std::memcpy(&version, bytes.data(), sizeof(version));
    CHECK(version == 8);
}

// ==============================================================================
// Version rejection: v6/v7/v8 are accepted; any other version returns
// kResultFalse.
// ==============================================================================

TEST_CASE("State v8: setState rejects future / out-of-range versions",
          "[phase6_state][state_v6][state]")
{
    V6Fixture fx;
    for (int32 v : {9, 10, 99, -1})
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

TEST_CASE("State v8: setState rejects pre-v6 blobs",
          "[phase6_state][state_v6][state]")
{
    V6Fixture fx;
    for (int32 v : {1, 2, 3, 4, 5})
    {
        std::vector<std::uint8_t> buf;
        appendBytes(buf, &v, sizeof(v));
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

TEST_CASE("State v6 (FR-082): session-scoped params are not on the wire",
          "[phase6_state][state_v6][state][session_scope]")
{
    V6Fixture fx;
    MemoryStream stream;
    REQUIRE(fx.processor.getState(&stream) == kResultOk);
    auto bytes = readAllBytes(stream);

    // Expected size for v8: 13170 bytes (see FR-084 test). If kUiModeId had
    // leaked into the processor blob it would add 4 bytes.
    CHECK(bytes.size() == 13170u);
}

// ==============================================================================
// FR-082: MacroMapper::reapplyAll() is invoked after a v6 load with
// non-neutral macros -- verified indirectly by observing an underlying
// target parameter (cfg.couplingAmount on pad 0) change away from its
// loaded value toward the macro-derived value.
// ==============================================================================

TEST_CASE("State v6 (FR-082): reapplyAll runs after v6 load with non-neutral macros",
          "[phase6_state][state_v6][state]")
{
    V6Fixture fx;

    auto& pads = fx.processor.voicePoolForTest().padConfigsArray();
    // Drive Complexity to 1.0 on pad 0. Per MacroMapper::applyComplexity, this
    // adds kComplexityCouplingSpan * 0.5 to the couplingAmount base. We do NOT
    // need to know the exact span -- it suffices that reapplyAll changes
    // pad0.couplingAmount from its loaded value.
    pads[0].macroComplexity = 1.0f;
    pads[0].couplingAmount = 0.0f; // deliberately inconsistent with macro

    MemoryStream stream;
    REQUIRE(fx.processor.getState(&stream) == kResultOk);

    V6Fixture fx2;
    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    REQUIRE(fx2.processor.setState(&stream) == kResultOk);

    const auto& p0 = fx2.processor.voicePoolForTest().padConfig(0);
    // After reapplyAll, couplingAmount should have been recomputed from
    // the Complexity macro, so it should differ from the 0.0 we stored.
    CHECK(p0.couplingAmount > 0.0f);
    // And macroComplexity itself should still be 1.0 (round-trip).
    CHECK(p0.macroComplexity == Approx(1.0f).margin(1e-7f));
}

// (SC-006 v5-blob audio parity test removed along with the v5->v6 migration
// code path; with a single on-wire version there is no cross-version audio
// comparison to perform.)
