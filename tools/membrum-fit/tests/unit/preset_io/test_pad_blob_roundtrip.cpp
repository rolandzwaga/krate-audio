// Per-pad preset blob round-trip: write a PadConfig through the state
// codec, read back, verify every sound field matches bitwise.
#include "state/state_codec.h"
#include "dsp/default_kit.h"

#include "pluginterfaces/base/ibstream.h"
#include "public.sdk/source/common/memorystream.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <array>

TEST_CASE("Per-pad preset blob: write -> read round-trips every sound field") {
    Membrum::PadConfig src{};
    Membrum::DefaultKit::applyTemplate(src, Membrum::DrumTemplate::Kick);
    src.material = 0.42f;
    src.size     = 0.73f;
    src.decay    = 0.55f;
    src.tsFilterCutoff = 0.61f;
    src.tsPitchEnvStart = 0.31f;

    auto snap = Membrum::State::toPadPresetSnapshot(src);
    Steinberg::MemoryStream stream;
    REQUIRE(Membrum::State::writePadPresetBlob(&stream, snap) == Steinberg::kResultOk);

    Steinberg::int64 ignored = 0;
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, &ignored);

    Membrum::State::PadPresetSnapshot back{};
    REQUIRE(Membrum::State::readPadPresetBlob(&stream, back) == Steinberg::kResultOk);
    REQUIRE(back.exciterType == snap.exciterType);
    REQUIRE(back.bodyModel   == snap.bodyModel);
    for (std::size_t i = 0; i < snap.sound.size(); ++i) {
        REQUIRE(back.sound[i] == Catch::Approx(snap.sound[i]).margin(1e-9));
    }
}
