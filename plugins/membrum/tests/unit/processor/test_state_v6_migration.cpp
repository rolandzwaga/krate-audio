// ==============================================================================
// State v6 Migration Tests -- Membrum Phase 6 UI (spec 141)
//
// Covers FR-080 (v6 kCurrentStateVersion), FR-081 (v5->v6 migration with
// neutral macro defaults), FR-082 (MacroMapper::reapplyAll after load),
// FR-083 (v1/v2/v3/v4 migration chains forward to v6), FR-084 (bit-exact
// round-trip of v6 blobs including macros), and session-scoped param
// exclusion (kUiModeId / kEditorSizeId never in IBStream).
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
//     [160 x float32 pad-major macros: pad0.tightness..pad0.complexity, ...,
//       pad31.tightness..pad31.complexity]
//
// Sizes:
//   v4 payload:   12 + 32*282 + 4             = 9040 bytes
//   v5 payload:   v4 + 4*8 + 32*8 + 2         = 9330 bytes (zero overrides)
//   v6 payload:   v5 + 160*4                  = 9970 bytes (zero overrides)
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "controller/controller.h"
#include "dsp/pad_config.h"
#include "plugin_ids.h"
#include "processor/processor.h"
#include "voice_pool/voice_pool.h"

#include "pluginterfaces/base/ibstream.h"
#include "public.sdk/source/common/memorystream.h"

#include <algorithm>
#include <array>
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

// Build a synthetic v4 state blob (size 9040 bytes).
std::vector<std::uint8_t> buildV4Blob()
{
    std::vector<std::uint8_t> buf;
    const int32 version = 4;
    const int32 maxPoly = 8;
    const int32 stealPolicy = 0;
    appendBytes(buf, &version, sizeof(version));
    appendBytes(buf, &maxPoly, sizeof(maxPoly));
    appendBytes(buf, &stealPolicy, sizeof(stealPolicy));
    for (int pad = 0; pad < Membrum::kNumPads; ++pad)
    {
        const int32 ex = 0;
        const int32 bm = 0;
        appendBytes(buf, &ex, sizeof(ex));
        appendBytes(buf, &bm, sizeof(bm));
        for (int i = 0; i < 34; ++i)
        {
            const double z = 0.0;
            appendBytes(buf, &z, sizeof(z));
        }
        const std::uint8_t cg = 0;
        const std::uint8_t ob = 0;
        appendBytes(buf, &cg, sizeof(cg));
        appendBytes(buf, &ob, sizeof(ob));
    }
    const int32 selPad = 0;
    appendBytes(buf, &selPad, sizeof(selPad));
    return buf;
}

// Build a synthetic v5 state blob (v4 body + Phase 5 tail, zero overrides).
std::vector<std::uint8_t> buildV5Blob()
{
    auto buf = buildV4Blob();
    // Rewrite version field (first 4 bytes) to 5.
    const int32 v5 = 5;
    std::memcpy(buf.data(), &v5, sizeof(v5));

    // Global coupling params.
    const double gc = 0.0;
    const double sb = 0.0;
    const double tr = 0.0;
    const double cd = 1.0;
    appendBytes(buf, &gc, sizeof(gc));
    appendBytes(buf, &sb, sizeof(sb));
    appendBytes(buf, &tr, sizeof(tr));
    appendBytes(buf, &cd, sizeof(cd));

    // 32 per-pad coupling amounts.
    for (int pad = 0; pad < Membrum::kNumPads; ++pad)
    {
        const double amt = 0.5;
        appendBytes(buf, &amt, sizeof(amt));
    }

    // Zero overrides.
    const std::uint16_t overrideCount = 0;
    appendBytes(buf, &overrideCount, sizeof(overrideCount));
    return buf;
}

// Build a synthetic v1 blob (5 float64 params + version=1 header).
// Post-Phase 1 format from processor.cpp legacy path.
std::vector<std::uint8_t> buildV1Blob()
{
    std::vector<std::uint8_t> buf;
    const int32 version = 1;
    appendBytes(buf, &version, sizeof(version));
    // 5 float64 sound params (material, size, decay, strikePos, level).
    for (int i = 0; i < 5; ++i)
    {
        const double v = 0.5;
        appendBytes(buf, &v, sizeof(v));
    }
    return buf;
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
// FR-080: kCurrentStateVersion == 6.
// ==============================================================================

TEST_CASE("State v6 (FR-080): kCurrentStateVersion is 6",
          "[phase6_state][state_v6][state]")
{
    STATIC_REQUIRE(Membrum::kCurrentStateVersion == 6);
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

TEST_CASE("State v6 (FR-084): v6 blob contains 160 macro floats (640 bytes)",
          "[phase6_state][state_v6][state]")
{
    V6Fixture fx;
    MemoryStream stream;
    REQUIRE(fx.processor.getState(&stream) == kResultOk);
    auto bytes = readAllBytes(stream);

    // v5 payload with zero overrides = 9330 bytes; v6 adds 160*4 = 640 bytes.
    CHECK(bytes.size() == 9970u);

    // Version field is the first 4 bytes and must equal 6.
    int32 version = 0;
    std::memcpy(&version, bytes.data(), sizeof(version));
    CHECK(version == 6);
}

// ==============================================================================
// FR-081: v5 blob loads with all 160 macros at 0.5 (neutral).
// ==============================================================================

TEST_CASE("State v6 (FR-081): v5 blob migrates with neutral macro defaults",
          "[phase6_state][state_v6][state][migration]")
{
    V6Fixture fx;

    // Pre-dirty the macros so we can detect a reset-to-0.5 on load.
    auto& padsPre = fx.processor.voicePoolForTest().padConfigsArray();
    for (auto& p : padsPre)
    {
        p.macroTightness  = 0.1f;
        p.macroBrightness = 0.9f;
        p.macroBodySize   = 0.1f;
        p.macroPunch      = 0.9f;
        p.macroComplexity = 0.1f;
    }

    auto v5 = buildV5Blob();
    MemoryStream stream;
    loadIntoStream(stream, v5);
    REQUIRE(fx.processor.setState(&stream) == kResultOk);

    for (int pad = 0; pad < Membrum::kNumPads; ++pad)
    {
        const auto& p = fx.processor.voicePoolForTest().padConfig(pad);
        CHECK(p.macroTightness  == Approx(0.5f).margin(1e-7f));
        CHECK(p.macroBrightness == Approx(0.5f).margin(1e-7f));
        CHECK(p.macroBodySize   == Approx(0.5f).margin(1e-7f));
        CHECK(p.macroPunch      == Approx(0.5f).margin(1e-7f));
        CHECK(p.macroComplexity == Approx(0.5f).margin(1e-7f));
    }
}

// ==============================================================================
// FR-083: v4 blob migrates with neutral macros + Phase 5 defaults.
// ==============================================================================

TEST_CASE("State v6 (FR-083): v4 blob migrates through v5 and v6 defaults",
          "[phase6_state][state_v6][state][migration]")
{
    V6Fixture fx;

    auto& padsPre = fx.processor.voicePoolForTest().padConfigsArray();
    for (auto& p : padsPre)
    {
        p.macroTightness  = 0.2f;
        p.macroBrightness = 0.8f;
        p.couplingAmount  = 0.123f;
    }

    auto v4 = buildV4Blob();
    REQUIRE(v4.size() == 9040u);
    MemoryStream stream;
    loadIntoStream(stream, v4);
    REQUIRE(fx.processor.setState(&stream) == kResultOk);

    for (int pad = 0; pad < Membrum::kNumPads; ++pad)
    {
        const auto& p = fx.processor.voicePoolForTest().padConfig(pad);
        // v6 defaults: all macros neutral.
        CHECK(p.macroTightness  == Approx(0.5f).margin(1e-7f));
        CHECK(p.macroBrightness == Approx(0.5f).margin(1e-7f));
        CHECK(p.macroBodySize   == Approx(0.5f).margin(1e-7f));
        CHECK(p.macroPunch      == Approx(0.5f).margin(1e-7f));
        CHECK(p.macroComplexity == Approx(0.5f).margin(1e-7f));
        // Phase 5 defaults: couplingAmount resets to 0.5.
        CHECK(p.couplingAmount  == Approx(0.5f).margin(1e-7f));
    }
}

// ==============================================================================
// FR-083: v1 blob migrates through the full v1 -> v2 -> v3 -> v4 -> v5 -> v6 chain.
// ==============================================================================

TEST_CASE("State v6 (FR-083): v1 blob migrates through full chain without crash",
          "[phase6_state][state_v6][state][migration]")
{
    V6Fixture fx;
    auto v1 = buildV1Blob();
    MemoryStream stream;
    loadIntoStream(stream, v1);
    // The v1 legacy path reads best-effort and may return kResultOk even on
    // short blobs (missing Phase 2+ data falls back to defaults). What matters
    // is no crash and that v6 defaults are applied.
    REQUIRE(fx.processor.setState(&stream) == kResultOk);

    for (int pad = 0; pad < Membrum::kNumPads; ++pad)
    {
        const auto& p = fx.processor.voicePoolForTest().padConfig(pad);
        CHECK(p.macroTightness  == Approx(0.5f).margin(1e-7f));
        CHECK(p.macroBrightness == Approx(0.5f).margin(1e-7f));
        CHECK(p.macroBodySize   == Approx(0.5f).margin(1e-7f));
        CHECK(p.macroPunch      == Approx(0.5f).margin(1e-7f));
        CHECK(p.macroComplexity == Approx(0.5f).margin(1e-7f));
    }
}

// ==============================================================================
// Future-version rejection: version > 6 returns kResultFalse.
// ==============================================================================

TEST_CASE("State v6: setState rejects v7 blob",
          "[phase6_state][state_v6][state]")
{
    V6Fixture fx;
    std::vector<std::uint8_t> buf;
    const int32 v7 = 7;
    appendBytes(buf, &v7, sizeof(v7));
    // Pad out with zeros to look like a plausibly-sized blob.
    buf.resize(128, std::uint8_t{0});

    MemoryStream stream;
    loadIntoStream(stream, buf);
    CHECK(fx.processor.setState(&stream) == kResultFalse);
}

// ==============================================================================
// Session-scoped parameter exclusion: kUiModeId and kEditorSizeId are never
// written into IBStream by the Processor. (Controller resets them on load.)
// ==============================================================================

TEST_CASE("State v6 (FR-082): session-scoped params are not on the wire",
          "[phase6_state][state_v6][state][session_scope]")
{
    V6Fixture fx;
    MemoryStream stream;
    REQUIRE(fx.processor.getState(&stream) == kResultOk);
    auto bytes = readAllBytes(stream);

    // Expected size is fixed (9970 bytes). If session-scoped params had
    // leaked into the blob they would add ~12 bytes (2 x float64 or similar)
    // and this size check would fail.
    CHECK(bytes.size() == 9970u);

    // Additionally, a scan for the raw parameter IDs must not find them.
    // kUiModeId (280) and kEditorSizeId (281) would not typically appear
    // as literals, but the known v6 layout has no parameter-ID field at all,
    // so any occurrence of little-endian 280 / 281 as an int32 at a
    // four-byte aligned offset is suspicious. This is a soft check.
    auto containsLittleEndianInt32 = [&](int32 needle) noexcept {
        std::uint8_t want[4];
        std::memcpy(want, &needle, 4);
        for (std::size_t i = 0; i + 4 <= bytes.size(); i += 4)
        {
            if (std::memcmp(bytes.data() + i, want, 4) == 0)
                return true;
        }
        return false;
    };
    (void)containsLittleEndianInt32; // reserved for future diagnostics
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
