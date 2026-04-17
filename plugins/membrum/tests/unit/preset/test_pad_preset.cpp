// ==============================================================================
// Per-pad preset save/load tests (Phase 6 / T039)
// ==============================================================================
// Verifies:
//   - Pad preset StateProvider produces 284-byte blob
//     (version int32 + exciterType int32 + bodyModel int32 + 34 float64)
//   - Pad preset LoadProvider applies to selected pad only, other 31 unchanged
//   - Choke group and output bus are NOT modified by pad preset load
//   - Pad preset loaded onto pad 15 matches original pad 1 sound params
//   - Truncated/corrupted pad preset blob fails gracefully
//   - Pad preset subcategory directory structure matches drum types
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "controller/controller.h"
#include "plugin_ids.h"
#include "dsp/pad_config.h"
#include "dsp/exciter_type.h"
#include "dsp/body_model_type.h"
#include "preset/membrum_preset_config.h"

#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/base/ibstream.h"

#include <array>
#include <cstdint>
#include <cstring>

using namespace Steinberg;
using namespace Steinberg::Vst;
using Catch::Approx;

namespace {

constexpr int64 kPadPresetBytes = 380;  // v4: version(4) + exciter(4) + body(4) + 46*float64(368)

/// Number of sound params serialized as float64 (offsets 2-35).
constexpr int kPadPresetSoundParamCount = 34;

/// Build a pad preset blob manually for testing load.
/// Writes version=1, exciterType, bodyModel, and 34 float64 sound values.
MemoryStream* buildPadPresetBlob(int32 exciterType, int32 bodyModel,
                                  double soundParams[34])
{
    auto* stream = new MemoryStream();
    int32 version = 1;
    stream->write(&version, sizeof(version), nullptr);
    stream->write(&exciterType, sizeof(exciterType), nullptr);
    stream->write(&bodyModel, sizeof(bodyModel), nullptr);
    for (int i = 0; i < 34; ++i)
        stream->write(&soundParams[i], sizeof(soundParams[i]), nullptr);
    stream->seek(0, IBStream::kIBSeekSet, nullptr);
    return stream;
}

} // namespace

// ==============================================================================
// T039: Pad preset StateProvider produces exactly 348 bytes (v2 with Phase 7 slots)
// ==============================================================================

TEST_CASE("Pad preset: StateProvider produces exactly 348 bytes",
          "[membrum][preset][pad_preset]")
{
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    // Set selected pad to pad 5 so we know which pad is being serialized
    controller.setParamNormalized(Membrum::kSelectedPadId, 5.0 / 31.0);

    IBStream* stream = controller.padPresetStateProvider();
    REQUIRE(stream != nullptr);

    int64 pos = 0;
    stream->tell(&pos);

    // Stream should be seeked to beginning (ready to read) -- check size by seeking to end
    stream->seek(0, IBStream::kIBSeekEnd, &pos);
    CHECK(pos == kPadPresetBytes);

    stream->release();
    REQUIRE(controller.terminate() == kResultOk);
}

TEST_CASE("Pad preset: blob format is version + exciterType + bodyModel + 46 float64",
          "[membrum][preset][pad_preset]")
{
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    // Configure pad 0 with known values
    const auto padExcId = static_cast<ParamID>(
        Membrum::padParamId(0, Membrum::kPadExciterType));
    const auto padBodyId = static_cast<ParamID>(
        Membrum::padParamId(0, Membrum::kPadBodyModel));
    const auto padMatId = static_cast<ParamID>(
        Membrum::padParamId(0, Membrum::kPadMaterial));

    // Set exciter to NoiseBurst (2), body to Plate (1), material to 0.75
    controller.setParamNormalized(padExcId,
        (2.0 + 0.5) / static_cast<double>(Membrum::ExciterType::kCount));
    controller.setParamNormalized(padBodyId,
        (1.0 + 0.5) / static_cast<double>(Membrum::BodyModelType::kCount));
    controller.setParamNormalized(padMatId, 0.75);

    // Select pad 0
    controller.setParamNormalized(Membrum::kSelectedPadId, 0.0);

    IBStream* stream = controller.padPresetStateProvider();
    REQUIRE(stream != nullptr);

    // Read back the blob
    stream->seek(0, IBStream::kIBSeekSet, nullptr);

    int32 version = 0;
    stream->read(&version, sizeof(version), nullptr);
    CHECK(version == 4);

    int32 exciterTypeI32 = -1;
    stream->read(&exciterTypeI32, sizeof(exciterTypeI32), nullptr);
    CHECK(exciterTypeI32 == 2);  // NoiseBurst

    int32 bodyModelI32 = -1;
    stream->read(&bodyModelI32, sizeof(bodyModelI32), nullptr);
    CHECK(bodyModelI32 == 1);  // Plate

    // First sound param (offset 2 = material) should be 0.75
    double materialVal = 0.0;
    stream->read(&materialVal, sizeof(materialVal), nullptr);
    CHECK(materialVal == Approx(0.75).margin(1e-6));

    stream->release();
    REQUIRE(controller.terminate() == kResultOk);
}

// ==============================================================================
// T039: LoadProvider applies to selected pad only
// ==============================================================================

TEST_CASE("Pad preset: LoadProvider applies to selected pad only, others unchanged",
          "[membrum][preset][pad_preset]")
{
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    // Set pad 5 material to a distinctive value
    const auto pad5MatId = static_cast<ParamID>(
        Membrum::padParamId(5, Membrum::kPadMaterial));
    controller.setParamNormalized(pad5MatId, 0.123);

    // Record pad 3's material before load
    const auto pad3MatId = static_cast<ParamID>(
        Membrum::padParamId(3, Membrum::kPadMaterial));
    const double pad3Before = controller.getParamNormalized(pad3MatId);

    // Build a pad preset with material = 0.99
    double soundParams[34] = {};
    soundParams[0] = 0.99;  // material (offset 2 -> index 0 in sound array)
    for (int i = 1; i < 34; ++i)
        soundParams[i] = 0.5;  // fill rest with 0.5

    auto* stream = buildPadPresetBlob(0, 0, soundParams);

    // Select pad 5 and load
    controller.setParamNormalized(Membrum::kSelectedPadId, 5.0 / 31.0);
    bool ok = controller.padPresetLoadProvider(stream);
    CHECK(ok);

    // Pad 5 material should now be 0.99
    CHECK(controller.getParamNormalized(pad5MatId) == Approx(0.99).margin(1e-6));

    // Pad 3 material should be unchanged
    CHECK(controller.getParamNormalized(pad3MatId) == Approx(pad3Before).margin(1e-9));

    stream->release();
    REQUIRE(controller.terminate() == kResultOk);
}

// ==============================================================================
// T039: Choke group and output bus NOT modified by pad preset load
// ==============================================================================

TEST_CASE("Pad preset: choke group and output bus are NOT modified by load",
          "[membrum][preset][pad_preset]")
{
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    // Set pad 2 choke group to 5 and output bus to 3
    const auto pad2ChokeId = static_cast<ParamID>(
        Membrum::padParamId(2, Membrum::kPadChokeGroup));
    const auto pad2BusId = static_cast<ParamID>(
        Membrum::padParamId(2, Membrum::kPadOutputBus));
    controller.setParamNormalized(pad2ChokeId, 5.0 / 8.0);
    controller.setParamNormalized(pad2BusId, 3.0 / 15.0);

    const double chokeBefore = controller.getParamNormalized(pad2ChokeId);
    const double busBefore = controller.getParamNormalized(pad2BusId);

    // Build a pad preset blob (choke/bus positions contain arbitrary values)
    double soundParams[34] = {};
    for (int i = 0; i < 34; ++i)
        soundParams[i] = 0.5;
    // Positions 28 and 29 in the sound array correspond to offsets 30-31 (choke/bus)
    soundParams[28] = 7.0;   // choke group as float64 in kit format
    soundParams[29] = 12.0;  // output bus as float64 in kit format

    auto* stream = buildPadPresetBlob(0, 0, soundParams);

    // Select pad 2 and load
    controller.setParamNormalized(Membrum::kSelectedPadId, 2.0 / 31.0);
    bool ok = controller.padPresetLoadProvider(stream);
    CHECK(ok);

    // Choke group and output bus should be UNCHANGED
    CHECK(controller.getParamNormalized(pad2ChokeId) == Approx(chokeBefore).margin(1e-9));
    CHECK(controller.getParamNormalized(pad2BusId) == Approx(busBefore).margin(1e-9));

    stream->release();
    REQUIRE(controller.terminate() == kResultOk);
}

// ==============================================================================
// T039: Pad preset loaded onto pad 15 matches original pad 1 sound params
// ==============================================================================

TEST_CASE("Pad preset: round-trip from pad 1 to pad 15 matches all sound params",
          "[membrum][preset][pad_preset]")
{
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    // Configure pad 1 with distinctive values
    const int srcPad = 1;
    const auto excId = static_cast<ParamID>(
        Membrum::padParamId(srcPad, Membrum::kPadExciterType));
    const auto bodyId = static_cast<ParamID>(
        Membrum::padParamId(srcPad, Membrum::kPadBodyModel));

    // Set exciter=FMSine(3), body=Tube(3)
    controller.setParamNormalized(excId,
        (3.0 + 0.5) / static_cast<double>(Membrum::ExciterType::kCount));
    controller.setParamNormalized(bodyId,
        (3.0 + 0.5) / static_cast<double>(Membrum::BodyModelType::kCount));

    // Set various sound params to distinctive values
    const int offsets[] = {
        Membrum::kPadMaterial, Membrum::kPadSize, Membrum::kPadDecay,
        Membrum::kPadStrikePosition, Membrum::kPadLevel,
        Membrum::kPadTSFilterCutoff, Membrum::kPadTSFilterResonance,
        Membrum::kPadModeStretch, Membrum::kPadMorphEnabled,
        Membrum::kPadFMRatio, Membrum::kPadFeedbackAmount,
    };
    const double values[] = {
        0.11, 0.22, 0.33, 0.44, 0.55, 0.66, 0.77, 0.88, 1.0, 0.91, 0.82,
    };
    for (int i = 0; i < 11; ++i)
    {
        const auto pid = static_cast<ParamID>(
            Membrum::padParamId(srcPad, offsets[i]));
        controller.setParamNormalized(pid, values[i]);
    }

    // Save pad preset from pad 1
    controller.setParamNormalized(Membrum::kSelectedPadId, 1.0 / 31.0);
    IBStream* stream = controller.padPresetStateProvider();
    REQUIRE(stream != nullptr);

    // Load onto pad 15
    const int dstPad = 15;
    controller.setParamNormalized(Membrum::kSelectedPadId, 15.0 / 31.0);
    stream->seek(0, IBStream::kIBSeekSet, nullptr);
    bool ok = controller.padPresetLoadProvider(stream);
    CHECK(ok);

    // Verify pad 15 matches pad 1 for all distinctive values
    for (int i = 0; i < 11; ++i)
    {
        const auto srcId = static_cast<ParamID>(
            Membrum::padParamId(srcPad, offsets[i]));
        const auto dstId = static_cast<ParamID>(
            Membrum::padParamId(dstPad, offsets[i]));
        INFO("Offset " << offsets[i]);
        CHECK(controller.getParamNormalized(dstId) ==
              Approx(controller.getParamNormalized(srcId)).margin(1e-6));
    }

    // Also verify exciter type and body model match
    const auto dstExcId = static_cast<ParamID>(
        Membrum::padParamId(dstPad, Membrum::kPadExciterType));
    const auto dstBodyId = static_cast<ParamID>(
        Membrum::padParamId(dstPad, Membrum::kPadBodyModel));
    CHECK(controller.getParamNormalized(dstExcId) ==
          Approx(controller.getParamNormalized(excId)).margin(1e-6));
    CHECK(controller.getParamNormalized(dstBodyId) ==
          Approx(controller.getParamNormalized(bodyId)).margin(1e-6));

    stream->release();
    REQUIRE(controller.terminate() == kResultOk);
}

// ==============================================================================
// T039: Truncated/corrupted pad preset blob fails gracefully
// ==============================================================================

TEST_CASE("Pad preset: truncated blob fails gracefully",
          "[membrum][preset][pad_preset]")
{
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    // Build a truncated blob: only version + exciterType (8 bytes, missing bodyModel + sound params)
    auto* stream = new MemoryStream();
    int32 version = 1;
    stream->write(&version, sizeof(version), nullptr);
    int32 exciterType = 0;
    stream->write(&exciterType, sizeof(exciterType), nullptr);
    // Missing: bodyModel and all 34 float64 values
    stream->seek(0, IBStream::kIBSeekSet, nullptr);

    controller.setParamNormalized(Membrum::kSelectedPadId, 0.0);
    bool ok = controller.padPresetLoadProvider(stream);
    CHECK_FALSE(ok);

    // Pad 0 should be unchanged (or at least not corrupted)
    // Note: partial reads may have modified some params, but the function should
    // ideally not apply partial state. We at least verify it didn't crash.

    stream->release();
    REQUIRE(controller.terminate() == kResultOk);
}

TEST_CASE("Pad preset: wrong version fails gracefully",
          "[membrum][preset][pad_preset]")
{
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    // Build a blob with version=99
    double soundParams[34] = {};
    for (int i = 0; i < 34; ++i)
        soundParams[i] = 0.5;

    auto* stream = new MemoryStream();
    int32 version = 99;
    stream->write(&version, sizeof(version), nullptr);
    int32 et = 0;
    stream->write(&et, sizeof(et), nullptr);
    int32 bm = 0;
    stream->write(&bm, sizeof(bm), nullptr);
    for (int i = 0; i < 34; ++i)
        stream->write(&soundParams[i], sizeof(soundParams[i]), nullptr);
    stream->seek(0, IBStream::kIBSeekSet, nullptr);

    controller.setParamNormalized(Membrum::kSelectedPadId, 0.0);
    bool ok = controller.padPresetLoadProvider(stream);
    CHECK_FALSE(ok);

    stream->release();
    REQUIRE(controller.terminate() == kResultOk);
}

// ==============================================================================
// T039: Pad preset subcategory directory structure
// ==============================================================================

TEST_CASE("Pad preset: subcategories match drum types",
          "[membrum][preset][pad_preset]")
{
    // Spec 141 Phase 6 (T052): pad subcategories match the controller's
    // preset browser tab list (`Factory`/`User` tabs come from the browser
    // view itself): Kick, Snare, Tom, Hat, Cymbal, Perc, Tonal, FX.
    auto cfg = Membrum::padPresetConfig();
    REQUIRE(cfg.subcategoryNames.size() == 8);
    CHECK(cfg.subcategoryNames[0] == "Kick");
    CHECK(cfg.subcategoryNames[1] == "Snare");
    CHECK(cfg.subcategoryNames[2] == "Tom");
    CHECK(cfg.subcategoryNames[3] == "Hat");
    CHECK(cfg.subcategoryNames[4] == "Cymbal");
    CHECK(cfg.subcategoryNames[5] == "Perc");
    CHECK(cfg.subcategoryNames[6] == "Tonal");
    CHECK(cfg.subcategoryNames[7] == "FX");
}
