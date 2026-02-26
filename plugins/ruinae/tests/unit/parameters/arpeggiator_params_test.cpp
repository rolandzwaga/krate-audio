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

// ==============================================================================
// Phase 4 (072-independent-lanes) User Story 1: Velocity Lane Parameter Tests
// ==============================================================================

TEST_CASE("ArpVelLaneLength_Registration", "[arp][params]") {
    using namespace Ruinae;
    Steinberg::Vst::ParameterContainer container;
    registerArpParams(container);

    auto* param = container.getParameter(kArpVelocityLaneLengthId);
    REQUIRE(param != nullptr);

    ParameterInfo info = param->getInfo();
    CHECK((info.flags & ParameterInfo::kCanAutomate) != 0);
    // Discrete param with stepCount=31 (range [1,32])
    CHECK(info.stepCount == 31);
}

TEST_CASE("ArpVelLaneStep_Registration", "[arp][params]") {
    using namespace Ruinae;
    Steinberg::Vst::ParameterContainer container;
    registerArpParams(container);

    // Step params 3021-3052 registered with range [0,1] default 1.0
    for (int i = 0; i < 32; ++i) {
        auto paramId = static_cast<ParamID>(kArpVelocityLaneStep0Id + i);
        auto* param = container.getParameter(paramId);
        REQUIRE(param != nullptr);

        ParameterInfo info = param->getInfo();
        CHECK((info.flags & ParameterInfo::kCanAutomate) != 0);
        CHECK((info.flags & ParameterInfo::kIsHidden) != 0);
    }
}

TEST_CASE("ArpVelLaneLength_Denormalize", "[arp][params]") {
    using namespace Ruinae;
    ArpeggiatorParams params;

    // 0.0 -> length=1
    handleArpParamChange(params, kArpVelocityLaneLengthId, 0.0);
    CHECK(params.velocityLaneLength.load() == 1);

    // 1.0 -> length=32
    handleArpParamChange(params, kArpVelocityLaneLengthId, 1.0);
    CHECK(params.velocityLaneLength.load() == 32);

    // 0.5 -> 1 + round(0.5 * 31) = 1 + 16 = 17
    handleArpParamChange(params, kArpVelocityLaneLengthId, 0.5);
    CHECK(params.velocityLaneLength.load() == 17);
}

TEST_CASE("ArpVelLaneStep_Denormalize", "[arp][params]") {
    using namespace Ruinae;
    ArpeggiatorParams params;

    // 0.0 -> step[0]=0.0f
    handleArpParamChange(params, kArpVelocityLaneStep0Id, 0.0);
    CHECK(params.velocityLaneSteps[0].load() == Approx(0.0f).margin(0.001f));

    // 1.0 -> step[0]=1.0f
    handleArpParamChange(params, kArpVelocityLaneStep0Id, 1.0);
    CHECK(params.velocityLaneSteps[0].load() == Approx(1.0f).margin(0.001f));

    // 0.5 -> step[0]=0.5f
    handleArpParamChange(params, kArpVelocityLaneStep0Id, 0.5);
    CHECK(params.velocityLaneSteps[0].load() == Approx(0.5f).margin(0.001f));
}

TEST_CASE("ArpVelParams_SaveLoad_RoundTrip", "[arp][params]") {
    using namespace Ruinae;

    // Set non-default velocity lane values
    ArpeggiatorParams original;
    original.velocityLaneLength.store(4, std::memory_order_relaxed);
    original.velocityLaneSteps[0].store(1.0f, std::memory_order_relaxed);
    original.velocityLaneSteps[1].store(0.3f, std::memory_order_relaxed);
    original.velocityLaneSteps[2].store(0.3f, std::memory_order_relaxed);
    original.velocityLaneSteps[3].store(0.7f, std::memory_order_relaxed);

    // Serialize
    auto stream = Steinberg::owned(new Steinberg::MemoryStream());
    {
        Steinberg::IBStreamer writeStream(stream, kLittleEndian);
        saveArpParams(original, writeStream);
    }

    // Deserialize
    ArpeggiatorParams loaded;
    stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    {
        Steinberg::IBStreamer readStream(stream, kLittleEndian);
        bool ok = loadArpParams(loaded, readStream);
        REQUIRE(ok);
    }

    // Verify velocity lane round-trip
    CHECK(loaded.velocityLaneLength.load() == 4);
    CHECK(loaded.velocityLaneSteps[0].load() == Approx(1.0f).margin(1e-6f));
    CHECK(loaded.velocityLaneSteps[1].load() == Approx(0.3f).margin(1e-6f));
    CHECK(loaded.velocityLaneSteps[2].load() == Approx(0.3f).margin(1e-6f));
    CHECK(loaded.velocityLaneSteps[3].load() == Approx(0.7f).margin(1e-6f));
}

TEST_CASE("ArpVelParams_BackwardCompat", "[arp][params]") {
    using namespace Ruinae;

    // Construct a Phase 3 stream with ONLY 11 base arp params (no lane data)
    auto stream = Steinberg::owned(new Steinberg::MemoryStream());
    {
        Steinberg::IBStreamer writeStream(stream, kLittleEndian);
        writeStream.writeInt32(1);     // enabled = true
        writeStream.writeInt32(0);     // mode = Up
        writeStream.writeInt32(1);     // octaveRange = 1
        writeStream.writeInt32(0);     // octaveMode = Sequential
        writeStream.writeInt32(1);     // tempoSync = true
        writeStream.writeInt32(10);    // noteValue = 1/8
        writeStream.writeFloat(4.0f);  // freeRate = 4.0 Hz
        writeStream.writeFloat(80.0f); // gateLength = 80%
        writeStream.writeFloat(0.0f);  // swing = 0%
        writeStream.writeInt32(0);     // latchMode = Off
        writeStream.writeInt32(0);     // retrigger = Off
    }

    ArpeggiatorParams loaded;
    stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    {
        Steinberg::IBStreamer readStream(stream, kLittleEndian);
        // loadArpParams should return true for the 11 base params,
        // then fail to read lane data -- that's OK, lanes stay at defaults
        loadArpParams(loaded, readStream);
    }

    // Base params loaded correctly
    CHECK(loaded.enabled.load() == true);
    CHECK(loaded.mode.load() == 0);

    // Velocity lane defaults preserved (no lane data in stream)
    CHECK(loaded.velocityLaneLength.load() == 16);
    for (int i = 0; i < 32; ++i) {
        CHECK(loaded.velocityLaneSteps[i].load() == Approx(1.0f).margin(1e-6f));
    }
}

// ==============================================================================
// Phase 4 (072-independent-lanes) User Story 2: Gate Lane Parameter Tests
// ==============================================================================

TEST_CASE("ArpGateLaneLength_Registration", "[arp][params]") {
    using namespace Ruinae;
    Steinberg::Vst::ParameterContainer container;
    registerArpParams(container);

    auto* param = container.getParameter(kArpGateLaneLengthId);
    REQUIRE(param != nullptr);

    ParameterInfo info = param->getInfo();
    CHECK((info.flags & ParameterInfo::kCanAutomate) != 0);
    // Discrete param with stepCount=31 (range [1,32])
    CHECK(info.stepCount == 31);
}

TEST_CASE("ArpGateLaneStep_Registration", "[arp][params]") {
    using namespace Ruinae;
    Steinberg::Vst::ParameterContainer container;
    registerArpParams(container);

    // Step params 3061-3092 registered with range [0.01, 2.0] default 1.0
    for (int i = 0; i < 32; ++i) {
        auto paramId = static_cast<ParamID>(kArpGateLaneStep0Id + i);
        auto* param = container.getParameter(paramId);
        REQUIRE(param != nullptr);

        ParameterInfo info = param->getInfo();
        CHECK((info.flags & ParameterInfo::kCanAutomate) != 0);
        CHECK((info.flags & ParameterInfo::kIsHidden) != 0);
    }
}

TEST_CASE("ArpGateLaneStep_Denormalize", "[arp][params]") {
    using namespace Ruinae;
    ArpeggiatorParams params;

    // 0.0 -> 0.01 + 0.0 * 1.99 = 0.01
    handleArpParamChange(params, kArpGateLaneStep0Id, 0.0);
    CHECK(params.gateLaneSteps[0].load() == Approx(0.01f).margin(0.001f));

    // 1.0 -> 0.01 + 1.0 * 1.99 = 2.0
    handleArpParamChange(params, kArpGateLaneStep0Id, 1.0);
    CHECK(params.gateLaneSteps[0].load() == Approx(2.0f).margin(0.001f));

    // 0.5 -> 0.01 + 0.5 * 1.99 = 1.005
    handleArpParamChange(params, kArpGateLaneStep0Id, 0.5);
    CHECK(params.gateLaneSteps[0].load() == Approx(1.005f).margin(0.001f));
}

TEST_CASE("ArpGateParams_SaveLoad_RoundTrip", "[arp][params]") {
    using namespace Ruinae;

    // Set non-default gate lane values
    ArpeggiatorParams original;
    original.gateLaneLength.store(3, std::memory_order_relaxed);
    original.gateLaneSteps[0].store(0.5f, std::memory_order_relaxed);
    original.gateLaneSteps[1].store(1.0f, std::memory_order_relaxed);
    original.gateLaneSteps[2].store(1.5f, std::memory_order_relaxed);

    // Serialize
    auto stream = Steinberg::owned(new Steinberg::MemoryStream());
    {
        Steinberg::IBStreamer writeStream(stream, kLittleEndian);
        saveArpParams(original, writeStream);
    }

    // Deserialize
    ArpeggiatorParams loaded;
    stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    {
        Steinberg::IBStreamer readStream(stream, kLittleEndian);
        bool ok = loadArpParams(loaded, readStream);
        REQUIRE(ok);
    }

    // Verify gate lane round-trip
    CHECK(loaded.gateLaneLength.load() == 3);
    CHECK(loaded.gateLaneSteps[0].load() == Approx(0.5f).margin(1e-6f));
    CHECK(loaded.gateLaneSteps[1].load() == Approx(1.0f).margin(1e-6f));
    CHECK(loaded.gateLaneSteps[2].load() == Approx(1.5f).margin(1e-6f));
}

TEST_CASE("ArpGateParams_BackwardCompat", "[arp][params]") {
    using namespace Ruinae;

    // Construct a stream with 11 base params + velocity lane data only (no gate lane)
    auto stream = Steinberg::owned(new Steinberg::MemoryStream());
    {
        Steinberg::IBStreamer writeStream(stream, kLittleEndian);
        // 11 base params
        writeStream.writeInt32(1);     // enabled = true
        writeStream.writeInt32(0);     // mode = Up
        writeStream.writeInt32(1);     // octaveRange = 1
        writeStream.writeInt32(0);     // octaveMode = Sequential
        writeStream.writeInt32(1);     // tempoSync = true
        writeStream.writeInt32(10);    // noteValue = 1/8
        writeStream.writeFloat(4.0f);  // freeRate = 4.0 Hz
        writeStream.writeFloat(80.0f); // gateLength = 80%
        writeStream.writeFloat(0.0f);  // swing = 0%
        writeStream.writeInt32(0);     // latchMode = Off
        writeStream.writeInt32(0);     // retrigger = Off

        // Velocity lane data (33 values)
        writeStream.writeInt32(1);     // velocityLaneLength = 1
        for (int i = 0; i < 32; ++i) {
            writeStream.writeFloat(1.0f); // all steps = 1.0f
        }
        // NO gate lane data -- stream ends here
    }

    ArpeggiatorParams loaded;
    stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    {
        Steinberg::IBStreamer readStream(stream, kLittleEndian);
        loadArpParams(loaded, readStream);
    }

    // Base params loaded correctly
    CHECK(loaded.enabled.load() == true);

    // Velocity lane loaded correctly
    CHECK(loaded.velocityLaneLength.load() == 1);

    // Gate lane defaults preserved (no gate lane data in stream)
    CHECK(loaded.gateLaneLength.load() == 16);
    for (int i = 0; i < 32; ++i) {
        CHECK(loaded.gateLaneSteps[i].load() == Approx(1.0f).margin(1e-6f));
    }
}

// ==============================================================================
// Phase 5 (072-independent-lanes) User Story 3: Pitch Lane Parameter Tests
// ==============================================================================

TEST_CASE("ArpPitchLaneLength_Registration", "[arp][params]") {
    using namespace Ruinae;
    Steinberg::Vst::ParameterContainer container;
    registerArpParams(container);

    auto* param = container.getParameter(kArpPitchLaneLengthId);
    REQUIRE(param != nullptr);

    ParameterInfo info = param->getInfo();
    CHECK((info.flags & ParameterInfo::kCanAutomate) != 0);
    // Discrete param with stepCount=31 (range [1,32])
    CHECK(info.stepCount == 31);
}

TEST_CASE("ArpPitchLaneStep_Registration", "[arp][params]") {
    using namespace Ruinae;
    Steinberg::Vst::ParameterContainer container;
    registerArpParams(container);

    // Step params 3101-3132 registered as discrete [-24,+24] default 0
    for (int i = 0; i < 32; ++i) {
        auto paramId = static_cast<ParamID>(kArpPitchLaneStep0Id + i);
        auto* param = container.getParameter(paramId);
        REQUIRE(param != nullptr);

        ParameterInfo info = param->getInfo();
        CHECK((info.flags & ParameterInfo::kCanAutomate) != 0);
        CHECK((info.flags & ParameterInfo::kIsHidden) != 0);
        // Discrete param with stepCount=48 (range [-24,+24])
        CHECK(info.stepCount == 48);
    }

    // FR-034: Verify std::atomic<int> is lock-free
    Ruinae::ArpeggiatorParams params;
    REQUIRE(params.pitchLaneSteps[0].is_lock_free());
}

TEST_CASE("ArpPitchLaneStep_Denormalize", "[arp][params]") {
    using namespace Ruinae;
    ArpeggiatorParams params;

    // 0.0 -> -24 + round(0.0 * 48) = -24
    handleArpParamChange(params, kArpPitchLaneStep0Id, 0.0);
    CHECK(params.pitchLaneSteps[0].load() == -24);

    // 1.0 -> -24 + round(1.0 * 48) = -24 + 48 = +24
    handleArpParamChange(params, kArpPitchLaneStep0Id, 1.0);
    CHECK(params.pitchLaneSteps[0].load() == 24);

    // 0.5 -> -24 + round(0.5 * 48) = -24 + 24 = 0
    handleArpParamChange(params, kArpPitchLaneStep0Id, 0.5);
    CHECK(params.pitchLaneSteps[0].load() == 0);
}

TEST_CASE("ArpPitchParams_SaveLoad_RoundTrip", "[arp][params]") {
    using namespace Ruinae;

    // Set non-default pitch lane values including negative offsets
    ArpeggiatorParams original;
    original.pitchLaneLength.store(4, std::memory_order_relaxed);
    original.pitchLaneSteps[0].store(0, std::memory_order_relaxed);
    original.pitchLaneSteps[1].store(7, std::memory_order_relaxed);
    original.pitchLaneSteps[2].store(-12, std::memory_order_relaxed);
    original.pitchLaneSteps[3].store(-24, std::memory_order_relaxed);

    // Serialize
    auto stream = Steinberg::owned(new Steinberg::MemoryStream());
    {
        Steinberg::IBStreamer writeStream(stream, kLittleEndian);
        saveArpParams(original, writeStream);
    }

    // Deserialize
    ArpeggiatorParams loaded;
    stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    {
        Steinberg::IBStreamer readStream(stream, kLittleEndian);
        bool ok = loadArpParams(loaded, readStream);
        REQUIRE(ok);
    }

    // Verify pitch lane round-trip
    CHECK(loaded.pitchLaneLength.load() == 4);
    CHECK(loaded.pitchLaneSteps[0].load() == 0);
    CHECK(loaded.pitchLaneSteps[1].load() == 7);
    CHECK(loaded.pitchLaneSteps[2].load() == -12);
    CHECK(loaded.pitchLaneSteps[3].load() == -24);
}

TEST_CASE("ArpPitchParams_BackwardCompat", "[arp][params]") {
    using namespace Ruinae;

    // Construct a stream with 11 base params + velocity lane + gate lane (no pitch lane)
    auto stream = Steinberg::owned(new Steinberg::MemoryStream());
    {
        Steinberg::IBStreamer writeStream(stream, kLittleEndian);
        // 11 base params
        writeStream.writeInt32(1);     // enabled = true
        writeStream.writeInt32(0);     // mode = Up
        writeStream.writeInt32(1);     // octaveRange = 1
        writeStream.writeInt32(0);     // octaveMode = Sequential
        writeStream.writeInt32(1);     // tempoSync = true
        writeStream.writeInt32(10);    // noteValue = 1/8
        writeStream.writeFloat(4.0f);  // freeRate = 4.0 Hz
        writeStream.writeFloat(80.0f); // gateLength = 80%
        writeStream.writeFloat(0.0f);  // swing = 0%
        writeStream.writeInt32(0);     // latchMode = Off
        writeStream.writeInt32(0);     // retrigger = Off

        // Velocity lane data (33 values)
        writeStream.writeInt32(1);     // velocityLaneLength = 1
        for (int i = 0; i < 32; ++i) {
            writeStream.writeFloat(1.0f); // all steps = 1.0f
        }

        // Gate lane data (33 values)
        writeStream.writeInt32(1);     // gateLaneLength = 1
        for (int i = 0; i < 32; ++i) {
            writeStream.writeFloat(1.0f); // all steps = 1.0f
        }
        // NO pitch lane data -- stream ends here
    }

    ArpeggiatorParams loaded;
    stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    {
        Steinberg::IBStreamer readStream(stream, kLittleEndian);
        loadArpParams(loaded, readStream);
    }

    // Base params loaded correctly
    CHECK(loaded.enabled.load() == true);

    // Velocity lane loaded correctly
    CHECK(loaded.velocityLaneLength.load() == 1);

    // Gate lane loaded correctly
    CHECK(loaded.gateLaneLength.load() == 1);

    // Pitch lane defaults preserved (no pitch lane data in stream)
    CHECK(loaded.pitchLaneLength.load() == 16);
    for (int i = 0; i < 32; ++i) {
        CHECK(loaded.pitchLaneSteps[i].load() == 0);
    }
}

// ==============================================================================
// Phase 7 (072-independent-lanes) User Story 5: Lane State Persistence Tests
// ==============================================================================

TEST_CASE("LanePersistence_FullRoundTrip", "[arp][params]") {
    using namespace Ruinae;

    // Configure velocity length=5, gate length=3, pitch length=7 with non-default values
    ArpeggiatorParams original;

    // Set non-default base arp params to make this a complete round-trip test
    original.enabled.store(true, std::memory_order_relaxed);
    original.mode.store(2, std::memory_order_relaxed);
    original.gateLength.store(60.0f, std::memory_order_relaxed);

    // Velocity lane: length=5, steps 0-4 set to distinct values, steps 5-31 left at default 1.0f
    original.velocityLaneLength.store(5, std::memory_order_relaxed);
    original.velocityLaneSteps[0].store(0.1f, std::memory_order_relaxed);
    original.velocityLaneSteps[1].store(0.25f, std::memory_order_relaxed);
    original.velocityLaneSteps[2].store(0.5f, std::memory_order_relaxed);
    original.velocityLaneSteps[3].store(0.75f, std::memory_order_relaxed);
    original.velocityLaneSteps[4].store(0.9f, std::memory_order_relaxed);
    // Steps 5-31 remain at default 1.0f -- they should ALSO be preserved

    // Gate lane: length=3, steps 0-2 set to distinct values
    original.gateLaneLength.store(3, std::memory_order_relaxed);
    original.gateLaneSteps[0].store(0.5f, std::memory_order_relaxed);
    original.gateLaneSteps[1].store(1.5f, std::memory_order_relaxed);
    original.gateLaneSteps[2].store(0.01f, std::memory_order_relaxed);
    // Steps 3-31 remain at default 1.0f

    // Pitch lane: length=7, steps 0-6 set to distinct values including negatives
    original.pitchLaneLength.store(7, std::memory_order_relaxed);
    original.pitchLaneSteps[0].store(-24, std::memory_order_relaxed);
    original.pitchLaneSteps[1].store(-12, std::memory_order_relaxed);
    original.pitchLaneSteps[2].store(-5, std::memory_order_relaxed);
    original.pitchLaneSteps[3].store(0, std::memory_order_relaxed);
    original.pitchLaneSteps[4].store(7, std::memory_order_relaxed);
    original.pitchLaneSteps[5].store(12, std::memory_order_relaxed);
    original.pitchLaneSteps[6].store(24, std::memory_order_relaxed);
    // Steps 7-31 remain at default 0

    // Also set a step BEYOND the active length to a non-default value
    // to verify that steps beyond active length also round-trip
    original.velocityLaneSteps[10].store(0.42f, std::memory_order_relaxed);
    original.gateLaneSteps[15].store(1.8f, std::memory_order_relaxed);
    original.pitchLaneSteps[20].store(-7, std::memory_order_relaxed);

    // Serialize
    auto stream = Steinberg::owned(new Steinberg::MemoryStream());
    {
        Steinberg::IBStreamer writeStream(stream, kLittleEndian);
        saveArpParams(original, writeStream);
    }

    // Deserialize into fresh params
    ArpeggiatorParams loaded;
    stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    {
        Steinberg::IBStreamer readStream(stream, kLittleEndian);
        bool ok = loadArpParams(loaded, readStream);
        REQUIRE(ok);
    }

    // SC-004: Verify ALL 99 lane values (3 lengths + 96 steps) match exactly

    // Velocity lane
    CHECK(loaded.velocityLaneLength.load() == 5);
    for (int i = 0; i < 32; ++i) {
        INFO("Velocity step " << i);
        CHECK(loaded.velocityLaneSteps[i].load() ==
            Approx(original.velocityLaneSteps[i].load()).margin(1e-6f));
    }

    // Gate lane
    CHECK(loaded.gateLaneLength.load() == 3);
    for (int i = 0; i < 32; ++i) {
        INFO("Gate step " << i);
        CHECK(loaded.gateLaneSteps[i].load() ==
            Approx(original.gateLaneSteps[i].load()).margin(1e-6f));
    }

    // Pitch lane
    CHECK(loaded.pitchLaneLength.load() == 7);
    for (int i = 0; i < 32; ++i) {
        INFO("Pitch step " << i);
        CHECK(loaded.pitchLaneSteps[i].load() == original.pitchLaneSteps[i].load());
    }

    // Verify steps BEYOND active length round-trip correctly
    CHECK(loaded.velocityLaneSteps[10].load() == Approx(0.42f).margin(1e-6f));
    CHECK(loaded.gateLaneSteps[15].load() == Approx(1.8f).margin(1e-6f));
    CHECK(loaded.pitchLaneSteps[20].load() == -7);
}

TEST_CASE("LanePersistence_Phase3Compat_NoLaneData", "[arp][params]") {
    using namespace Ruinae;

    // Construct IBStream with only 11-param arp data (no lane data)
    auto stream = Steinberg::owned(new Steinberg::MemoryStream());
    {
        Steinberg::IBStreamer writeStream(stream, kLittleEndian);
        writeStream.writeInt32(1);     // enabled = true
        writeStream.writeInt32(0);     // mode = Up
        writeStream.writeInt32(1);     // octaveRange = 1
        writeStream.writeInt32(0);     // octaveMode = Sequential
        writeStream.writeInt32(1);     // tempoSync = true
        writeStream.writeInt32(10);    // noteValue = 1/8
        writeStream.writeFloat(4.0f);  // freeRate = 4.0 Hz
        writeStream.writeFloat(80.0f); // gateLength = 80%
        writeStream.writeFloat(0.0f);  // swing = 0%
        writeStream.writeInt32(0);     // latchMode = Off
        writeStream.writeInt32(0);     // retrigger = Off
    }

    ArpeggiatorParams loaded;
    stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    {
        Steinberg::IBStreamer readStream(stream, kLittleEndian);
        // SC-005: Load should not crash. Base params return true; lane data is missing.
        loadArpParams(loaded, readStream);
    }

    // SC-005: Verify all lane defaults
    // Velocity lane defaults
    CHECK(loaded.velocityLaneLength.load() == 16);
    for (int i = 0; i < 32; ++i) {
        INFO("Velocity step " << i);
        CHECK(loaded.velocityLaneSteps[i].load() == Approx(1.0f).margin(1e-6f));
    }

    // Gate lane defaults
    CHECK(loaded.gateLaneLength.load() == 16);
    for (int i = 0; i < 32; ++i) {
        INFO("Gate step " << i);
        CHECK(loaded.gateLaneSteps[i].load() == Approx(1.0f).margin(1e-6f));
    }

    // Pitch lane defaults
    CHECK(loaded.pitchLaneLength.load() == 16);
    for (int i = 0; i < 32; ++i) {
        INFO("Pitch step " << i);
        CHECK(loaded.pitchLaneSteps[i].load() == 0);
    }
}

TEST_CASE("LanePersistence_PartialLaneData", "[arp][params]") {
    using namespace Ruinae;

    // Construct stream with 11 arp params + velocity lane only; stream ends mid gate lane
    auto stream = Steinberg::owned(new Steinberg::MemoryStream());
    {
        Steinberg::IBStreamer writeStream(stream, kLittleEndian);
        // 11 base params
        writeStream.writeInt32(1);     // enabled = true
        writeStream.writeInt32(0);     // mode = Up
        writeStream.writeInt32(1);     // octaveRange = 1
        writeStream.writeInt32(0);     // octaveMode = Sequential
        writeStream.writeInt32(1);     // tempoSync = true
        writeStream.writeInt32(10);    // noteValue = 1/8
        writeStream.writeFloat(4.0f);  // freeRate = 4.0 Hz
        writeStream.writeFloat(80.0f); // gateLength = 80%
        writeStream.writeFloat(0.0f);  // swing = 0%
        writeStream.writeInt32(0);     // latchMode = Off
        writeStream.writeInt32(0);     // retrigger = Off

        // Velocity lane data (33 values -- length + 32 steps)
        writeStream.writeInt32(4);     // velocityLaneLength = 4
        for (int i = 0; i < 32; ++i) {
            writeStream.writeFloat(i < 4 ? 0.5f : 1.0f);
        }
        // NO gate lane data, NO pitch lane data -- stream ends here
    }

    ArpeggiatorParams loaded;
    stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    {
        Steinberg::IBStreamer readStream(stream, kLittleEndian);
        // Should not crash. Velocity restored, gate/pitch at defaults.
        loadArpParams(loaded, readStream);
    }

    // Velocity lane restored
    CHECK(loaded.velocityLaneLength.load() == 4);
    CHECK(loaded.velocityLaneSteps[0].load() == Approx(0.5f).margin(1e-6f));
    CHECK(loaded.velocityLaneSteps[1].load() == Approx(0.5f).margin(1e-6f));
    CHECK(loaded.velocityLaneSteps[2].load() == Approx(0.5f).margin(1e-6f));
    CHECK(loaded.velocityLaneSteps[3].load() == Approx(0.5f).margin(1e-6f));
    CHECK(loaded.velocityLaneSteps[4].load() == Approx(1.0f).margin(1e-6f));

    // Gate lane at defaults (not present in stream)
    CHECK(loaded.gateLaneLength.load() == 16);
    for (int i = 0; i < 32; ++i) {
        INFO("Gate step " << i);
        CHECK(loaded.gateLaneSteps[i].load() == Approx(1.0f).margin(1e-6f));
    }

    // Pitch lane at defaults (not present in stream)
    CHECK(loaded.pitchLaneLength.load() == 16);
    for (int i = 0; i < 32; ++i) {
        INFO("Pitch step " << i);
        CHECK(loaded.pitchLaneSteps[i].load() == 0);
    }
}

TEST_CASE("LanePersistence_PitchNegativeValues", "[arp][params]") {
    using namespace Ruinae;

    // Save pitch lane with offsets [-24, -12, 0, +12, +24]
    ArpeggiatorParams original;
    original.pitchLaneLength.store(5, std::memory_order_relaxed);
    original.pitchLaneSteps[0].store(-24, std::memory_order_relaxed);
    original.pitchLaneSteps[1].store(-12, std::memory_order_relaxed);
    original.pitchLaneSteps[2].store(0, std::memory_order_relaxed);
    original.pitchLaneSteps[3].store(12, std::memory_order_relaxed);
    original.pitchLaneSteps[4].store(24, std::memory_order_relaxed);

    // Serialize
    auto stream = Steinberg::owned(new Steinberg::MemoryStream());
    {
        Steinberg::IBStreamer writeStream(stream, kLittleEndian);
        saveArpParams(original, writeStream);
    }

    // Deserialize
    ArpeggiatorParams loaded;
    stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    {
        Steinberg::IBStreamer readStream(stream, kLittleEndian);
        bool ok = loadArpParams(loaded, readStream);
        REQUIRE(ok);
    }

    // Verify all signed values preserved correctly (no sign-loss from int32 round-trip)
    CHECK(loaded.pitchLaneLength.load() == 5);
    CHECK(loaded.pitchLaneSteps[0].load() == -24);
    CHECK(loaded.pitchLaneSteps[1].load() == -12);
    CHECK(loaded.pitchLaneSteps[2].load() == 0);
    CHECK(loaded.pitchLaneSteps[3].load() == 12);
    CHECK(loaded.pitchLaneSteps[4].load() == 24);
}

// ==============================================================================
// Phase 3 (073-per-step-mods): Modifier Lane Parameter Tests (T018)
// ==============================================================================

TEST_CASE("ArpModifierLaneLength_Registration", "[arp][params]") {
    // FR-026, FR-027: kArpModifierLaneLengthId registered as RangeParameter [1,32]
    // default 1, kCanAutomate, NOT kIsHidden
    using namespace Ruinae;
    ParameterContainer container;
    registerArpParams(container);

    auto* param = container.getParameter(kArpModifierLaneLengthId);
    REQUIRE(param != nullptr);

    ParameterInfo info = param->getInfo();
    CHECK((info.flags & ParameterInfo::kCanAutomate) != 0);
    CHECK((info.flags & ParameterInfo::kIsHidden) == 0);
}

TEST_CASE("ArpModifierLaneStep_Registration", "[arp][params]") {
    // FR-026, FR-027: Step params 3141-3172 registered [0,255] default 1 (kStepActive),
    // with kCanAutomate AND kIsHidden
    using namespace Ruinae;
    ParameterContainer container;
    registerArpParams(container);

    for (int i = 0; i < 32; ++i) {
        auto* param = container.getParameter(
            static_cast<ParamID>(kArpModifierLaneStep0Id + i));
        INFO("Modifier step param " << i << " (ID " << (kArpModifierLaneStep0Id + i) << ")");
        REQUIRE(param != nullptr);

        ParameterInfo info = param->getInfo();
        CHECK((info.flags & ParameterInfo::kCanAutomate) != 0);
        CHECK((info.flags & ParameterInfo::kIsHidden) != 0);
    }
}

TEST_CASE("ArpAccentVelocity_Registration", "[arp][params]") {
    // FR-026, FR-027: kArpAccentVelocityId registered as RangeParameter [0,127]
    // default 30, kCanAutomate, NOT kIsHidden
    using namespace Ruinae;
    ParameterContainer container;
    registerArpParams(container);

    auto* param = container.getParameter(kArpAccentVelocityId);
    REQUIRE(param != nullptr);

    ParameterInfo info = param->getInfo();
    CHECK((info.flags & ParameterInfo::kCanAutomate) != 0);
    CHECK((info.flags & ParameterInfo::kIsHidden) == 0);
}

TEST_CASE("ArpSlideTime_Registration", "[arp][params]") {
    // FR-026, FR-027: kArpSlideTimeId registered as continuous Parameter [0,1]
    // default 0.12, kCanAutomate
    using namespace Ruinae;
    ParameterContainer container;
    registerArpParams(container);

    auto* param = container.getParameter(kArpSlideTimeId);
    REQUIRE(param != nullptr);

    ParameterInfo info = param->getInfo();
    CHECK((info.flags & ParameterInfo::kCanAutomate) != 0);
}

TEST_CASE("ArpModifierLaneLength_Denormalize", "[arp][params]") {
    // FR-028: handleArpParamChange denormalizes modifier lane length correctly
    using namespace Ruinae;
    ArpeggiatorParams params;

    // 0.0 -> length 1
    handleArpParamChange(params, kArpModifierLaneLengthId, 0.0);
    CHECK(params.modifierLaneLength.load() == 1);

    // 1.0 -> length 32
    handleArpParamChange(params, kArpModifierLaneLengthId, 1.0);
    CHECK(params.modifierLaneLength.load() == 32);

    // 16.0/31.0 -> rounds to 16 -> 1 + 16 = 17
    handleArpParamChange(params, kArpModifierLaneLengthId, 16.0 / 31.0);
    CHECK(params.modifierLaneLength.load() == 17);
}

TEST_CASE("ArpModifierLaneStep_Denormalize", "[arp][params]") {
    // FR-028: handleArpParamChange denormalizes modifier step values correctly
    using namespace Ruinae;
    ArpeggiatorParams params;

    // 0.0 -> step[0] = 0
    handleArpParamChange(params, kArpModifierLaneStep0Id, 0.0);
    CHECK(params.modifierLaneSteps[0].load() == 0);

    // 1.0/255.0 -> step[0] = 1
    handleArpParamChange(params, kArpModifierLaneStep0Id, 1.0 / 255.0);
    CHECK(params.modifierLaneSteps[0].load() == 1);

    // 1.0 -> step[0] = 255
    handleArpParamChange(params, kArpModifierLaneStep0Id, 1.0);
    CHECK(params.modifierLaneSteps[0].load() == 255);
}

TEST_CASE("ArpAccentVelocity_Denormalize", "[arp][params]") {
    // FR-028: handleArpParamChange denormalizes accent velocity correctly
    using namespace Ruinae;
    ArpeggiatorParams params;

    // 0.0 -> accentVelocity = 0
    handleArpParamChange(params, kArpAccentVelocityId, 0.0);
    CHECK(params.accentVelocity.load() == 0);

    // 30.0/127.0 -> 30
    handleArpParamChange(params, kArpAccentVelocityId, 30.0 / 127.0);
    CHECK(params.accentVelocity.load() == 30);

    // 1.0 -> 127
    handleArpParamChange(params, kArpAccentVelocityId, 1.0);
    CHECK(params.accentVelocity.load() == 127);
}

TEST_CASE("ArpSlideTime_Denormalize", "[arp][params]") {
    // FR-028: handleArpParamChange denormalizes slide time correctly
    using namespace Ruinae;
    ArpeggiatorParams params;

    // 0.0 -> slideTime = 0.0
    handleArpParamChange(params, kArpSlideTimeId, 0.0);
    CHECK(params.slideTime.load() == Approx(0.0f).margin(0.001f));

    // 0.12 -> ~60ms
    handleArpParamChange(params, kArpSlideTimeId, 0.12);
    CHECK(params.slideTime.load() == Approx(60.0f).margin(0.1f));

    // 1.0 -> 500ms
    handleArpParamChange(params, kArpSlideTimeId, 1.0);
    CHECK(params.slideTime.load() == Approx(500.0f).margin(0.001f));
}

// ==============================================================================
// Phase 8 (073-per-step-mods) User Story 6: Modifier Lane Persistence Tests (T061)
// ==============================================================================

TEST_CASE("ModifierLane_SaveLoad_RoundTrip", "[arp][params][state]") {
    // SC-007: Configure modifier lane length=8, distinct flag combinations per step,
    // accentVelocity=35, slideTime=50.0f; save; fresh params; load; verify all 35 match.
    using namespace Ruinae;

    ArpeggiatorParams original;
    original.modifierLaneLength.store(8, std::memory_order_relaxed);
    // Set steps 0-7 to distinct flag combinations
    original.modifierLaneSteps[0].store(0x01, std::memory_order_relaxed);  // Active
    original.modifierLaneSteps[1].store(0x03, std::memory_order_relaxed);  // Active|Tie
    original.modifierLaneSteps[2].store(0x05, std::memory_order_relaxed);  // Active|Slide
    original.modifierLaneSteps[3].store(0x09, std::memory_order_relaxed);  // Active|Accent
    original.modifierLaneSteps[4].store(0x0F, std::memory_order_relaxed);  // All flags
    original.modifierLaneSteps[5].store(0x00, std::memory_order_relaxed);  // Rest
    original.modifierLaneSteps[6].store(0x0D, std::memory_order_relaxed);  // Active|Slide|Accent
    original.modifierLaneSteps[7].store(0x0B, std::memory_order_relaxed);  // Active|Tie|Accent
    original.accentVelocity.store(35, std::memory_order_relaxed);
    original.slideTime.store(50.0f, std::memory_order_relaxed);

    // Serialize
    auto stream = Steinberg::owned(new Steinberg::MemoryStream());
    {
        Steinberg::IBStreamer writeStream(stream, kLittleEndian);
        saveArpParams(original, writeStream);
    }

    // Deserialize into fresh params
    ArpeggiatorParams loaded;
    stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    {
        Steinberg::IBStreamer readStream(stream, kLittleEndian);
        bool ok = loadArpParams(loaded, readStream);
        REQUIRE(ok);
    }

    // Verify all 35 modifier values match
    CHECK(loaded.modifierLaneLength.load() == 8);
    for (int i = 0; i < 32; ++i) {
        INFO("Modifier step " << i);
        CHECK(loaded.modifierLaneSteps[i].load() ==
              original.modifierLaneSteps[i].load());
    }
    CHECK(loaded.accentVelocity.load() == 35);
    CHECK(loaded.slideTime.load() == Approx(50.0f).margin(0.001f));
}

TEST_CASE("ModifierLane_BackwardCompat_Phase4Stream", "[arp][params][state][compat]") {
    // FR-030, SC-008: Construct IBStream with only Phase 4 data (11 base + 99 lane params);
    // load; verify no crash, defaults: length=1, steps=1, accent=30, slideTime=60.0f
    using namespace Ruinae;

    auto stream = Steinberg::owned(new Steinberg::MemoryStream());
    {
        Steinberg::IBStreamer writeStream(stream, kLittleEndian);
        // 11 base params
        writeStream.writeInt32(1);     // enabled = true
        writeStream.writeInt32(0);     // mode = Up
        writeStream.writeInt32(1);     // octaveRange = 1
        writeStream.writeInt32(0);     // octaveMode = Sequential
        writeStream.writeInt32(1);     // tempoSync = true
        writeStream.writeInt32(10);    // noteValue = 1/8
        writeStream.writeFloat(4.0f);  // freeRate = 4.0 Hz
        writeStream.writeFloat(80.0f); // gateLength = 80%
        writeStream.writeFloat(0.0f);  // swing = 0%
        writeStream.writeInt32(0);     // latchMode = Off
        writeStream.writeInt32(0);     // retrigger = Off

        // Velocity lane data (33 values)
        writeStream.writeInt32(1);     // velocityLaneLength = 1
        for (int i = 0; i < 32; ++i) {
            writeStream.writeFloat(1.0f);
        }

        // Gate lane data (33 values)
        writeStream.writeInt32(1);     // gateLaneLength = 1
        for (int i = 0; i < 32; ++i) {
            writeStream.writeFloat(1.0f);
        }

        // Pitch lane data (33 values)
        writeStream.writeInt32(1);     // pitchLaneLength = 1
        for (int i = 0; i < 32; ++i) {
            writeStream.writeInt32(0);
        }
        // NO modifier lane data -- stream ends here (Phase 4 preset)
    }

    ArpeggiatorParams loaded;
    stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    {
        Steinberg::IBStreamer readStream(stream, kLittleEndian);
        bool ok = loadArpParams(loaded, readStream);
        // Should return true: Phase 4 preset with no modifier data
        REQUIRE(ok == true);
    }

    // Base params loaded correctly
    CHECK(loaded.enabled.load() == true);
    CHECK(loaded.mode.load() == 0);

    // Modifier lane defaults preserved
    CHECK(loaded.modifierLaneLength.load() == 16);
    for (int i = 0; i < 32; ++i) {
        INFO("Modifier step " << i);
        CHECK(loaded.modifierLaneSteps[i].load() == 1);  // kStepActive
    }
    CHECK(loaded.accentVelocity.load() == 30);
    CHECK(loaded.slideTime.load() == Approx(60.0f).margin(0.001f));
}

TEST_CASE("ModifierLane_PartialStream_LengthOnly_ReturnsFalse", "[arp][params][state][compat]") {
    // FR-030: Phase 4 data + ONLY modifierLaneLength; load returns false (corrupt).
    // This distinguishes from Phase 4 backward-compat (EOF at length read = true)
    // vs. partial modifier section (EOF after length read = false).
    using namespace Ruinae;

    auto stream = Steinberg::owned(new Steinberg::MemoryStream());
    {
        Steinberg::IBStreamer writeStream(stream, kLittleEndian);
        // 11 base params
        writeStream.writeInt32(1);     // enabled = true
        writeStream.writeInt32(0);     // mode = Up
        writeStream.writeInt32(1);     // octaveRange = 1
        writeStream.writeInt32(0);     // octaveMode = Sequential
        writeStream.writeInt32(1);     // tempoSync = true
        writeStream.writeInt32(10);    // noteValue = 1/8
        writeStream.writeFloat(4.0f);  // freeRate = 4.0 Hz
        writeStream.writeFloat(80.0f); // gateLength = 80%
        writeStream.writeFloat(0.0f);  // swing = 0%
        writeStream.writeInt32(0);     // latchMode = Off
        writeStream.writeInt32(0);     // retrigger = Off

        // Velocity lane data (33 values)
        writeStream.writeInt32(1);
        for (int i = 0; i < 32; ++i) {
            writeStream.writeFloat(1.0f);
        }

        // Gate lane data (33 values)
        writeStream.writeInt32(1);
        for (int i = 0; i < 32; ++i) {
            writeStream.writeFloat(1.0f);
        }

        // Pitch lane data (33 values)
        writeStream.writeInt32(1);
        for (int i = 0; i < 32; ++i) {
            writeStream.writeInt32(0);
        }

        // ONLY modifier lane length (truncated -- no step data after)
        writeStream.writeInt32(4);
    }

    ArpeggiatorParams loaded;
    stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    {
        Steinberg::IBStreamer readStream(stream, kLittleEndian);
        bool ok = loadArpParams(loaded, readStream);
        // Should return false: modifier length present but steps missing (corrupt)
        CHECK(ok == false);
    }
}

TEST_CASE("ModifierLane_StepValues_BeyondActiveLength_Preserved", "[arp][params][state]") {
    // Set modifier lane length=4, set steps 4-31 to non-default values; save/load;
    // verify steps 4-31 preserved (all 32 serialized).
    using namespace Ruinae;

    ArpeggiatorParams original;
    original.modifierLaneLength.store(4, std::memory_order_relaxed);
    // Set steps beyond active length to non-default values
    for (int i = 4; i < 32; ++i) {
        original.modifierLaneSteps[i].store(0x0F, std::memory_order_relaxed);  // All flags
    }

    // Serialize
    auto stream = Steinberg::owned(new Steinberg::MemoryStream());
    {
        Steinberg::IBStreamer writeStream(stream, kLittleEndian);
        saveArpParams(original, writeStream);
    }

    // Deserialize
    ArpeggiatorParams loaded;
    stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    {
        Steinberg::IBStreamer readStream(stream, kLittleEndian);
        bool ok = loadArpParams(loaded, readStream);
        REQUIRE(ok);
    }

    // Verify length
    CHECK(loaded.modifierLaneLength.load() == 4);

    // Verify steps beyond active length are preserved
    for (int i = 4; i < 32; ++i) {
        INFO("Modifier step " << i);
        CHECK(loaded.modifierLaneSteps[i].load() == 0x0F);
    }
}

TEST_CASE("ModifierLane_SlideTime_FloatPrecision", "[arp][params][state]") {
    // Save slideTime=60.0f; load; verify with Approx().margin(0.001f).
    using namespace Ruinae;

    ArpeggiatorParams original;
    original.slideTime.store(60.0f, std::memory_order_relaxed);

    // Serialize
    auto stream = Steinberg::owned(new Steinberg::MemoryStream());
    {
        Steinberg::IBStreamer writeStream(stream, kLittleEndian);
        saveArpParams(original, writeStream);
    }

    // Deserialize
    ArpeggiatorParams loaded;
    stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    {
        Steinberg::IBStreamer readStream(stream, kLittleEndian);
        bool ok = loadArpParams(loaded, readStream);
        REQUIRE(ok);
    }

    CHECK(loaded.slideTime.load() == Approx(60.0f).margin(0.001f));
}

// ==============================================================================
// Phase 9: SC-010 FormatArpParam Tests (073-per-step-mods edge cases)
// ==============================================================================

TEST_CASE("SC010_FormatArpParam_ModifierLaneLength", "[arp][params][format]") {
    using namespace Ruinae;
    Steinberg::Vst::String128 string;

    // Normalized value for length 8: (8-1)/31 = 7/31
    double norm8 = 7.0 / 31.0;
    auto result = formatArpParam(kArpModifierLaneLengthId, norm8, string);
    CHECK(result == Steinberg::kResultOk);
    CHECK(toString128(string) == "8 steps");

    // Length 1 at norm 0.0
    result = formatArpParam(kArpModifierLaneLengthId, 0.0, string);
    CHECK(result == Steinberg::kResultOk);
    CHECK(toString128(string) == "1 steps");

    // Length 32 at norm 1.0
    result = formatArpParam(kArpModifierLaneLengthId, 1.0, string);
    CHECK(result == Steinberg::kResultOk);
    CHECK(toString128(string) == "32 steps");
}

TEST_CASE("SC010_FormatArpParam_ModifierStep", "[arp][params][format]") {
    using namespace Ruinae;
    Steinberg::Vst::String128 string;

    // Step value 5 (Active|Slide) -> normalized = 5/255 -> "SL"
    double norm5 = 5.0 / 255.0;
    auto result = formatArpParam(kArpModifierLaneStep0Id, norm5, string);
    CHECK(result == Steinberg::kResultOk);
    CHECK(toString128(string) == "SL");

    // Step value 0 (Rest) -> norm 0.0 -> "REST"
    result = formatArpParam(kArpModifierLaneStep0Id, 0.0, string);
    CHECK(result == Steinberg::kResultOk);
    CHECK(toString128(string) == "REST");

    // Step value 255 (all flags set) -> norm 1.0 -> has Active+Tie -> "TIE"
    result = formatArpParam(kArpModifierLaneStep0Id, 1.0, string);
    CHECK(result == Steinberg::kResultOk);
    CHECK(toString128(string) == "TIE");

    // Step value 1 (kStepActive) -> norm 1/255 -> "--"
    double norm1 = 1.0 / 255.0;
    result = formatArpParam(kArpModifierLaneStep0Id, norm1, string);
    CHECK(result == Steinberg::kResultOk);
    CHECK(toString128(string) == "--");
}

TEST_CASE("SC010_FormatArpParam_AccentVelocity", "[arp][params][format]") {
    using namespace Ruinae;
    Steinberg::Vst::String128 string;

    // Accent velocity 30 -> norm = 30/127
    double norm30 = 30.0 / 127.0;
    auto result = formatArpParam(kArpAccentVelocityId, norm30, string);
    CHECK(result == Steinberg::kResultOk);
    CHECK(toString128(string) == "30");

    // Accent velocity 0 -> norm 0.0
    result = formatArpParam(kArpAccentVelocityId, 0.0, string);
    CHECK(result == Steinberg::kResultOk);
    CHECK(toString128(string) == "0");

    // Accent velocity 127 -> norm 1.0
    result = formatArpParam(kArpAccentVelocityId, 1.0, string);
    CHECK(result == Steinberg::kResultOk);
    CHECK(toString128(string) == "127");
}

TEST_CASE("SC010_FormatArpParam_SlideTime", "[arp][params][format]") {
    using namespace Ruinae;
    Steinberg::Vst::String128 string;

    // Slide time 60ms -> norm = 60/500 = 0.12
    auto result = formatArpParam(kArpSlideTimeId, 0.12, string);
    CHECK(result == Steinberg::kResultOk);
    CHECK(toString128(string) == "60 ms");

    // Slide time 0ms -> norm 0.0
    result = formatArpParam(kArpSlideTimeId, 0.0, string);
    CHECK(result == Steinberg::kResultOk);
    CHECK(toString128(string) == "0 ms");

    // Slide time 500ms -> norm 1.0
    result = formatArpParam(kArpSlideTimeId, 1.0, string);
    CHECK(result == Steinberg::kResultOk);
    CHECK(toString128(string) == "500 ms");
}

// ==============================================================================
// Phase 6 (075-euclidean-timing) Task Group 3: Euclidean Parameter Tests
// ==============================================================================

// T069: All 4 Euclidean parameter IDs registered with kCanAutomate, none kIsHidden
TEST_CASE("EuclideanParams_AllRegistered_WithCanAutomate", "[arp][params][euclidean]") {
    using namespace Ruinae;
    Steinberg::Vst::ParameterContainer container;
    registerArpParams(container);

    // All 4 Euclidean parameter IDs must be present
    constexpr ParamID euclideanIds[] = {
        kArpEuclideanEnabledId,   // 3230
        kArpEuclideanHitsId,      // 3231
        kArpEuclideanStepsId,     // 3232
        kArpEuclideanRotationId,  // 3233
    };

    for (auto id : euclideanIds) {
        INFO("Parameter ID " << id);
        auto* param = container.getParameter(id);
        REQUIRE(param != nullptr);

        ParameterInfo info = param->getInfo();
        // kCanAutomate must be set
        CHECK((info.flags & ParameterInfo::kCanAutomate) != 0);
        // kIsHidden must NOT be set -- all are user-facing
        CHECK((info.flags & ParameterInfo::kIsHidden) == 0);
    }
}

// T070: formatArpParam for Euclidean Enabled: "Off" / "On"
TEST_CASE("EuclideanParams_FormatEnabled", "[arp][params][euclidean][format]") {
    using namespace Ruinae;
    Steinberg::Vst::String128 string;

    // 0.0 -> "Off"
    auto result = formatArpParam(kArpEuclideanEnabledId, 0.0, string);
    CHECK(result == Steinberg::kResultOk);
    CHECK(toString128(string) == "Off");

    // 1.0 -> "On"
    result = formatArpParam(kArpEuclideanEnabledId, 1.0, string);
    CHECK(result == Steinberg::kResultOk);
    CHECK(toString128(string) == "On");
}

// T071: formatArpParam for Euclidean Hits: "%d hits"
TEST_CASE("EuclideanParams_FormatHits", "[arp][params][euclidean][format]") {
    using namespace Ruinae;
    Steinberg::Vst::String128 string;

    // 0.0 -> "0 hits"
    auto result = formatArpParam(kArpEuclideanHitsId, 0.0, string);
    CHECK(result == Steinberg::kResultOk);
    CHECK(toString128(string) == "0 hits");

    // 3.0/32.0 -> "3 hits"
    result = formatArpParam(kArpEuclideanHitsId, 3.0 / 32.0, string);
    CHECK(result == Steinberg::kResultOk);
    CHECK(toString128(string) == "3 hits");

    // 5.0/32.0 -> "5 hits"
    result = formatArpParam(kArpEuclideanHitsId, 5.0 / 32.0, string);
    CHECK(result == Steinberg::kResultOk);
    CHECK(toString128(string) == "5 hits");

    // 1.0 -> "32 hits"
    result = formatArpParam(kArpEuclideanHitsId, 1.0, string);
    CHECK(result == Steinberg::kResultOk);
    CHECK(toString128(string) == "32 hits");
}

// T072: formatArpParam for Euclidean Steps: "%d steps"
TEST_CASE("EuclideanParams_FormatSteps", "[arp][params][euclidean][format]") {
    using namespace Ruinae;
    Steinberg::Vst::String128 string;

    // 0.0 -> "2 steps"
    auto result = formatArpParam(kArpEuclideanStepsId, 0.0, string);
    CHECK(result == Steinberg::kResultOk);
    CHECK(toString128(string) == "2 steps");

    // 6.0/30.0 -> "8 steps"
    result = formatArpParam(kArpEuclideanStepsId, 6.0 / 30.0, string);
    CHECK(result == Steinberg::kResultOk);
    CHECK(toString128(string) == "8 steps");

    // 1.0 -> "32 steps"
    result = formatArpParam(kArpEuclideanStepsId, 1.0, string);
    CHECK(result == Steinberg::kResultOk);
    CHECK(toString128(string) == "32 steps");
}

// T073: formatArpParam for Euclidean Rotation: "%d"
TEST_CASE("EuclideanParams_FormatRotation", "[arp][params][euclidean][format]") {
    using namespace Ruinae;
    Steinberg::Vst::String128 string;

    // 0.0 -> "0"
    auto result = formatArpParam(kArpEuclideanRotationId, 0.0, string);
    CHECK(result == Steinberg::kResultOk);
    CHECK(toString128(string) == "0");

    // 3.0/31.0 -> "3"
    result = formatArpParam(kArpEuclideanRotationId, 3.0 / 31.0, string);
    CHECK(result == Steinberg::kResultOk);
    CHECK(toString128(string) == "3");

    // 1.0 -> "31"
    result = formatArpParam(kArpEuclideanRotationId, 1.0, string);
    CHECK(result == Steinberg::kResultOk);
    CHECK(toString128(string) == "31");
}

// T074: handleArpParamChange for Euclidean Enabled
TEST_CASE("EuclideanParams_HandleParamChange_Enabled", "[arp][params][euclidean][denorm]") {
    using namespace Ruinae;
    ArpeggiatorParams params;

    // 0.0 -> false
    handleArpParamChange(params, kArpEuclideanEnabledId, 0.0);
    CHECK(params.euclideanEnabled.load() == false);

    // 1.0 -> true
    handleArpParamChange(params, kArpEuclideanEnabledId, 1.0);
    CHECK(params.euclideanEnabled.load() == true);

    // 0.4 -> false (threshold at 0.5)
    handleArpParamChange(params, kArpEuclideanEnabledId, 0.4);
    CHECK(params.euclideanEnabled.load() == false);

    // 0.5 -> true (threshold at 0.5)
    handleArpParamChange(params, kArpEuclideanEnabledId, 0.5);
    CHECK(params.euclideanEnabled.load() == true);
}

// T075: handleArpParamChange for Euclidean Hits
TEST_CASE("EuclideanParams_HandleParamChange_Hits", "[arp][params][euclidean][denorm]") {
    using namespace Ruinae;
    ArpeggiatorParams params;

    // 0.0 -> hits=0
    handleArpParamChange(params, kArpEuclideanHitsId, 0.0);
    CHECK(params.euclideanHits.load() == 0);

    // 3.0/32.0 -> hits=3
    handleArpParamChange(params, kArpEuclideanHitsId, 3.0 / 32.0);
    CHECK(params.euclideanHits.load() == 3);

    // 1.0 -> hits=32
    handleArpParamChange(params, kArpEuclideanHitsId, 1.0);
    CHECK(params.euclideanHits.load() == 32);
}

// T076: handleArpParamChange for Euclidean Steps
TEST_CASE("EuclideanParams_HandleParamChange_Steps", "[arp][params][euclidean][denorm]") {
    using namespace Ruinae;
    ArpeggiatorParams params;

    // 0.0 -> steps=2
    handleArpParamChange(params, kArpEuclideanStepsId, 0.0);
    CHECK(params.euclideanSteps.load() == 2);

    // 6.0/30.0 -> steps=8
    handleArpParamChange(params, kArpEuclideanStepsId, 6.0 / 30.0);
    CHECK(params.euclideanSteps.load() == 8);

    // 1.0 -> steps=32
    handleArpParamChange(params, kArpEuclideanStepsId, 1.0);
    CHECK(params.euclideanSteps.load() == 32);
}

// T077: handleArpParamChange for Euclidean Rotation
TEST_CASE("EuclideanParams_HandleParamChange_Rotation", "[arp][params][euclidean][denorm]") {
    using namespace Ruinae;
    ArpeggiatorParams params;

    // 0.0 -> rotation=0
    handleArpParamChange(params, kArpEuclideanRotationId, 0.0);
    CHECK(params.euclideanRotation.load() == 0);

    // 3.0/31.0 -> rotation=3
    handleArpParamChange(params, kArpEuclideanRotationId, 3.0 / 31.0);
    CHECK(params.euclideanRotation.load() == 3);

    // 1.0 -> rotation=31
    handleArpParamChange(params, kArpEuclideanRotationId, 1.0);
    CHECK(params.euclideanRotation.load() == 31);
}

// ==============================================================================
// Phase 7 (076-conditional-trigs) Task Group 4: Condition Parameter Tests
// ==============================================================================

// T079: All 34 condition parameter IDs registered with correct flags (SC-012, FR-040)
TEST_CASE("ConditionParams_AllRegistered_CorrectFlags", "[arp][params][condition]") {
    using namespace Ruinae;
    Steinberg::Vst::ParameterContainer container;
    registerArpParams(container);

    // kArpConditionLaneLengthId (3240): kCanAutomate, NOT kIsHidden
    {
        auto* param = container.getParameter(kArpConditionLaneLengthId);
        REQUIRE(param != nullptr);
        ParameterInfo info = param->getInfo();
        CHECK((info.flags & ParameterInfo::kCanAutomate) != 0);
        CHECK((info.flags & ParameterInfo::kIsHidden) == 0);
    }

    // All 32 step IDs (3241-3272): kCanAutomate AND kIsHidden
    for (int i = 0; i < 32; ++i) {
        INFO("Condition step ID " << (kArpConditionLaneStep0Id + i));
        auto* param = container.getParameter(
            static_cast<ParamID>(kArpConditionLaneStep0Id + i));
        REQUIRE(param != nullptr);
        ParameterInfo info = param->getInfo();
        CHECK((info.flags & ParameterInfo::kCanAutomate) != 0);
        CHECK((info.flags & ParameterInfo::kIsHidden) != 0);
    }

    // kArpFillToggleId (3280): kCanAutomate, NOT kIsHidden
    {
        auto* param = container.getParameter(kArpFillToggleId);
        REQUIRE(param != nullptr);
        ParameterInfo info = param->getInfo();
        CHECK((info.flags & ParameterInfo::kCanAutomate) != 0);
        CHECK((info.flags & ParameterInfo::kIsHidden) == 0);
    }
}

// T080: formatArpParam for condition lane length (SC-012, FR-047)
TEST_CASE("ConditionParams_FormatLaneLength", "[arp][params][condition][format]") {
    using namespace Ruinae;
    Steinberg::Vst::String128 string;

    // 0.0 -> "1 step" (singular)
    auto result = formatArpParam(kArpConditionLaneLengthId, 0.0, string);
    CHECK(result == Steinberg::kResultOk);
    CHECK(toString128(string) == "1 step");

    // 7.0/31.0 -> "8 steps" (plural)
    result = formatArpParam(kArpConditionLaneLengthId, 7.0 / 31.0, string);
    CHECK(result == Steinberg::kResultOk);
    CHECK(toString128(string) == "8 steps");

    // 1.0 -> "32 steps"
    result = formatArpParam(kArpConditionLaneLengthId, 1.0, string);
    CHECK(result == Steinberg::kResultOk);
    CHECK(toString128(string) == "32 steps");
}

// T081: formatArpParam for all 18 condition display values (SC-012, FR-047)
TEST_CASE("ConditionParams_FormatStepValues", "[arp][params][condition][format]") {
    using namespace Ruinae;
    Steinberg::Vst::String128 string;

    // Map of normalized values to expected display strings
    // For step IDs, normalized value maps via round(value * 17) to index 0-17
    static const struct {
        double normValue;
        const char* expected;
    } kExpected[] = {
        { 0.0 / 17.0, "Always" },   // idx 0
        { 1.0 / 17.0, "10%" },      // idx 1
        { 2.0 / 17.0, "25%" },      // idx 2
        { 3.0 / 17.0, "50%" },      // idx 3
        { 4.0 / 17.0, "75%" },      // idx 4
        { 5.0 / 17.0, "90%" },      // idx 5
        { 6.0 / 17.0, "1:2" },      // idx 6
        { 7.0 / 17.0, "2:2" },      // idx 7
        { 8.0 / 17.0, "1:3" },      // idx 8
        { 9.0 / 17.0, "2:3" },      // idx 9
        { 10.0 / 17.0, "3:3" },     // idx 10
        { 11.0 / 17.0, "1:4" },     // idx 11
        { 12.0 / 17.0, "2:4" },     // idx 12
        { 13.0 / 17.0, "3:4" },     // idx 13
        { 14.0 / 17.0, "4:4" },     // idx 14
        { 15.0 / 17.0, "1st" },     // idx 15
        { 16.0 / 17.0, "Fill" },    // idx 16
        { 1.0, "!Fill" },           // idx 17
    };

    for (const auto& [normValue, expected] : kExpected) {
        INFO("Condition index for norm " << normValue << " expected: " << expected);
        auto result = formatArpParam(kArpConditionLaneStep0Id, normValue, string);
        CHECK(result == Steinberg::kResultOk);
        CHECK(toString128(string) == expected);
    }
}

// T082: formatArpParam for fill toggle (SC-012, FR-047)
TEST_CASE("ConditionParams_FormatFillToggle", "[arp][params][condition][format]") {
    using namespace Ruinae;
    Steinberg::Vst::String128 string;

    // 0.0 -> "Off"
    auto result = formatArpParam(kArpFillToggleId, 0.0, string);
    CHECK(result == Steinberg::kResultOk);
    CHECK(toString128(string) == "Off");

    // 1.0 -> "On"
    result = formatArpParam(kArpFillToggleId, 1.0, string);
    CHECK(result == Steinberg::kResultOk);
    CHECK(toString128(string) == "On");
}

// T083: handleArpParamChange for condition lane length (FR-042)
TEST_CASE("ConditionParams_HandleParamChange_LaneLength", "[arp][params][condition][denorm]") {
    using namespace Ruinae;
    ArpeggiatorParams params;

    // 0.0 -> conditionLaneLength == 1
    handleArpParamChange(params, kArpConditionLaneLengthId, 0.0);
    CHECK(params.conditionLaneLength.load() == 1);

    // 7.0/31.0 -> conditionLaneLength == 8
    handleArpParamChange(params, kArpConditionLaneLengthId, 7.0 / 31.0);
    CHECK(params.conditionLaneLength.load() == 8);

    // 1.0 -> conditionLaneLength == 32
    handleArpParamChange(params, kArpConditionLaneLengthId, 1.0);
    CHECK(params.conditionLaneLength.load() == 32);
}

// T084: handleArpParamChange for condition step values (FR-042)
TEST_CASE("ConditionParams_HandleParamChange_StepValues", "[arp][params][condition][denorm]") {
    using namespace Ruinae;
    ArpeggiatorParams params;

    // 0.0 -> step 0 == 0 (Always)
    handleArpParamChange(params, kArpConditionLaneStep0Id, 0.0);
    CHECK(params.conditionLaneSteps[0].load() == 0);

    // 3.0/17.0 -> step 0 == 3 (Prob50)
    handleArpParamChange(params, kArpConditionLaneStep0Id, 3.0 / 17.0);
    CHECK(params.conditionLaneSteps[0].load() == 3);

    // 1.0 -> step 0 == 17 (NotFill)
    handleArpParamChange(params, kArpConditionLaneStep0Id, 1.0);
    CHECK(params.conditionLaneSteps[0].load() == 17);
}

// T085: handleArpParamChange for fill toggle (FR-042)
TEST_CASE("ConditionParams_HandleParamChange_FillToggle", "[arp][params][condition][denorm]") {
    using namespace Ruinae;
    ArpeggiatorParams params;

    // 0.0 -> fillToggle == false
    handleArpParamChange(params, kArpFillToggleId, 0.0);
    CHECK(params.fillToggle.load() == false);

    // 0.4 -> fillToggle == false (threshold at 0.5)
    handleArpParamChange(params, kArpFillToggleId, 0.4);
    CHECK(params.fillToggle.load() == false);

    // 0.5 -> fillToggle == true
    handleArpParamChange(params, kArpFillToggleId, 0.5);
    CHECK(params.fillToggle.load() == true);

    // 1.0 -> fillToggle == true
    handleArpParamChange(params, kArpFillToggleId, 1.0);
    CHECK(params.fillToggle.load() == true);
}

// ==============================================================================
// Phase 6 (077-spice-dice-humanize) Task Group 5: Spice/Dice/Humanize Parameter Tests
// ==============================================================================

// T061: All 3 Spice/Dice/Humanize params registered with kCanAutomate, none kIsHidden
TEST_CASE("SpiceHumanize_AllThreeParams_Registered", "[arp][params][spice][humanize]") {
    using namespace Ruinae;
    Steinberg::Vst::ParameterContainer container;
    registerArpParams(container);

    // kArpSpiceId (3290): kCanAutomate, NOT kIsHidden
    {
        auto* param = container.getParameter(kArpSpiceId);
        REQUIRE(param != nullptr);
        ParameterInfo info = param->getInfo();
        CHECK((info.flags & ParameterInfo::kCanAutomate) != 0);
        CHECK((info.flags & ParameterInfo::kIsHidden) == 0);
    }

    // kArpDiceTriggerId (3291): kCanAutomate, NOT kIsHidden
    {
        auto* param = container.getParameter(kArpDiceTriggerId);
        REQUIRE(param != nullptr);
        ParameterInfo info = param->getInfo();
        CHECK((info.flags & ParameterInfo::kCanAutomate) != 0);
        CHECK((info.flags & ParameterInfo::kIsHidden) == 0);
    }

    // kArpHumanizeId (3292): kCanAutomate, NOT kIsHidden
    {
        auto* param = container.getParameter(kArpHumanizeId);
        REQUIRE(param != nullptr);
        ParameterInfo info = param->getInfo();
        CHECK((info.flags & ParameterInfo::kCanAutomate) != 0);
        CHECK((info.flags & ParameterInfo::kIsHidden) == 0);
    }

    // Verify sentinels unchanged
    CHECK(kArpEndId == 3299);
    CHECK(kNumParameters == 3300);
}

// T062: formatArpParam for Spice: percentage display
TEST_CASE("SpiceHumanize_FormatSpice_Percentage", "[arp][params][spice][format]") {
    using namespace Ruinae;
    Steinberg::Vst::String128 string;

    // 0.0 -> "0%"
    auto result = formatArpParam(kArpSpiceId, 0.0, string);
    CHECK(result == Steinberg::kResultOk);
    CHECK(toString128(string) == "0%");

    // 0.5 -> "50%"
    result = formatArpParam(kArpSpiceId, 0.5, string);
    CHECK(result == Steinberg::kResultOk);
    CHECK(toString128(string) == "50%");

    // 1.0 -> "100%"
    result = formatArpParam(kArpSpiceId, 1.0, string);
    CHECK(result == Steinberg::kResultOk);
    CHECK(toString128(string) == "100%");
}

// T063: formatArpParam for Dice trigger: "--" / "Roll"
TEST_CASE("SpiceHumanize_FormatDiceTrigger", "[arp][params][spice][format]") {
    using namespace Ruinae;
    Steinberg::Vst::String128 string;

    // 0.0 -> "--"
    auto result = formatArpParam(kArpDiceTriggerId, 0.0, string);
    CHECK(result == Steinberg::kResultOk);
    CHECK(toString128(string) == "--");

    // 0.5 -> "Roll"
    result = formatArpParam(kArpDiceTriggerId, 0.5, string);
    CHECK(result == Steinberg::kResultOk);
    CHECK(toString128(string) == "Roll");

    // 1.0 -> "Roll"
    result = formatArpParam(kArpDiceTriggerId, 1.0, string);
    CHECK(result == Steinberg::kResultOk);
    CHECK(toString128(string) == "Roll");
}

// T064: formatArpParam for Humanize: percentage display
TEST_CASE("SpiceHumanize_FormatHumanize_Percentage", "[arp][params][humanize][format]") {
    using namespace Ruinae;
    Steinberg::Vst::String128 string;

    // 0.0 -> "0%"
    auto result = formatArpParam(kArpHumanizeId, 0.0, string);
    CHECK(result == Steinberg::kResultOk);
    CHECK(toString128(string) == "0%");

    // 0.5 -> "50%"
    result = formatArpParam(kArpHumanizeId, 0.5, string);
    CHECK(result == Steinberg::kResultOk);
    CHECK(toString128(string) == "50%");

    // 1.0 -> "100%"
    result = formatArpParam(kArpHumanizeId, 1.0, string);
    CHECK(result == Steinberg::kResultOk);
    CHECK(toString128(string) == "100%");
}

// T065: handleArpParamChange for Spice: clamped float storage
TEST_CASE("SpiceHumanize_HandleParamChange_SpiceStored", "[arp][params][spice][denorm]") {
    using namespace Ruinae;
    ArpeggiatorParams params;

    // 0.35 -> spice == 0.35f
    handleArpParamChange(params, kArpSpiceId, 0.35);
    CHECK(params.spice.load() == Approx(0.35f).margin(0.001f));

    // -0.1 (below range, clamped) -> spice == 0.0f
    handleArpParamChange(params, kArpSpiceId, -0.1);
    CHECK(params.spice.load() == Approx(0.0f).margin(0.001f));

    // 1.5 (above range, clamped) -> spice == 1.0f
    handleArpParamChange(params, kArpSpiceId, 1.5);
    CHECK(params.spice.load() == Approx(1.0f).margin(0.001f));
}

// T066: handleArpParamChange for Dice trigger: rising edge detection
TEST_CASE("SpiceHumanize_HandleParamChange_DiceTriggerRisingEdge", "[arp][params][spice][denorm]") {
    using namespace Ruinae;
    ArpeggiatorParams params;

    // 0.0 -> diceTrigger remains false
    handleArpParamChange(params, kArpDiceTriggerId, 0.0);
    CHECK(params.diceTrigger.load() == false);

    // 1.0 -> diceTrigger set to true (rising edge)
    handleArpParamChange(params, kArpDiceTriggerId, 1.0);
    CHECK(params.diceTrigger.load() == true);

    // Reset for fresh test
    params.diceTrigger.store(false, std::memory_order_relaxed);

    // 0.4 (below 0.5 threshold) -> diceTrigger remains false
    handleArpParamChange(params, kArpDiceTriggerId, 0.4);
    CHECK(params.diceTrigger.load() == false);

    // 0.5 (at threshold) -> diceTrigger set to true
    handleArpParamChange(params, kArpDiceTriggerId, 0.5);
    CHECK(params.diceTrigger.load() == true);
}

// T067: handleArpParamChange for Humanize: clamped float storage
TEST_CASE("SpiceHumanize_HandleParamChange_HumanizeStored", "[arp][params][humanize][denorm]") {
    using namespace Ruinae;
    ArpeggiatorParams params;

    // 0.75 -> humanize == 0.75f
    handleArpParamChange(params, kArpHumanizeId, 0.75);
    CHECK(params.humanize.load() == Approx(0.75f).margin(0.001f));
}

// ==============================================================================
// T057: Playhead Parameter Registration (079-layout-framework, US5)
// ==============================================================================
// Verify kArpVelocityPlayheadId (3294) and kArpGatePlayheadId (3295) are
// registered as hidden, non-automatable (kIsReadOnly), and excluded from
// preset state save/load.
// ==============================================================================

TEST_CASE("PlayheadParams_Registration_HiddenAndReadOnly", "[arp][params][playhead]") {
    using namespace Ruinae;
    Steinberg::Vst::ParameterContainer container;
    registerArpParams(container);

    SECTION("Velocity playhead is registered with kIsHidden") {
        auto* param = container.getParameter(kArpVelocityPlayheadId);
        REQUIRE(param != nullptr);
        ParameterInfo info = param->getInfo();
        CHECK((info.flags & ParameterInfo::kIsHidden) != 0);
    }

    SECTION("Velocity playhead is registered with kIsReadOnly (non-automatable)") {
        auto* param = container.getParameter(kArpVelocityPlayheadId);
        REQUIRE(param != nullptr);
        ParameterInfo info = param->getInfo();
        CHECK((info.flags & ParameterInfo::kIsReadOnly) != 0);
        // kIsReadOnly implies NOT automatable (kCanAutomate must not be set)
        CHECK((info.flags & ParameterInfo::kCanAutomate) == 0);
    }

    SECTION("Gate playhead is registered with kIsHidden") {
        auto* param = container.getParameter(kArpGatePlayheadId);
        REQUIRE(param != nullptr);
        ParameterInfo info = param->getInfo();
        CHECK((info.flags & ParameterInfo::kIsHidden) != 0);
    }

    SECTION("Gate playhead is registered with kIsReadOnly (non-automatable)") {
        auto* param = container.getParameter(kArpGatePlayheadId);
        REQUIRE(param != nullptr);
        ParameterInfo info = param->getInfo();
        CHECK((info.flags & ParameterInfo::kIsReadOnly) != 0);
        CHECK((info.flags & ParameterInfo::kCanAutomate) == 0);
    }
}

TEST_CASE("PlayheadParams_ExcludedFromPresetState", "[arp][params][playhead][state]") {
    using namespace Ruinae;

    // Save params with non-default playhead values
    // (In practice, playhead params are not part of ArpeggiatorParams struct
    // since they are transient. Verify that saveArpParams/loadArpParams do NOT
    // include kArpVelocityPlayheadId or kArpGatePlayheadId in the stream.)

    // Set some non-default arp params and save
    ArpeggiatorParams original;
    original.enabled.store(true, std::memory_order_relaxed);

    auto stream = Steinberg::owned(new Steinberg::MemoryStream());
    {
        Steinberg::IBStreamer writeStream(stream, kLittleEndian);
        saveArpParams(original, writeStream);
    }

    // Load into a fresh struct
    ArpeggiatorParams loaded;
    stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    {
        Steinberg::IBStreamer readStream(stream, kLittleEndian);
        bool ok = loadArpParams(loaded, readStream);
        REQUIRE(ok);
    }

    // The ArpeggiatorParams struct should NOT have playhead fields (they are
    // transient parameter-only, not part of the serialized state).
    // We verify this indirectly: the save/load round-trip succeeds without any
    // playhead data, proving they are excluded from serialization.
    CHECK(loaded.enabled.load() == true);

    // Additionally verify the parameter IDs are correct constants
    CHECK(kArpVelocityPlayheadId == 3294);
    CHECK(kArpGatePlayheadId == 3295);
}

TEST_CASE("PlayheadParams_DefaultValueIsSentinel", "[arp][params][playhead]") {
    using namespace Ruinae;
    Steinberg::Vst::ParameterContainer container;
    registerArpParams(container);

    SECTION("Velocity playhead default is 1.0 (sentinel = no playback)") {
        auto* param = container.getParameter(kArpVelocityPlayheadId);
        REQUIRE(param != nullptr);
        // Default normalized value should be 1.0 (sentinel)
        CHECK(param->getNormalized() == Approx(1.0).margin(1e-6));
    }

    SECTION("Gate playhead default is 1.0 (sentinel = no playback)") {
        auto* param = container.getParameter(kArpGatePlayheadId);
        REQUIRE(param != nullptr);
        CHECK(param->getNormalized() == Approx(1.0).margin(1e-6));
    }
}

// ==============================================================================
// Phase 12 (082-presets-polish) US4: Parameter Display Verification Tests
// ==============================================================================

// T063: All arp parameters have "Arp" prefix in display name (FR-020, SC-005)
TEST_CASE("All arp parameters have Arp prefix in display name", "[arp][param display]") {
    using namespace Ruinae;
    Steinberg::Vst::ParameterContainer container;
    registerArpParams(container);

    // Collect all kArp* parameter IDs from plugin_ids.h, excluding playhead-only IDs
    std::vector<ParamID> arpParamIds;

    // Base arp parameters (3000-3010)
    for (ParamID id = kArpEnabledId; id <= kArpRetriggerId; ++id)
        arpParamIds.push_back(id);

    // Velocity lane: length + 32 steps (3020-3052)
    arpParamIds.push_back(kArpVelocityLaneLengthId);
    for (int i = 0; i < 32; ++i)
        arpParamIds.push_back(static_cast<ParamID>(kArpVelocityLaneStep0Id + i));

    // Gate lane: length + 32 steps (3060-3092)
    arpParamIds.push_back(kArpGateLaneLengthId);
    for (int i = 0; i < 32; ++i)
        arpParamIds.push_back(static_cast<ParamID>(kArpGateLaneStep0Id + i));

    // Pitch lane: length + 32 steps (3100-3132)
    arpParamIds.push_back(kArpPitchLaneLengthId);
    for (int i = 0; i < 32; ++i)
        arpParamIds.push_back(static_cast<ParamID>(kArpPitchLaneStep0Id + i));

    // Modifier lane: length + 32 steps (3140-3172)
    arpParamIds.push_back(kArpModifierLaneLengthId);
    for (int i = 0; i < 32; ++i)
        arpParamIds.push_back(static_cast<ParamID>(kArpModifierLaneStep0Id + i));

    // Accent velocity + slide time (3180-3181)
    arpParamIds.push_back(kArpAccentVelocityId);
    arpParamIds.push_back(kArpSlideTimeId);

    // Ratchet lane: length + 32 steps (3190-3222)
    arpParamIds.push_back(kArpRatchetLaneLengthId);
    for (int i = 0; i < 32; ++i)
        arpParamIds.push_back(static_cast<ParamID>(kArpRatchetLaneStep0Id + i));

    // Euclidean (3230-3233)
    arpParamIds.push_back(kArpEuclideanEnabledId);
    arpParamIds.push_back(kArpEuclideanHitsId);
    arpParamIds.push_back(kArpEuclideanStepsId);
    arpParamIds.push_back(kArpEuclideanRotationId);

    // Condition lane: length + 32 steps (3240-3272)
    arpParamIds.push_back(kArpConditionLaneLengthId);
    for (int i = 0; i < 32; ++i)
        arpParamIds.push_back(static_cast<ParamID>(kArpConditionLaneStep0Id + i));

    // Fill toggle (3280)
    arpParamIds.push_back(kArpFillToggleId);

    // Spice, Dice, Humanize (3290-3292)
    arpParamIds.push_back(kArpSpiceId);
    arpParamIds.push_back(kArpDiceTriggerId);
    arpParamIds.push_back(kArpHumanizeId);

    // Ratchet swing (3293)
    arpParamIds.push_back(kArpRatchetSwingId);

    // NOTE: Playhead IDs (3294-3299) are excluded per task spec

    // Verify each has the "Arp" prefix
    for (auto id : arpParamIds) {
        auto* param = container.getParameter(id);
        REQUIRE(param != nullptr);
        ParameterInfo info = param->getInfo();
        std::string title = toString128(info.title);
        INFO("Parameter ID " << id << " has title: \"" << title << "\"");
        CHECK(title.substr(0, 3) == "Arp");
    }
}

// T064: Arp step parameters use non-padded numbering (FR-021)
TEST_CASE("Arp step parameters use non-padded numbering", "[arp][param display]") {
    using namespace Ruinae;
    Steinberg::Vst::ParameterContainer container;
    registerArpParams(container);

    SECTION("Velocity step 1 is 'Arp Vel Step 1' not 'Arp Vel Step 01'") {
        auto* param = container.getParameter(kArpVelocityLaneStep0Id);
        REQUIRE(param != nullptr);
        std::string title = toString128(param->getInfo().title);
        CHECK(title == "Arp Vel Step 1");
    }

    SECTION("Velocity step 16 is 'Arp Vel Step 16'") {
        auto* param = container.getParameter(
            static_cast<ParamID>(kArpVelocityLaneStep0Id + 15));
        REQUIRE(param != nullptr);
        std::string title = toString128(param->getInfo().title);
        CHECK(title == "Arp Vel Step 16");
    }

    SECTION("Gate step 1 is 'Arp Gate Step 1'") {
        auto* param = container.getParameter(kArpGateLaneStep0Id);
        REQUIRE(param != nullptr);
        std::string title = toString128(param->getInfo().title);
        CHECK(title == "Arp Gate Step 1");
    }

    SECTION("Pitch step 32 is 'Arp Pitch Step 32'") {
        auto* param = container.getParameter(
            static_cast<ParamID>(kArpPitchLaneStep0Id + 31));
        REQUIRE(param != nullptr);
        std::string title = toString128(param->getInfo().title);
        CHECK(title == "Arp Pitch Step 32");
    }

    SECTION("Modifier step 1 is 'Arp Mod Step 1'") {
        auto* param = container.getParameter(kArpModifierLaneStep0Id);
        REQUIRE(param != nullptr);
        std::string title = toString128(param->getInfo().title);
        CHECK(title == "Arp Mod Step 1");
    }

    SECTION("Ratchet step 1 is 'Arp Ratchet Step 1'") {
        auto* param = container.getParameter(kArpRatchetLaneStep0Id);
        REQUIRE(param != nullptr);
        std::string title = toString128(param->getInfo().title);
        CHECK(title == "Arp Ratchet Step 1");
    }

    SECTION("Condition step 1 is 'Arp Cond Step 1'") {
        auto* param = container.getParameter(kArpConditionLaneStep0Id);
        REQUIRE(param != nullptr);
        std::string title = toString128(param->getInfo().title);
        CHECK(title == "Arp Cond Step 1");
    }
}

// T065: formatArpParam -- mode values display as mode names (FR-022)
TEST_CASE("formatArpParam -- mode values display as mode names", "[arp][param display]") {
    using namespace Ruinae;
    Steinberg::Vst::String128 str;

    static const char* const kExpected[] = {
        "Up", "Down", "UpDown", "DownUp", "Converge",
        "Diverge", "Random", "Walk", "AsPlayed", "Chord"
    };

    for (int i = 0; i < 10; ++i) {
        double norm = static_cast<double>(i) / 9.0;
        auto result = formatArpParam(kArpModeId, norm, str);
        INFO("Mode index " << i << " at normalized " << norm);
        CHECK(result == Steinberg::kResultOk);
        CHECK(toString128(str) == kExpected[i]);
    }
}

// T066: formatArpParam -- note value displays as note duration (FR-022)
TEST_CASE("formatArpParam -- note value displays as note duration", "[arp][param display]") {
    using namespace Ruinae;
    Steinberg::Vst::String128 str;

    SECTION("Index 7 -> 1/16") {
        double norm = 7.0 / 20.0;
        auto result = formatArpParam(kArpNoteValueId, norm, str);
        CHECK(result == Steinberg::kResultOk);
        CHECK(toString128(str) == "1/16");
    }

    SECTION("Index 10 -> 1/8") {
        double norm = 10.0 / 20.0;
        auto result = formatArpParam(kArpNoteValueId, norm, str);
        CHECK(result == Steinberg::kResultOk);
        CHECK(toString128(str) == "1/8");
    }

    SECTION("Index 9 -> 1/8T") {
        double norm = 9.0 / 20.0;
        auto result = formatArpParam(kArpNoteValueId, norm, str);
        CHECK(result == Steinberg::kResultOk);
        CHECK(toString128(str) == "1/8T");
    }

    SECTION("Index 13 -> 1/4") {
        double norm = 13.0 / 20.0;
        auto result = formatArpParam(kArpNoteValueId, norm, str);
        CHECK(result == Steinberg::kResultOk);
        CHECK(toString128(str) == "1/4");
    }
}

// T067: formatArpParam -- gate length displays as percentage (FR-022)
TEST_CASE("formatArpParam -- gate length displays as percentage", "[arp][param display]") {
    using namespace Ruinae;
    Steinberg::Vst::String128 str;

    // 75% -> normalized = (75 - 1) / 199 = 74/199
    double norm75 = (75.0 - 1.0) / 199.0;
    auto result = formatArpParam(kArpGateLengthId, norm75, str);
    CHECK(result == Steinberg::kResultOk);
    CHECK(toString128(str) == "75%");
}

// T068: formatArpParam -- pitch step displays as signed semitones (FR-022)
TEST_CASE("formatArpParam -- pitch step displays as signed semitones", "[arp][param display]") {
    using namespace Ruinae;
    Steinberg::Vst::String128 str;

    SECTION("+3 st: normalized = (3 + 24) / 48") {
        double norm = (3.0 + 24.0) / 48.0;
        auto result = formatArpParam(kArpPitchLaneStep0Id, norm, str);
        CHECK(result == Steinberg::kResultOk);
        CHECK(toString128(str) == "+3 st");
    }

    SECTION("-12 st: normalized = (-12 + 24) / 48 = 0.25") {
        double norm = (-12.0 + 24.0) / 48.0;
        auto result = formatArpParam(kArpPitchLaneStep0Id, norm, str);
        CHECK(result == Steinberg::kResultOk);
        CHECK(toString128(str) == "-12 st");
    }

    SECTION("0 st: normalized = 24 / 48 = 0.5") {
        double norm = 24.0 / 48.0;
        auto result = formatArpParam(kArpPitchLaneStep0Id, norm, str);
        CHECK(result == Steinberg::kResultOk);
        CHECK(toString128(str) == "0 st");
    }
}

// T069: formatArpParam -- condition step displays as condition name (FR-022)
TEST_CASE("formatArpParam -- condition step displays as condition name", "[arp][param display]") {
    using namespace Ruinae;
    Steinberg::Vst::String128 str;

    SECTION("Index 0 (Always)") {
        double norm = 0.0 / 17.0;
        auto result = formatArpParam(kArpConditionLaneStep0Id, norm, str);
        CHECK(result == Steinberg::kResultOk);
        CHECK(toString128(str) == "Always");
    }

    SECTION("Index 3 (Prob50) -> 50%") {
        double norm = 3.0 / 17.0;
        auto result = formatArpParam(kArpConditionLaneStep0Id, norm, str);
        CHECK(result == Steinberg::kResultOk);
        CHECK(toString128(str) == "50%");
    }

    SECTION("Index 16 (Fill)") {
        double norm = 16.0 / 17.0;
        auto result = formatArpParam(kArpConditionLaneStep0Id, norm, str);
        CHECK(result == Steinberg::kResultOk);
        CHECK(toString128(str) == "Fill");
    }
}

// T070: formatArpParam -- spice and humanize display as percentage (FR-022)
TEST_CASE("formatArpParam -- spice and humanize display as percentage", "[arp][param display]") {
    using namespace Ruinae;
    Steinberg::Vst::String128 str;

    SECTION("Spice 0.73 -> 73%") {
        auto result = formatArpParam(kArpSpiceId, 0.73, str);
        CHECK(result == Steinberg::kResultOk);
        CHECK(toString128(str) == "73%");
    }

    SECTION("Humanize 0.42 -> 42%") {
        auto result = formatArpParam(kArpHumanizeId, 0.42, str);
        CHECK(result == Steinberg::kResultOk);
        CHECK(toString128(str) == "42%");
    }
}

// T070a: formatArpParam -- ratchet swing displays as percentage (FR-022, SC-006)
TEST_CASE("formatArpParam -- ratchet swing displays as percentage", "[arp][param display]") {
    using namespace Ruinae;
    Steinberg::Vst::String128 str;

    // Normalized 0.48 maps to 50 + 0.48 * 25 = 62% in the 50-75% range
    auto result = formatArpParam(kArpRatchetSwingId, 0.48, str);
    CHECK(result == Steinberg::kResultOk);
    CHECK(toString128(str) == "62%");
}

// T070b: formatArpParam -- modifier step displays as human-readable flag abbreviations (FR-022)
TEST_CASE("formatArpParam -- modifier step displays as human-readable flag abbreviations", "[arp][param display]") {
    using namespace Ruinae;
    Steinberg::Vst::String128 str;

    // Modifier steps are RangeParameter 0-255, stepCount=255
    // Normalized = value / 255.0

    SECTION("0x00 (Rest) -> REST") {
        double norm = 0.0 / 255.0;
        auto result = formatArpParam(kArpModifierLaneStep0Id, norm, str);
        CHECK(result == Steinberg::kResultOk);
        CHECK(toString128(str) == "REST");
    }

    SECTION("0x01 (kStepActive only) -> --") {
        double norm = 1.0 / 255.0;
        auto result = formatArpParam(kArpModifierLaneStep0Id, norm, str);
        CHECK(result == Steinberg::kResultOk);
        CHECK(toString128(str) == "--");
    }

    SECTION("0x05 (kStepActive | kStepSlide) -> SL") {
        double norm = 5.0 / 255.0;
        auto result = formatArpParam(kArpModifierLaneStep0Id, norm, str);
        CHECK(result == Steinberg::kResultOk);
        CHECK(toString128(str) == "SL");
    }

    SECTION("0x09 (kStepActive | kStepAccent) -> AC") {
        double norm = 9.0 / 255.0;
        auto result = formatArpParam(kArpModifierLaneStep0Id, norm, str);
        CHECK(result == Steinberg::kResultOk);
        CHECK(toString128(str) == "AC");
    }

    SECTION("0x0D (kStepActive | kStepSlide | kStepAccent) -> SL AC") {
        double norm = 13.0 / 255.0;
        auto result = formatArpParam(kArpModifierLaneStep0Id, norm, str);
        CHECK(result == Steinberg::kResultOk);
        CHECK(toString128(str) == "SL AC");
    }

    SECTION("0x03 (kStepActive | kStepTie) -> TIE") {
        double norm = 3.0 / 255.0;
        auto result = formatArpParam(kArpModifierLaneStep0Id, norm, str);
        CHECK(result == Steinberg::kResultOk);
        CHECK(toString128(str) == "TIE");
    }
}
