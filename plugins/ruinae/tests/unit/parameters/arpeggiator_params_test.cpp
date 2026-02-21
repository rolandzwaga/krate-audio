// ==============================================================================
// Arpeggiator Parameter Tests (071-arp-engine-integration)
// ==============================================================================
// Tests for ArpeggiatorParams: denormalization, formatting, registration,
// serialization round-trip, and backward compatibility.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "parameters/arpeggiator_params.h"
#include "plugin_ids.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "pluginterfaces/vst/vsttypes.h"

#include <string>
#include <vector>

using Catch::Approx;
using namespace Steinberg;
using namespace Steinberg::Vst;

// ==============================================================================
// Helper: Convert String128 to std::string for comparison
// ==============================================================================
static std::string toString128(const Steinberg::Vst::String128& str128) {
    std::string result;
    for (int i = 0; i < 128 && str128[i] != 0; ++i) {
        result += static_cast<char>(str128[i]);
    }
    return result;
}

// ==============================================================================
// Phase 1: Struct Defaults
// ==============================================================================

TEST_CASE("ArpeggiatorParams struct has correct defaults", "[arp][params]") {
    Ruinae::ArpeggiatorParams params;
    REQUIRE(params.enabled.load() == false);
    REQUIRE(params.mode.load() == 0);
    REQUIRE(params.octaveRange.load() == 1);
    REQUIRE(params.octaveMode.load() == 0);
    REQUIRE(params.tempoSync.load() == true);
    REQUIRE(params.noteValue.load() == Ruinae::Parameters::kNoteValueDefaultIndex);
    REQUIRE(params.freeRate.load() == 4.0f);
    REQUIRE(params.gateLength.load() == 80.0f);
    REQUIRE(params.swing.load() == 0.0f);
    REQUIRE(params.latchMode.load() == 0);
    REQUIRE(params.retrigger.load() == 0);
}

// ==============================================================================
// T024: HandleParamChange - Denormalization for all 11 fields (FR-005, SC-002)
// ==============================================================================

TEST_CASE("ArpParams_HandleParamChange_AllFields", "[arp][params][denorm]") {
    using namespace Ruinae;
    ArpeggiatorParams params;

    SECTION("enabled: 0.0 -> false, 0.49 -> false, 0.5 -> true, 1.0 -> true") {
        handleArpParamChange(params, kArpEnabledId, 0.0);
        CHECK(params.enabled.load() == false);
        handleArpParamChange(params, kArpEnabledId, 0.49);
        CHECK(params.enabled.load() == false);
        handleArpParamChange(params, kArpEnabledId, 0.5);
        CHECK(params.enabled.load() == true);
        handleArpParamChange(params, kArpEnabledId, 1.0);
        CHECK(params.enabled.load() == true);
    }

    SECTION("mode: 0.0 -> 0 (Up), 1.0 -> 9 (Chord), mid -> 5 (Diverge)") {
        handleArpParamChange(params, kArpModeId, 0.0);
        CHECK(params.mode.load() == 0);
        handleArpParamChange(params, kArpModeId, 1.0);
        CHECK(params.mode.load() == 9);
        // 5/9 = 0.5556 -> 0.5556 * 9 + 0.5 = 5.5 -> 5
        handleArpParamChange(params, kArpModeId, 5.0 / 9.0);
        CHECK(params.mode.load() == 5);
    }

    SECTION("octaveRange: 0.0 -> 1, 1.0 -> 4, 0.333 -> 2, 0.667 -> 3") {
        handleArpParamChange(params, kArpOctaveRangeId, 0.0);
        CHECK(params.octaveRange.load() == 1);
        handleArpParamChange(params, kArpOctaveRangeId, 1.0);
        CHECK(params.octaveRange.load() == 4);
        // (1/3) * 3 = 1.0 -> round(1.0) = 1 -> 1+1 = 2
        handleArpParamChange(params, kArpOctaveRangeId, 1.0 / 3.0);
        CHECK(params.octaveRange.load() == 2);
        // (2/3) * 3 = 2.0 -> round(2.0) = 2 -> 1+2 = 3
        handleArpParamChange(params, kArpOctaveRangeId, 2.0 / 3.0);
        CHECK(params.octaveRange.load() == 3);
    }

    SECTION("octaveMode: 0.0 -> 0 (Sequential), 1.0 -> 1 (Interleaved)") {
        handleArpParamChange(params, kArpOctaveModeId, 0.0);
        CHECK(params.octaveMode.load() == 0);
        handleArpParamChange(params, kArpOctaveModeId, 1.0);
        CHECK(params.octaveMode.load() == 1);
    }

    SECTION("tempoSync: 0.0 -> false, 0.5 -> true, 1.0 -> true") {
        handleArpParamChange(params, kArpTempoSyncId, 0.0);
        CHECK(params.tempoSync.load() == false);
        handleArpParamChange(params, kArpTempoSyncId, 0.5);
        CHECK(params.tempoSync.load() == true);
        handleArpParamChange(params, kArpTempoSyncId, 1.0);
        CHECK(params.tempoSync.load() == true);
    }

    SECTION("noteValue: 0.0 -> 0, 1.0 -> 20, mid -> 10") {
        handleArpParamChange(params, kArpNoteValueId, 0.0);
        CHECK(params.noteValue.load() == 0);
        handleArpParamChange(params, kArpNoteValueId, 1.0);
        CHECK(params.noteValue.load() == 20);
        // 10/20 = 0.5 -> 0.5 * 20 + 0.5 = 10.5 -> 10
        handleArpParamChange(params, kArpNoteValueId, 0.5);
        CHECK(params.noteValue.load() == 10);
    }

    SECTION("freeRate: 0.0 -> 0.5 Hz, 1.0 -> 50.0 Hz, mid") {
        handleArpParamChange(params, kArpFreeRateId, 0.0);
        CHECK(params.freeRate.load() == Approx(0.5f).margin(0.01f));
        handleArpParamChange(params, kArpFreeRateId, 1.0);
        CHECK(params.freeRate.load() == Approx(50.0f).margin(0.01f));
        // 0.5 -> 0.5 + 0.5*49.5 = 25.25
        handleArpParamChange(params, kArpFreeRateId, 0.5);
        CHECK(params.freeRate.load() == Approx(25.25f).margin(0.01f));
    }

    SECTION("gateLength: 0.0 -> 1%, 1.0 -> 200%, mid -> 100.5%") {
        handleArpParamChange(params, kArpGateLengthId, 0.0);
        CHECK(params.gateLength.load() == Approx(1.0f).margin(0.01f));
        handleArpParamChange(params, kArpGateLengthId, 1.0);
        CHECK(params.gateLength.load() == Approx(200.0f).margin(0.01f));
        // 0.5 -> 1.0 + 0.5*199.0 = 100.5
        handleArpParamChange(params, kArpGateLengthId, 0.5);
        CHECK(params.gateLength.load() == Approx(100.5f).margin(0.01f));
    }

    SECTION("swing: 0.0 -> 0%, 1.0 -> 75%, mid -> 37.5%") {
        handleArpParamChange(params, kArpSwingId, 0.0);
        CHECK(params.swing.load() == Approx(0.0f).margin(0.01f));
        handleArpParamChange(params, kArpSwingId, 1.0);
        CHECK(params.swing.load() == Approx(75.0f).margin(0.01f));
        handleArpParamChange(params, kArpSwingId, 0.5);
        CHECK(params.swing.load() == Approx(37.5f).margin(0.01f));
    }

    SECTION("latchMode: 0.0 -> 0 (Off), 0.5 -> 1 (Hold), 1.0 -> 2 (Add)") {
        handleArpParamChange(params, kArpLatchModeId, 0.0);
        CHECK(params.latchMode.load() == 0);
        handleArpParamChange(params, kArpLatchModeId, 0.5);
        CHECK(params.latchMode.load() == 1);
        handleArpParamChange(params, kArpLatchModeId, 1.0);
        CHECK(params.latchMode.load() == 2);
    }

    SECTION("retrigger: 0.0 -> 0 (Off), 0.5 -> 1 (Note), 1.0 -> 2 (Beat)") {
        handleArpParamChange(params, kArpRetriggerId, 0.0);
        CHECK(params.retrigger.load() == 0);
        handleArpParamChange(params, kArpRetriggerId, 0.5);
        CHECK(params.retrigger.load() == 1);
        handleArpParamChange(params, kArpRetriggerId, 1.0);
        CHECK(params.retrigger.load() == 2);
    }
}

// ==============================================================================
// T025: FormatParam - Human-readable string output (FR-003)
// ==============================================================================

TEST_CASE("ArpParams_FormatParam_AllFields", "[arp][params][format]") {
    using namespace Ruinae;
    Steinberg::Vst::String128 string;

    SECTION("mode: 0.0 -> Up, 9/9=1.0 -> Chord") {
        auto result = formatArpParam(kArpModeId, 0.0, string);
        CHECK(result == Steinberg::kResultOk);
        CHECK(toString128(string) == "Up");

        result = formatArpParam(kArpModeId, 1.0, string);
        CHECK(result == Steinberg::kResultOk);
        CHECK(toString128(string) == "Chord");

        // 4/9 -> Converge (index 4)
        result = formatArpParam(kArpModeId, 4.0 / 9.0, string);
        CHECK(result == Steinberg::kResultOk);
        CHECK(toString128(string) == "Converge");
    }

    SECTION("octaveRange: 0.0 -> 1, 1.0 -> 4") {
        auto result = formatArpParam(kArpOctaveRangeId, 0.0, string);
        CHECK(result == Steinberg::kResultOk);
        CHECK(toString128(string) == "1");

        result = formatArpParam(kArpOctaveRangeId, 1.0, string);
        CHECK(result == Steinberg::kResultOk);
        CHECK(toString128(string) == "4");
    }

    SECTION("octaveMode: 0.0 -> Sequential, 1.0 -> Interleaved") {
        auto result = formatArpParam(kArpOctaveModeId, 0.0, string);
        CHECK(result == Steinberg::kResultOk);
        CHECK(toString128(string) == "Sequential");

        result = formatArpParam(kArpOctaveModeId, 1.0, string);
        CHECK(result == Steinberg::kResultOk);
        CHECK(toString128(string) == "Interleaved");
    }

    SECTION("noteValue: 0.0 -> 1/64T, 10/20=0.5 -> 1/8, 1.0 -> 1/1D") {
        auto result = formatArpParam(kArpNoteValueId, 0.0, string);
        CHECK(result == Steinberg::kResultOk);
        CHECK(toString128(string) == "1/64T");

        result = formatArpParam(kArpNoteValueId, 0.5, string);
        CHECK(result == Steinberg::kResultOk);
        CHECK(toString128(string) == "1/8");

        result = formatArpParam(kArpNoteValueId, 1.0, string);
        CHECK(result == Steinberg::kResultOk);
        CHECK(toString128(string) == "1/1D");
    }

    SECTION("freeRate: midpoint displays X.X Hz") {
        // 0.0 -> 0.5 Hz
        auto result = formatArpParam(kArpFreeRateId, 0.0, string);
        CHECK(result == Steinberg::kResultOk);
        CHECK(toString128(string) == "0.5 Hz");

        // 1.0 -> 50.0 Hz
        result = formatArpParam(kArpFreeRateId, 1.0, string);
        CHECK(result == Steinberg::kResultOk);
        CHECK(toString128(string) == "50.0 Hz");

        // Use a value that rounds unambiguously: 0.1 -> 0.5 + 0.1*49.5 = 5.45 -> "5.4 Hz" or "5.5 Hz"
        // Use a clean value instead: (4.0 - 0.5)/49.5 = normalized for 4.0 Hz
        double norm4Hz = (4.0 - 0.5) / 49.5;
        result = formatArpParam(kArpFreeRateId, norm4Hz, string);
        CHECK(result == Steinberg::kResultOk);
        CHECK(toString128(string) == "4.0 Hz");
    }

    SECTION("gateLength: 80% display") {
        // Normalized for 80%: (80 - 1) / 199 = 79/199 ~= 0.3970
        double norm80 = (80.0 - 1.0) / 199.0;
        auto result = formatArpParam(kArpGateLengthId, norm80, string);
        CHECK(result == Steinberg::kResultOk);
        CHECK(toString128(string) == "80%");
    }

    SECTION("swing: 0.0 -> 0%") {
        auto result = formatArpParam(kArpSwingId, 0.0, string);
        CHECK(result == Steinberg::kResultOk);
        CHECK(toString128(string) == "0%");

        result = formatArpParam(kArpSwingId, 1.0, string);
        CHECK(result == Steinberg::kResultOk);
        CHECK(toString128(string) == "75%");
    }

    SECTION("latchMode: 0.0 -> Off, 0.5 -> Hold, 1.0 -> Add") {
        auto result = formatArpParam(kArpLatchModeId, 0.0, string);
        CHECK(result == Steinberg::kResultOk);
        CHECK(toString128(string) == "Off");

        result = formatArpParam(kArpLatchModeId, 0.5, string);
        CHECK(result == Steinberg::kResultOk);
        CHECK(toString128(string) == "Hold");

        result = formatArpParam(kArpLatchModeId, 1.0, string);
        CHECK(result == Steinberg::kResultOk);
        CHECK(toString128(string) == "Add");
    }

    SECTION("retrigger: 0.0 -> Off, 0.5 -> Note, 1.0 -> Beat") {
        auto result = formatArpParam(kArpRetriggerId, 0.0, string);
        CHECK(result == Steinberg::kResultOk);
        CHECK(toString128(string) == "Off");

        result = formatArpParam(kArpRetriggerId, 0.5, string);
        CHECK(result == Steinberg::kResultOk);
        CHECK(toString128(string) == "Note");

        result = formatArpParam(kArpRetriggerId, 1.0, string);
        CHECK(result == Steinberg::kResultOk);
        CHECK(toString128(string) == "Beat");
    }

    SECTION("unknown param returns kResultFalse") {
        auto result = formatArpParam(9999, 0.5, string);
        CHECK(result == Steinberg::kResultFalse);
    }
}

// ==============================================================================
// T026: RegisterParams - All 11 IDs registered with kCanAutomate (FR-002)
// ==============================================================================

TEST_CASE("ArpParams_RegisterParams_AllPresent", "[arp][params][register]") {
    using namespace Ruinae;
    Steinberg::Vst::ParameterContainer container;

    registerArpParams(container);

    // All 11 parameter IDs that must be present
    constexpr ParamID expectedIds[] = {
        kArpEnabledId,
        kArpModeId,
        kArpOctaveRangeId,
        kArpOctaveModeId,
        kArpTempoSyncId,
        kArpNoteValueId,
        kArpFreeRateId,
        kArpGateLengthId,
        kArpSwingId,
        kArpLatchModeId,
        kArpRetriggerId,
    };

    for (auto id : expectedIds) {
        auto* param = container.getParameter(id);
        REQUIRE(param != nullptr);

        // Verify kCanAutomate flag is set
        ParameterInfo info = param->getInfo();
        CHECK((info.flags & ParameterInfo::kCanAutomate) != 0);
    }
}

// ==============================================================================
// T033: SaveLoad Round-Trip - All 11 fields (SC-003)
// ==============================================================================

TEST_CASE("ArpParams_SaveLoad_RoundTrip", "[arp][params][state]") {
    using namespace Ruinae;

    // Set all 11 fields to non-default values
    ArpeggiatorParams original;
    original.enabled.store(true, std::memory_order_relaxed);
    original.mode.store(3, std::memory_order_relaxed);            // DownUp
    original.octaveRange.store(3, std::memory_order_relaxed);     // 3 octaves
    original.octaveMode.store(1, std::memory_order_relaxed);      // Interleaved
    original.tempoSync.store(false, std::memory_order_relaxed);
    original.noteValue.store(14, std::memory_order_relaxed);      // 1/4D
    original.freeRate.store(12.5f, std::memory_order_relaxed);
    original.gateLength.store(60.0f, std::memory_order_relaxed);
    original.swing.store(25.0f, std::memory_order_relaxed);
    original.latchMode.store(1, std::memory_order_relaxed);       // Hold
    original.retrigger.store(2, std::memory_order_relaxed);       // Beat

    // Serialize to a memory stream
    auto stream = Steinberg::owned(new Steinberg::MemoryStream());
    {
        Steinberg::IBStreamer writeStream(stream, kLittleEndian);
        saveArpParams(original, writeStream);
    }

    // Deserialize to a fresh struct
    ArpeggiatorParams loaded;
    stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    {
        Steinberg::IBStreamer readStream(stream, kLittleEndian);
        bool ok = loadArpParams(loaded, readStream);
        REQUIRE(ok);
    }

    // Verify each field matches
    CHECK(loaded.enabled.load() == true);
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

// ==============================================================================
// T034: Backward Compatibility - Empty/truncated stream (FR-011)
// ==============================================================================

TEST_CASE("ArpParams_LoadArpParams_BackwardCompatibility", "[arp][params][state][compat]") {
    using namespace Ruinae;

    SECTION("empty stream returns false, params remain at defaults") {
        ArpeggiatorParams params;
        auto emptyStream = Steinberg::owned(new Steinberg::MemoryStream());
        {
            Steinberg::IBStreamer readStream(emptyStream, kLittleEndian);
            bool ok = loadArpParams(params, readStream);
            CHECK(ok == false);
        }

        // All fields remain at defaults
        CHECK(params.enabled.load() == false);
        CHECK(params.mode.load() == 0);
        CHECK(params.octaveRange.load() == 1);
        CHECK(params.octaveMode.load() == 0);
        CHECK(params.tempoSync.load() == true);
        CHECK(params.noteValue.load() == Parameters::kNoteValueDefaultIndex);
        CHECK(params.freeRate.load() == Approx(4.0f).margin(0.001f));
        CHECK(params.gateLength.load() == Approx(80.0f).margin(0.001f));
        CHECK(params.swing.load() == Approx(0.0f).margin(0.001f));
        CHECK(params.latchMode.load() == 0);
        CHECK(params.retrigger.load() == 0);
    }

    SECTION("truncated stream (only 3 fields) returns false after partial read") {
        // Write only 3 fields: enabled, mode, octaveRange
        auto stream = Steinberg::owned(new Steinberg::MemoryStream());
        {
            Steinberg::IBStreamer writeStream(stream, kLittleEndian);
            writeStream.writeInt32(1);  // enabled = true
            writeStream.writeInt32(5);  // mode = Diverge
            writeStream.writeInt32(2);  // octaveRange = 2
        }

        ArpeggiatorParams params;
        stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
        {
            Steinberg::IBStreamer readStream(stream, kLittleEndian);
            bool ok = loadArpParams(params, readStream);
            // Should return false because stream is truncated
            CHECK(ok == false);
        }

        // The first 3 fields that were read successfully are stored
        CHECK(params.enabled.load() == true);
        CHECK(params.mode.load() == 5);
        CHECK(params.octaveRange.load() == 2);
        // Remaining fields stay at their defaults (octaveMode onward was not read)
        CHECK(params.octaveMode.load() == 0);
        CHECK(params.tempoSync.load() == true);
    }
}

// ==============================================================================
// T035: LoadToController - Normalized values (FR-012)
// ==============================================================================

TEST_CASE("ArpParams_LoadToController_NormalizesCorrectly", "[arp][params][state][controller]") {
    using namespace Ruinae;

    // Write known plain values to stream
    auto stream = Steinberg::owned(new Steinberg::MemoryStream());
    {
        Steinberg::IBStreamer writeStream(stream, kLittleEndian);
        writeStream.writeInt32(1);    // enabled = true
        writeStream.writeInt32(3);    // mode = DownUp (index 3)
        writeStream.writeInt32(3);    // octaveRange = 3
        writeStream.writeInt32(1);    // octaveMode = Interleaved
        writeStream.writeInt32(0);    // tempoSync = false
        writeStream.writeInt32(14);   // noteValue = index 14 (1/4D)
        writeStream.writeFloat(12.5f);// freeRate = 12.5 Hz
        writeStream.writeFloat(60.0f);// gateLength = 60%
        writeStream.writeFloat(25.0f);// swing = 25%
        writeStream.writeInt32(1);    // latchMode = Hold
        writeStream.writeInt32(2);    // retrigger = Beat
    }

    // Capture setParam calls
    struct ParamCall {
        Steinberg::Vst::ParamID id;
        double value;
    };
    std::vector<ParamCall> calls;

    auto setParam = [&calls](Steinberg::Vst::ParamID id, double value) {
        calls.push_back({id, value});
    };

    stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    {
        Steinberg::IBStreamer readStream(stream, kLittleEndian);
        loadArpParamsToController(readStream, setParam);
    }

    // Should have 11 calls
    REQUIRE(calls.size() == 11);

    // Verify each normalized value
    // enabled: true -> 1.0
    CHECK(calls[0].id == kArpEnabledId);
    CHECK(calls[0].value == Approx(1.0).margin(0.001));

    // mode: 3 -> 3/9 = 0.3333
    CHECK(calls[1].id == kArpModeId);
    CHECK(calls[1].value == Approx(3.0 / 9.0).margin(0.001));

    // octaveRange: 3 -> (3-1)/3 = 0.6667
    CHECK(calls[2].id == kArpOctaveRangeId);
    CHECK(calls[2].value == Approx(2.0 / 3.0).margin(0.001));

    // octaveMode: 1 -> 1.0
    CHECK(calls[3].id == kArpOctaveModeId);
    CHECK(calls[3].value == Approx(1.0).margin(0.001));

    // tempoSync: false -> 0.0
    CHECK(calls[4].id == kArpTempoSyncId);
    CHECK(calls[4].value == Approx(0.0).margin(0.001));

    // noteValue: 14 -> 14/20 = 0.7
    CHECK(calls[5].id == kArpNoteValueId);
    CHECK(calls[5].value == Approx(14.0 / 20.0).margin(0.001));

    // freeRate: 12.5 -> (12.5 - 0.5) / 49.5 = 12.0/49.5 ~= 0.2424
    CHECK(calls[6].id == kArpFreeRateId);
    CHECK(calls[6].value == Approx((12.5 - 0.5) / 49.5).margin(0.001));

    // gateLength: 60.0 -> (60.0 - 1.0) / 199.0 = 59.0/199.0 ~= 0.2965
    CHECK(calls[7].id == kArpGateLengthId);
    CHECK(calls[7].value == Approx((60.0 - 1.0) / 199.0).margin(0.001));

    // swing: 25.0 -> 25.0 / 75.0 ~= 0.3333
    CHECK(calls[8].id == kArpSwingId);
    CHECK(calls[8].value == Approx(25.0 / 75.0).margin(0.001));

    // latchMode: 1 -> 1/2 = 0.5
    CHECK(calls[9].id == kArpLatchModeId);
    CHECK(calls[9].value == Approx(0.5).margin(0.001));

    // retrigger: 2 -> 2/2 = 1.0
    CHECK(calls[10].id == kArpRetriggerId);
    CHECK(calls[10].value == Approx(1.0).margin(0.001));
}
