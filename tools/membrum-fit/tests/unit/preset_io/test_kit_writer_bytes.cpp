// Byte-exact kit writer regression. Writes the same KitSnapshot two ways:
//   (a) via MembrumFit::PresetIO::writeKitPreset -> file, read back
//   (b) via the state codec directly -> memory stream
// The .vstpreset container wraps (b) with a VST3 header + chunk list, so we
// can't memcmp the whole file; instead we verify that the component-state
// chunk inside (a) is byte-identical to (b).
#include "src/preset_io/kit_preset_writer.h"

#include "preset/membrum_preset_container.h"
#include "state/state_codec.h"

#include "dsp/default_kit.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/vstpresetfile.h"
#include "pluginterfaces/base/ibstream.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {

std::vector<char> slurp(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::vector<char> buf((std::istreambuf_iterator<char>(f)),
                           std::istreambuf_iterator<char>());
    return buf;
}

}  // namespace

TEST_CASE("Kit writer: component-state bytes match direct codec output") {
    std::array<Membrum::PadConfig, 32> pads{};
    for (std::size_t i = 0; i < pads.size(); ++i) {
        Membrum::DefaultKit::applyTemplate(pads[i],
            (i % 6 == 0) ? Membrum::DrumTemplate::Kick
          : (i % 6 == 1) ? Membrum::DrumTemplate::Snare
          : (i % 6 == 2) ? Membrum::DrumTemplate::Tom
          : (i % 6 == 3) ? Membrum::DrumTemplate::Hat
          : (i % 6 == 4) ? Membrum::DrumTemplate::Cymbal
                         : Membrum::DrumTemplate::Perc);
    }

    const auto outPath = std::filesystem::temp_directory_path() / "membrum_fit_kit_bytes.vstpreset";
    REQUIRE(MembrumFit::PresetIO::writeKitPreset(outPath, pads, "ByteExactTest", "Acoustic"));

    // Reconstruct the same blob via the codec directly.
    Membrum::State::KitSnapshot kit{};
    kit.maxPolyphony = 8;
    kit.voiceStealingPolicy = 0;
    for (std::size_t i = 0; i < pads.size(); ++i) kit.pads[i] = Membrum::State::toPadSnapshot(pads[i]);
    kit.globalCoupling = 0.35;
    kit.snareBuzz      = 0.4;
    kit.tomResonance   = 0.3;
    kit.couplingDelayMs = 1.0;
    kit.selectedPadIndex = 0;
    kit.hasSession = false;

    Steinberg::MemoryStream ref;
    REQUIRE(Membrum::State::writeKitBlob(&ref, kit) == Steinberg::kResultOk);
    Steinberg::int64 refSize = 0;
    ref.tell(&refSize);
    REQUIRE(refSize > 0);
    std::vector<char> refBytes(static_cast<std::size_t>(refSize));
    Steinberg::int64 seeked = 0;
    ref.seek(0, Steinberg::IBStream::kIBSeekSet, &seeked);
    Steinberg::int32 read = 0;
    ref.read(refBytes.data(), static_cast<Steinberg::int32>(refSize), &read);
    REQUIRE(read == static_cast<Steinberg::int32>(refSize));

    // Read the component-state chunk out of the .vstpreset file.
    const auto fileBytes = slurp(outPath);
    REQUIRE(!fileBytes.empty());

    // Search for the reference blob as a substring inside the .vstpreset.
    // PresetFile::savePreset writes component state verbatim before the chunk
    // list trailer; a direct byte scan is the simplest invariant check.
    auto matches = [&](std::size_t off) {
        if (off + refBytes.size() > fileBytes.size()) return false;
        for (std::size_t i = 0; i < refBytes.size(); ++i)
            if (fileBytes[off + i] != refBytes[i]) return false;
        return true;
    };
    bool found = false;
    for (std::size_t off = 0; off + refBytes.size() <= fileBytes.size(); ++off) {
        if (matches(off)) { found = true; break; }
    }
    REQUIRE(found);

    std::error_code ec;
    std::filesystem::remove(outPath, ec);
}
