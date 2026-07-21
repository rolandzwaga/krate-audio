// ==============================================================================
// Gradus Arpeggiator Parameter Round-Trip Tests (Wave 3 D3 guardrail)
// ==============================================================================
// Gradus and Ruinae SHARE arp param IDs 3000-3372 and independent copies of the
// arp save/load logic (plugins/{gradus,ruinae}/src/parameters/arpeggiator_params.h).
// Ruinae already has a save/load round-trip test; this adds the matching guardrail
// for Gradus so BOTH plugins lock in round-trip integrity over the shared range.
//
// This test MUST keep passing across any future unification of the shared arp-param
// core (the D3 CRTP extraction): its purpose is to prove that saveArpParams ->
// stream -> loadArpParams preserves every shared field byte-for-value, so a change
// to the shared serialization cannot silently break Gradus's state.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "parameters/arpeggiator_params.h"
#include "plugin_ids.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"

#include "test_helpers/arp_shared_prefix_golden.h"

#include <vector>

using Catch::Approx;
using namespace Steinberg;

// Fields shared with Ruinae over arp IDs 3000-3372. Setting them to non-default
// values and round-tripping guarantees the shared serialization is intact.
TEST_CASE("Gradus ArpParams_SaveLoad_RoundTrip (shared range)", "[arp][params][state]") {
    using namespace Gradus;

    ArpeggiatorParams original;
    original.operatingMode.store(kArpMIDI, std::memory_order_relaxed);
    original.mode.store(3, std::memory_order_relaxed);          // DownUp
    original.octaveRange.store(3, std::memory_order_relaxed);
    original.octaveMode.store(1, std::memory_order_relaxed);    // Interleaved
    original.tempoSync.store(false, std::memory_order_relaxed);
    original.noteValue.store(14, std::memory_order_relaxed);    // 1/4D
    original.freeRate.store(12.5f, std::memory_order_relaxed);
    original.gateLength.store(60.0f, std::memory_order_relaxed);
    original.swing.store(25.0f, std::memory_order_relaxed);
    original.latchMode.store(1, std::memory_order_relaxed);     // Hold
    original.retrigger.store(2, std::memory_order_relaxed);     // Beat

    auto stream = Steinberg::owned(new Steinberg::MemoryStream());
    {
        Steinberg::IBStreamer writeStream(stream, kLittleEndian);
        saveArpParams(original, writeStream);
    }

    ArpeggiatorParams loaded;
    stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    {
        Steinberg::IBStreamer readStream(stream, kLittleEndian);
        bool ok = loadArpParams(loaded, readStream);
        REQUIRE(ok);
    }

    CHECK(loaded.operatingMode.load() == kArpMIDI);
    CHECK(loaded.mode.load() == 3);
    CHECK(loaded.octaveRange.load() == 3);
    CHECK(loaded.octaveMode.load() == 1);
    CHECK(loaded.tempoSync.load() == false);
    CHECK(loaded.noteValue.load() == 14);
    CHECK(loaded.freeRate.load() == Approx(12.5f).margin(0.001f));
    CHECK(loaded.gateLength.load() == Approx(60.0f).margin(0.001f));
    CHECK(loaded.swing.load() == Approx(25.0f).margin(0.001f));
    CHECK(loaded.latchMode.load() == 1);
    CHECK(loaded.retrigger.load() == 2);
}

// Empty / truncated stream must fail cleanly and leave params at defaults
// (matches Ruinae's backward-compatibility contract for the shared loader).
TEST_CASE("Gradus ArpParams_LoadArpParams_BackwardCompatibility", "[arp][params][state][compat]") {
    using namespace Gradus;

    SECTION("empty stream returns false, params remain at defaults") {
        ArpeggiatorParams params;
        const int defaultMode = params.mode.load();

        auto stream = Steinberg::owned(new Steinberg::MemoryStream());
        stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
        Steinberg::IBStreamer readStream(stream, kLittleEndian);
        bool ok = loadArpParams(params, readStream);

        CHECK_FALSE(ok);
        CHECK(params.mode.load() == defaultMode);
    }
}

// Cross-plugin byte-identity: the shared 200-byte save prefix must match Ruinae's
// (both go through Krate::Shared::saveArpParamsShared). Same golden constant is
// asserted in plugins/ruinae/.../arpeggiator_params_test.cpp.
TEST_CASE("Gradus arp shared-prefix save is byte-golden (D3)", "[arp][params][state][shared]") {
    using namespace Gradus;

    ArpeggiatorParams p;
    Krate::Test::setSharedArpFieldsDeterministic(p);

    auto stream = Steinberg::owned(new Steinberg::MemoryStream());
    {
        Steinberg::IBStreamer writeStream(stream, kLittleEndian);
        saveArpParams(p, writeStream);
    }
    stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    std::vector<uint8_t> bytes(Krate::Test::kSharedArpPrefixBytes, 0);
    int32 numRead = 0;
    stream->read(bytes.data(), static_cast<int32>(bytes.size()), &numRead);
    REQUIRE(numRead == static_cast<int32>(Krate::Test::kSharedArpPrefixBytes));

    const uint32_t fnv = Krate::Test::fnv1a(bytes.data(), bytes.size());
    INFO("gradus shared-prefix FNV = 0x" << std::hex << fnv);
    CHECK(fnv == Krate::Test::kSharedArpPrefixGoldenFnv);
}

// =============================================================================
// speedValueToNormalized (Gradus audit F10)
// =============================================================================
// The value->nearest-dropdown-index snap used to be copy-pasted three times in
// the load paths with drifted (dead) search seeds. This pins the single helper
// they now share; the existing save/load round-trip guardrails above cover the
// behaviour-identity of the refactor itself.

TEST_CASE("speedValueToNormalized snaps to the nearest kLaneSpeedValues entry",
          "[gradus][params][F10]")
{
    const auto normFor = [](int index) {
        return static_cast<double>(index)
             / static_cast<double>(Gradus::kLaneSpeedCount - 1);
    };

    // Exact entries map to their own index.
    for (int i = 0; i < Gradus::kLaneSpeedCount; ++i) {
        INFO("entry " << i << " = " << Gradus::kLaneSpeedValues[i]);
        CHECK(Gradus::speedValueToNormalized(Gradus::kLaneSpeedValues[i])
              == Catch::Approx(normFor(i)));
    }

    // Values between entries snap to the nearer one.
    CHECK(Gradus::speedValueToNormalized(0.26f) == Catch::Approx(normFor(0)));   // -> 0.25
    CHECK(Gradus::speedValueToNormalized(0.9f)  == Catch::Approx(normFor(3)));   // -> 1.0
    CHECK(Gradus::speedValueToNormalized(1.9f)  == Catch::Approx(normFor(7)));   // -> 2.0
    CHECK(Gradus::speedValueToNormalized(2.6f)  == Catch::Approx(normFor(8)));   // -> 3.0

    // Out-of-range values snap to the extremes rather than the default index.
    CHECK(Gradus::speedValueToNormalized(-100.0f) == Catch::Approx(normFor(0)));
    CHECK(Gradus::speedValueToNormalized(1.0e9f)
          == Catch::Approx(normFor(Gradus::kLaneSpeedCount - 1)));
}
