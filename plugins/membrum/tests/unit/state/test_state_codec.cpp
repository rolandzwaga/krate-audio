// ==============================================================================
// Membrum state codec tests -- direct round-trip, version rejection, session
// fields, per-pad preset slice.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "state/state_codec.h"
#include "dsp/pad_config.h"

#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/base/ibstream.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace Steinberg;
using Catch::Approx;
using Membrum::State::KitSnapshot;
using Membrum::State::PadSnapshot;
using Membrum::State::PadPresetSnapshot;
using Membrum::State::writeKitBlob;
using Membrum::State::readKitBlob;
using Membrum::State::writePadPresetBlob;
using Membrum::State::readPadPresetBlob;
using Membrum::State::toPadSnapshot;
using Membrum::State::applyPadSnapshot;
using Membrum::State::toPadPresetSnapshot;
using Membrum::State::applyPadPresetSnapshot;

namespace {

/// Build a KitSnapshot with distinct, deterministic, in-range values for
/// every field so round-trip mismatches surface as concrete deltas.
KitSnapshot makePopulatedKit()
{
    KitSnapshot kit;
    kit.maxPolyphony        = 12;
    kit.voiceStealingPolicy = 1;
    kit.selectedPadIndex    = 7;
    kit.globalCoupling      = 0.3;
    kit.snareBuzz           = 0.6;
    kit.tomResonance        = 0.45;
    kit.couplingDelayMs     = 1.25;

    for (std::size_t p = 0; p < kit.pads.size(); ++p)
    {
        auto& pad = kit.pads[p];
        const double base = static_cast<double>(p) / 32.0;
        pad.exciterType = static_cast<Membrum::ExciterType>(p % static_cast<std::size_t>(Membrum::ExciterType::kCount));
        pad.bodyModel   = static_cast<Membrum::BodyModelType>(p % static_cast<std::size_t>(Membrum::BodyModelType::kCount));
        for (std::size_t i = 0; i < pad.sound.size(); ++i)
        {
            pad.sound[i] = std::clamp(0.1 + base + static_cast<double>(i) * 0.013, 0.0, 1.0);
        }
        // Overwrite sound[28]/[29] with the float64 mirror of choke/bus.
        pad.chokeGroup      = static_cast<std::uint8_t>(p % 9);
        pad.outputBus       = static_cast<std::uint8_t>(p % 16);
        pad.sound[28]       = static_cast<double>(pad.chokeGroup);
        pad.sound[29]       = static_cast<double>(pad.outputBus);
        pad.couplingAmount  = std::clamp(0.05 + base, 0.0, 1.0);
        pad.macros[0]       = std::clamp(0.2 + base * 0.5, 0.0, 1.0);
        pad.macros[1]       = std::clamp(0.8 - base * 0.5, 0.0, 1.0);
        pad.macros[2]       = std::clamp(0.3 + base * 0.4, 0.0, 1.0);
        pad.macros[3]       = std::clamp(0.7 - base * 0.3, 0.0, 1.0);
        pad.macros[4]       = std::clamp(0.5 + (base - 0.5) * 0.25, 0.0, 1.0);
    }

    return kit;
}

bool padsEquivalent(const PadSnapshot& a, const PadSnapshot& b, double margin = 1e-12)
{
    if (a.exciterType != b.exciterType) return false;
    if (a.bodyModel   != b.bodyModel)   return false;
    for (std::size_t i = 0; i < a.sound.size(); ++i)
        if (std::abs(a.sound[i] - b.sound[i]) > margin) return false;
    if (a.chokeGroup  != b.chokeGroup)  return false;
    if (a.outputBus   != b.outputBus)   return false;
    if (std::abs(a.couplingAmount - b.couplingAmount) > margin) return false;
    for (std::size_t i = 0; i < a.macros.size(); ++i)
        if (std::abs(a.macros[i] - b.macros[i]) > margin) return false;
    return true;
}

} // namespace

TEST_CASE("state_codec: kit blob round-trip preserves populated snapshot",
          "[state_codec][roundtrip]")
{
    const KitSnapshot src = makePopulatedKit();

    MemoryStream stream;
    REQUIRE(writeKitBlob(&stream, src) == kResultOk);
    stream.seek(0, IBStream::kIBSeekSet, nullptr);

    KitSnapshot dst;
    REQUIRE(readKitBlob(&stream, dst) == kResultOk);

    CHECK(dst.maxPolyphony        == src.maxPolyphony);
    CHECK(dst.voiceStealingPolicy == src.voiceStealingPolicy);
    CHECK(dst.selectedPadIndex    == src.selectedPadIndex);
    CHECK(dst.globalCoupling      == Approx(src.globalCoupling).margin(1e-12));
    CHECK(dst.snareBuzz           == Approx(src.snareBuzz).margin(1e-12));
    CHECK(dst.tomResonance        == Approx(src.tomResonance).margin(1e-12));
    CHECK(dst.couplingDelayMs     == Approx(src.couplingDelayMs).margin(1e-12));

    for (std::size_t p = 0; p < src.pads.size(); ++p)
    {
        INFO("pad " << p);
        CHECK(padsEquivalent(dst.pads[p], src.pads[p]));
    }

    // No session field was emitted by default.
    CHECK(!dst.hasSession);
    CHECK(dst.uiMode == 0);
}

TEST_CASE("state_codec: readKitBlob accepts v6/v7 and rejects others",
          "[state_codec][version]")
{
    // v6..v13 are accepted (older auto-fill defaults for newer slots).
    // Every other version is rejected.
    for (int32 badVersion : { 1, 2, 3, 4, 5, 15, 99 })
    {
        MemoryStream stream;
        stream.write(&badVersion, sizeof(badVersion), nullptr);
        // Pad with zeros so a full read is possible.
        std::vector<std::uint8_t> filler(16384, 0);
        stream.write(filler.data(), static_cast<int32>(filler.size()), nullptr);
        stream.seek(0, IBStream::kIBSeekSet, nullptr);

        KitSnapshot dst;
        INFO("rejecting version " << badVersion);
        CHECK(readKitBlob(&stream, dst) == kResultFalse);
    }
}

TEST_CASE("state_codec: uiMode is optional on read, round-trips when written",
          "[state_codec][session]")
{
    SECTION("written -> read: hasSession=true preserves uiMode")
    {
        KitSnapshot src = makePopulatedKit();
        src.hasSession = true;
        src.uiMode     = 1;

        MemoryStream stream;
        REQUIRE(writeKitBlob(&stream, src) == kResultOk);
        stream.seek(0, IBStream::kIBSeekSet, nullptr);

        KitSnapshot dst;
        REQUIRE(readKitBlob(&stream, dst) == kResultOk);
        CHECK(dst.hasSession);
        CHECK(dst.uiMode == 1);
    }

    SECTION("absent on wire -> hasSession=false, uiMode unchanged")
    {
        KitSnapshot src = makePopulatedKit();
        src.hasSession = false;

        MemoryStream stream;
        REQUIRE(writeKitBlob(&stream, src) == kResultOk);
        stream.seek(0, IBStream::kIBSeekSet, nullptr);

        KitSnapshot dst;
        dst.uiMode = 0;
        REQUIRE(readKitBlob(&stream, dst) == kResultOk);
        CHECK(!dst.hasSession);
        CHECK(dst.uiMode == 0);
    }

    SECTION("hasSession adds exactly 4 bytes (one int32) to the blob")
    {
        KitSnapshot src = makePopulatedKit();

        MemoryStream streamNoSession;
        src.hasSession = false;
        REQUIRE(writeKitBlob(&streamNoSession, src) == kResultOk);
        Steinberg::int64 sizeNoSession = 0;
        streamNoSession.tell(&sizeNoSession);

        MemoryStream streamWithSession;
        src.hasSession = true;
        src.uiMode     = 1;
        REQUIRE(writeKitBlob(&streamWithSession, src) == kResultOk);
        Steinberg::int64 sizeWithSession = 0;
        streamWithSession.tell(&sizeWithSession);

        CHECK(sizeWithSession == sizeNoSession + 4);
    }
}

TEST_CASE("state_codec: per-pad preset round-trip",
          "[state_codec][pad_preset]")
{
    PadPresetSnapshot src;
    src.exciterType = Membrum::ExciterType::NoiseBurst;
    src.bodyModel   = Membrum::BodyModelType::Plate;
    for (std::size_t i = 0; i < src.sound.size(); ++i)
        src.sound[i] = 0.01 + static_cast<double>(i) * 0.015;

    MemoryStream stream;
    REQUIRE(writePadPresetBlob(&stream, src) == kResultOk);

    // Confirm the exact size: 4 (version) + 4 (exciter) + 4 (body) + 51*8 = 420.
    int64 bytes = 0;
    stream.seek(0, IBStream::kIBSeekEnd, &bytes);
    CHECK(bytes == 420);

    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    PadPresetSnapshot dst;
    REQUIRE(readPadPresetBlob(&stream, dst) == kResultOk);
    CHECK(dst.exciterType == src.exciterType);
    CHECK(dst.bodyModel   == src.bodyModel);
    for (std::size_t i = 0; i < src.sound.size(); ++i)
    {
        INFO("sound[" << i << "]");
        CHECK(dst.sound[i] == Approx(src.sound[i]).margin(1e-12));
    }
}

TEST_CASE("state_codec: readPadPresetBlob accepts v1..v6 and rejects others",
          "[state_codec][pad_preset][version]")
{
    // v1..v6 are accepted; others rejected.
    for (int32 badVersion : { 0, 7, 8, 99 })
    {
        MemoryStream stream;
        stream.write(&badVersion, sizeof(badVersion), nullptr);
        std::vector<std::uint8_t> filler(512, 0);
        stream.write(filler.data(), static_cast<int32>(filler.size()), nullptr);
        stream.seek(0, IBStream::kIBSeekSet, nullptr);

        PadPresetSnapshot dst;
        INFO("rejecting pad-preset version " << badVersion);
        CHECK(readPadPresetBlob(&stream, dst) == kResultFalse);
    }
}

TEST_CASE("state_codec: toPadSnapshot/applyPadSnapshot are inverses",
          "[state_codec][pad_snapshot]")
{
    Membrum::PadConfig src;
    src.exciterType        = Membrum::ExciterType::Friction;
    src.bodyModel          = Membrum::BodyModelType::Bell;
    src.material           = 0.123f;
    src.size               = 0.456f;
    src.decay              = 0.789f;
    src.strikePosition     = 0.321f;
    src.level              = 0.654f;
    src.tsFilterType       = 0.5f;
    src.tsFilterCutoff     = 0.8f;
    src.tsFilterResonance  = 0.2f;
    src.tsFilterEnvAmount  = 0.7f;
    src.tsDriveAmount      = 0.15f;
    src.tsFoldAmount       = 0.35f;
    src.tsPitchEnvStart    = 0.4f;
    src.tsPitchEnvEnd      = 0.6f;
    src.tsPitchEnvTime     = 0.25f;
    src.tsPitchEnvCurve    = 0.0f;
    src.tsFilterEnvAttack  = 0.1f;
    src.tsFilterEnvDecay   = 0.2f;
    src.tsFilterEnvSustain = 0.3f;
    src.tsFilterEnvRelease = 0.4f;
    src.modeStretch        = 0.5f;
    src.decaySkew          = 0.6f;
    src.modeInjectAmount   = 0.15f;
    src.nonlinearCoupling  = 0.25f;
    src.morphEnabled       = 1.0f;
    src.morphStart         = 0.2f;
    src.morphEnd           = 0.8f;
    src.morphDuration      = 0.33f;
    src.morphCurve         = 1.0f;
    src.chokeGroup         = 3;
    src.outputBus          = 5;
    src.fmRatio            = 0.4f;
    src.feedbackAmount     = 0.5f;
    src.noiseBurstDuration = 0.6f;
    src.frictionPressure   = 0.7f;
    src.couplingAmount     = 0.85f;
    src.macroTightness     = 0.11f;
    src.macroBrightness    = 0.22f;
    src.macroBodySize      = 0.33f;
    src.macroPunch         = 0.44f;
    src.macroComplexity    = 0.55f;

    const PadSnapshot snap = toPadSnapshot(src);
    Membrum::PadConfig dst;
    applyPadSnapshot(snap, dst);

    CHECK(dst.exciterType        == src.exciterType);
    CHECK(dst.bodyModel          == src.bodyModel);
    CHECK(dst.material           == Approx(src.material).margin(1e-6f));
    CHECK(dst.size               == Approx(src.size).margin(1e-6f));
    CHECK(dst.decay              == Approx(src.decay).margin(1e-6f));
    CHECK(dst.strikePosition     == Approx(src.strikePosition).margin(1e-6f));
    CHECK(dst.level              == Approx(src.level).margin(1e-6f));
    CHECK(dst.tsFilterCutoff     == Approx(src.tsFilterCutoff).margin(1e-6f));
    CHECK(dst.modeStretch        == Approx(src.modeStretch).margin(1e-6f));
    CHECK(dst.morphEnabled       == Approx(src.morphEnabled).margin(1e-6f));
    CHECK(dst.chokeGroup         == src.chokeGroup);
    CHECK(dst.outputBus          == src.outputBus);
    CHECK(dst.fmRatio            == Approx(src.fmRatio).margin(1e-6f));
    CHECK(dst.frictionPressure   == Approx(src.frictionPressure).margin(1e-6f));
    CHECK(dst.couplingAmount     == Approx(src.couplingAmount).margin(1e-6f));
    CHECK(dst.macroTightness     == Approx(src.macroTightness).margin(1e-6f));
    CHECK(dst.macroComplexity    == Approx(src.macroComplexity).margin(1e-6f));
}

TEST_CASE("state_codec: applyPadPresetSnapshot does NOT modify choke/bus/coupling/macros",
          "[state_codec][pad_preset][fr061]")
{
    Membrum::PadConfig existing;
    existing.chokeGroup      = 7;
    existing.outputBus       = 4;
    existing.couplingAmount  = 0.91f;
    existing.macroTightness  = 0.13f;
    existing.macroBrightness = 0.87f;

    PadPresetSnapshot snap;
    snap.exciterType = Membrum::ExciterType::Impulse;
    snap.bodyModel   = Membrum::BodyModelType::Membrane;
    // Deliberately populate sound[28]/[29] with values that, if applied, would
    // overwrite the existing choke/bus. They must be ignored.
    for (std::size_t i = 0; i < snap.sound.size(); ++i)
        snap.sound[i] = 0.5;
    snap.sound[28] = 0.0;  // would map to chokeGroup 0 if applied
    snap.sound[29] = 0.0;  // would map to outputBus 0 if applied

    applyPadPresetSnapshot(snap, existing);

    CHECK(existing.chokeGroup      == 7);
    CHECK(existing.outputBus       == 4);
    CHECK(existing.couplingAmount  == Approx(0.91f).margin(1e-6f));
    CHECK(existing.macroTightness  == Approx(0.13f).margin(1e-6f));
    CHECK(existing.macroBrightness == Approx(0.87f).margin(1e-6f));
    // Sound fields should have been overwritten.
    CHECK(existing.material == Approx(0.5f).margin(1e-6f));
    CHECK(existing.level    == Approx(0.5f).margin(1e-6f));
}
