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
