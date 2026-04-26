// ==============================================================================
// State Round-Trip Tests -- Membrum Phase 6 UI (spec 141)
//
// Covers FR-082 (MacroMapper::reapplyAll after load), FR-084 (bit-exact
// round-trip of state blobs including macros), and session-scoped param
// exclusion (kUiModeId never in IBStream).
//
// All legacy version migration logic was dropped in the pre-release codec
// reset -- only kCurrentStateVersion (= 1) is accepted on read. Test cases
// retain the historical "v6" naming in the file name for git history; the
// assertions all target the current format.
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "controller/controller.h"
#include "dsp/pad_config.h"
#include "plugin_ids.h"
#include "processor/macro_mapper.h"
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
// kCurrentStateVersion sanity check (pinned at 1 by the pre-release reset).
// ==============================================================================

TEST_CASE("State (FR-080): kCurrentStateVersion is 1",
          "[phase6_state][state_v6][state]")
{
    STATIC_REQUIRE(Membrum::kCurrentStateVersion == 1);
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

TEST_CASE("State (FR-084): blob wraps macros + sound slots + master gain, no overrides",
          "[phase6_state][state_v6][state]")
{
    V6Fixture fx;
    MemoryStream stream;
    REQUIRE(fx.processor.getState(&stream) == kResultOk);
    auto bytes = readAllBytes(stream);

    // Layout (see state_codec.h):
    //   12 (header) + 32 * 426 (per-pad block) + 4 (selPad) + 32 (4 globals)
    //   + 256 (32 coupling amounts) + 1280 (160 macros) + 8 (master gain)
    //   = 15224 bytes.
    CHECK(bytes.size() == 15224u);

    // Version field is the first 4 bytes and must equal kCurrentStateVersion.
    int32 version = 0;
    std::memcpy(&version, bytes.data(), sizeof(version));
    CHECK(version == Membrum::kCurrentStateVersion);
}

// ==============================================================================
// Version rejection: only kCurrentStateVersion is accepted; everything else
// (legacy or future) returns kResultFalse.
// ==============================================================================

TEST_CASE("State: setState rejects every version other than kCurrentStateVersion",
          "[phase6_state][state_v6][state]")
{
    V6Fixture fx;
    for (int32 v : {0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 99, -1})
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

    // Expected size: 15224 bytes (see FR-084 test). If kUiModeId had leaked
    // into the processor blob it would add 4 bytes.
    CHECK(bytes.size() == 15224u);
}

// ==============================================================================
// FR-082: MacroMapper::reapplyAll() is invoked after a v6 load with
// non-neutral macros -- verified indirectly by observing an underlying
// target parameter (cfg.couplingAmount on pad 0) change away from its
// loaded value toward the macro-derived value.
// ==============================================================================

TEST_CASE("State: macro-driven couplingAmount survives save-load",
          "[phase6_state][state_v6][state]")
{
    // Phase 8G: setState no longer "fixes" inconsistent saved state by calling
    // reapplyAll. With incremental MacroMapper, reapplyAll on load would drift
    // bytes on every save-load cycle (each load layers another delta on top
    // of the saved post-macro values). The contract now is: saved state is
    // loaded verbatim. To exercise macro-derived couplingAmount, we let the
    // MacroMapper apply Complexity properly first, then verify save+load
    // round-trips what was rendered to cfg.
    V6Fixture fx;

    auto& pads = fx.processor.voicePoolForTest().padConfigsArray();
    pads[0].macroComplexity = 1.0f;
    Membrum::MacroMapper mapper;
    Membrum::RegisteredDefaultsTable defaults{};
    defaults.byOffset[Membrum::kPadCouplingAmount]    = 0.5f;
    defaults.byOffset[Membrum::kPadNonlinearCoupling] = 0.0f;
    defaults.byOffset[Membrum::kPadModeInjectAmount]  = 0.0f;
    mapper.prepare(defaults);
    mapper.apply(0, pads[0]);

    const float renderedCoupling = pads[0].couplingAmount;
    REQUIRE(renderedCoupling > 0.5f); // Complexity=1.0 should boost above base.

    MemoryStream stream;
    REQUIRE(fx.processor.getState(&stream) == kResultOk);

    V6Fixture fx2;
    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    REQUIRE(fx2.processor.setState(&stream) == kResultOk);

    const auto& p0 = fx2.processor.voicePoolForTest().padConfig(0);
    CHECK(p0.couplingAmount  == Approx(renderedCoupling).margin(1e-6f));
    CHECK(p0.macroComplexity == Approx(1.0f).margin(1e-7f));
}

// (SC-006 v5-blob audio parity test removed along with the v5->v6 migration
// code path; with a single on-wire version there is no cross-version audio
// comparison to perform.)
