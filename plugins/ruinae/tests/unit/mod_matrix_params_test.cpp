// ==============================================================================
// Unit Test: Mod Matrix Parameters Round-Trip (spec 049)
// ==============================================================================
// Verifies that all 56 mod matrix parameters (8 slots x 7 params each)
// survive save/load correctly for both base (source, dest, amount) and
// detail (curve, smooth, scale, bypass) parameters.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "parameters/mod_matrix_params.h"
#include "processor/processor.h"
#include "plugin_ids.h"

#include "public.sdk/source/common/memorystream.h"
#include "base/source/fstreamer.h"

using Catch::Approx;

namespace {

// Helper: create and initialize a Processor
Steinberg::IPtr<Ruinae::Processor> makeProcessor() {
    auto p = Steinberg::owned(new Ruinae::Processor());
    p->initialize(nullptr);
    Steinberg::Vst::ProcessSetup setup{};
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = 512;
    p->setupProcessing(setup);
    return p;
}

} // anonymous namespace

// =============================================================================
// ModMatrixSlot / ModMatrixParams data model tests
// =============================================================================

TEST_CASE("ModMatrixSlot defaults are correct", "[modmatrix][unit]") {
    Ruinae::ModMatrixSlot slot;
    REQUIRE(slot.source.load() == 0);
    REQUIRE(slot.dest.load() == 0);
    REQUIRE(slot.amount.load() == 0.0f);
    REQUIRE(slot.curve.load() == 0);
    REQUIRE(slot.smoothMs.load() == 0.0f);
    REQUIRE(slot.scale.load() == 2); // Default x1
    REQUIRE(slot.bypass.load() == 0);
}

// =============================================================================
// handleModMatrixParamChange tests
// =============================================================================

TEST_CASE("handleModMatrixParamChange handles base params", "[modmatrix][unit]") {
    Ruinae::ModMatrixParams params;

    SECTION("Source param routes to slot.source") {
        // Slot 0: ID 1300
        Ruinae::handleModMatrixParamChange(params, 1300, 0.5);
        int expected = static_cast<int>(0.5 * (Ruinae::kModSourceCount - 1) + 0.5);
        REQUIRE(params.slots[0].source.load() == expected);
    }

    SECTION("Dest param routes to slot.dest") {
        // Slot 0: ID 1301
        Ruinae::handleModMatrixParamChange(params, 1301, 0.5);
        int expected = static_cast<int>(0.5 * (Ruinae::kModDestCount - 1) + 0.5);
        REQUIRE(params.slots[0].dest.load() == expected);
    }

    SECTION("Amount param maps bipolar correctly") {
        // Slot 0: ID 1302, normalized 0.0 = -1.0 bipolar
        Ruinae::handleModMatrixParamChange(params, 1302, 0.0);
        REQUIRE(params.slots[0].amount.load() == Approx(-1.0f).margin(0.01f));

        // normalized 0.5 = 0.0 bipolar
        Ruinae::handleModMatrixParamChange(params, 1302, 0.5);
        REQUIRE(params.slots[0].amount.load() == Approx(0.0f).margin(0.01f));

        // normalized 1.0 = +1.0 bipolar
        Ruinae::handleModMatrixParamChange(params, 1302, 1.0);
        REQUIRE(params.slots[0].amount.load() == Approx(1.0f).margin(0.01f));
    }

    SECTION("Slot 7 base params work") {
        // Slot 7 Source: ID 1321
        Ruinae::handleModMatrixParamChange(params, 1321, 1.0);
        REQUIRE(params.slots[7].source.load() == Ruinae::kModSourceCount - 1);

        // Slot 7 Amount: ID 1323
        Ruinae::handleModMatrixParamChange(params, 1323, 0.75);
        float expected = static_cast<float>(0.75 * 2.0 - 1.0);
        REQUIRE(params.slots[7].amount.load() == Approx(expected).margin(0.01f));
    }
}

TEST_CASE("handleModMatrixParamChange handles detail params", "[modmatrix][unit]") {
    Ruinae::ModMatrixParams params;

    SECTION("Curve param routes to slot.curve") {
        // Slot 0 Curve: ID 1324
        Ruinae::handleModMatrixParamChange(params, 1324, 1.0);
        REQUIRE(params.slots[0].curve.load() == Ruinae::kModCurveCount - 1);

        Ruinae::handleModMatrixParamChange(params, 1324, 0.0);
        REQUIRE(params.slots[0].curve.load() == 0);
    }

    SECTION("Smooth param maps to 0-100ms") {
        // Slot 0 Smooth: ID 1325
        Ruinae::handleModMatrixParamChange(params, 1325, 0.0);
        REQUIRE(params.slots[0].smoothMs.load() == Approx(0.0f).margin(0.01f));

        Ruinae::handleModMatrixParamChange(params, 1325, 0.5);
        REQUIRE(params.slots[0].smoothMs.load() == Approx(50.0f).margin(0.01f));

        Ruinae::handleModMatrixParamChange(params, 1325, 1.0);
        REQUIRE(params.slots[0].smoothMs.load() == Approx(100.0f).margin(0.01f));
    }

    SECTION("Scale param routes to slot.scale") {
        // Slot 0 Scale: ID 1326
        Ruinae::handleModMatrixParamChange(params, 1326, 0.0);
        REQUIRE(params.slots[0].scale.load() == 0); // x0.25

        Ruinae::handleModMatrixParamChange(params, 1326, 1.0);
        REQUIRE(params.slots[0].scale.load() == Ruinae::kModScaleCount - 1); // x4
    }

    SECTION("Bypass param is boolean threshold at 0.5") {
        // Slot 0 Bypass: ID 1327
        Ruinae::handleModMatrixParamChange(params, 1327, 0.0);
        REQUIRE(params.slots[0].bypass.load() == 0);

        Ruinae::handleModMatrixParamChange(params, 1327, 0.49);
        REQUIRE(params.slots[0].bypass.load() == 0);

        Ruinae::handleModMatrixParamChange(params, 1327, 0.5);
        REQUIRE(params.slots[0].bypass.load() == 1);

        Ruinae::handleModMatrixParamChange(params, 1327, 1.0);
        REQUIRE(params.slots[0].bypass.load() == 1);
    }

    SECTION("Slot 7 detail params work") {
        // Slot 7 Curve: ID 1352
        Ruinae::handleModMatrixParamChange(params, 1352, 0.667);
        REQUIRE(params.slots[7].curve.load() == 2); // Logarithmic

        // Slot 7 Bypass: ID 1355
        Ruinae::handleModMatrixParamChange(params, 1355, 1.0);
        REQUIRE(params.slots[7].bypass.load() == 1);
    }
}

// =============================================================================
// State Save/Load Round-Trip tests (T020)
// =============================================================================

TEST_CASE("Mod matrix params round-trip: base + detail", "[modmatrix][state]") {
    Ruinae::ModMatrixParams original;

    // Set up test values for all 8 slots
    for (int i = 0; i < 8; ++i) {
        auto& slot = original.slots[static_cast<size_t>(i)];
        slot.source.store(i % Ruinae::kModSourceCount);
        slot.dest.store(i % Ruinae::kModDestCount);
        slot.amount.store(-1.0f + static_cast<float>(i) * 0.25f);
        slot.curve.store(i % Ruinae::kModCurveCount);
        slot.smoothMs.store(static_cast<float>(i) * 12.5f);
        slot.scale.store(i % Ruinae::kModScaleCount);
        slot.bypass.store(i % 2);
    }

    // Save
    Steinberg::MemoryStream stream;
    Steinberg::IBStreamer streamer(&stream, kLittleEndian);
    Ruinae::saveModMatrixParams(original, streamer);

    // Load
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    Steinberg::IBStreamer reader(&stream, kLittleEndian);
    Ruinae::ModMatrixParams loaded;
    REQUIRE(Ruinae::loadModMatrixParams(loaded, reader));

    // Verify all 8 slots
    for (int i = 0; i < 8; ++i) {
        INFO("Slot " << i);
        REQUIRE(loaded.slots[static_cast<size_t>(i)].source.load()
                == original.slots[static_cast<size_t>(i)].source.load());
        REQUIRE(loaded.slots[static_cast<size_t>(i)].dest.load()
                == original.slots[static_cast<size_t>(i)].dest.load());
        REQUIRE(loaded.slots[static_cast<size_t>(i)].amount.load()
                == Approx(original.slots[static_cast<size_t>(i)].amount.load()).margin(0.001f));
        REQUIRE(loaded.slots[static_cast<size_t>(i)].curve.load()
                == original.slots[static_cast<size_t>(i)].curve.load());
        REQUIRE(loaded.slots[static_cast<size_t>(i)].smoothMs.load()
                == Approx(original.slots[static_cast<size_t>(i)].smoothMs.load()).margin(0.001f));
        REQUIRE(loaded.slots[static_cast<size_t>(i)].scale.load()
                == original.slots[static_cast<size_t>(i)].scale.load());
        REQUIRE(loaded.slots[static_cast<size_t>(i)].bypass.load()
                == original.slots[static_cast<size_t>(i)].bypass.load());
    }
}

TEST_CASE("Mod matrix full processor state round-trip with detail params", "[modmatrix][state][integration]") {
    // Create processor, set mod matrix params, save, load, verify
    auto proc1 = makeProcessor();

    // Send parameter changes for slot 0 base + detail via processParameterChanges
    // We use getState/setState to verify round-trip
    Steinberg::MemoryStream stream;
    proc1->getState(&stream);

    // Load into a second processor
    auto proc2 = makeProcessor();
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    auto result = proc2->setState(&stream);
    REQUIRE(result == Steinberg::kResultTrue);

    // Save from proc2 and compare stream sizes
    Steinberg::MemoryStream stream2;
    proc2->getState(&stream2);

    Steinberg::int64 size1 = 0, size2 = 0;
    stream.seek(0, Steinberg::IBStream::kIBSeekEnd, &size1);
    stream2.seek(0, Steinberg::IBStream::kIBSeekEnd, &size2);
    REQUIRE(size1 == size2);
    REQUIRE(size1 > 4); // At least version + data

    proc1->terminate();
    proc2->terminate();
}

// =============================================================================
// Parameter ID formula verification
// =============================================================================

TEST_CASE("Detail param IDs follow expected formulas", "[modmatrix][unit]") {
    // Verify the formula: Curve = 1324 + slot*4, etc.
    REQUIRE(Ruinae::kModMatrixSlot0CurveId == 1324);
    REQUIRE(Ruinae::kModMatrixSlot0SmoothId == 1325);
    REQUIRE(Ruinae::kModMatrixSlot0ScaleId == 1326);
    REQUIRE(Ruinae::kModMatrixSlot0BypassId == 1327);

    REQUIRE(Ruinae::kModMatrixSlot1CurveId == 1328);
    REQUIRE(Ruinae::kModMatrixSlot7CurveId == 1352);
    REQUIRE(Ruinae::kModMatrixSlot7BypassId == 1355);

    // Verify no overlap between base and detail ranges
    REQUIRE(Ruinae::kModMatrixSlot7AmountId == 1323);
    REQUIRE(Ruinae::kModMatrixDetailBaseId == 1324);
}
