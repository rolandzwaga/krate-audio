// ==============================================================================
// Factory kit preset round-trip test.
//
// Every resources/presets/**/*.vstpreset file must:
//   1. Parse through the standard VST3 PresetFile loader.
//   2. Carry Membrum's processor UID.
//   3. Hold a component-state chunk that reads + round-trips bit-exact
//      through readKitBlob / writeKitBlob.
//
// Earlier revisions shipped raw KitSnapshot blobs with a .memkit extension.
// Those are not VST3 presets (the shared PresetBrowserView scans only
// .vstpreset files) and have been converted via tools/convert-memkit.js.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "dsp/bodies/body_damping_encoding.h"
#include "dsp/default_kit.h"
#include "dsp/macro_offsets.h"
#include "dsp/pad_config.h"
#include "plugin_ids.h"
#include "preset/membrum_preset_container.h"
#include "state/state_codec.h"

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/vstpresetfile.h"
#include "pluginterfaces/base/ibstream.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using Membrum::State::KitSnapshot;
using Membrum::State::readKitBlob;
using Membrum::State::writeKitBlob;

namespace {

std::vector<std::uint8_t> readFile(const std::filesystem::path& p)
{
    std::ifstream in(p, std::ios::binary);
    if (!in)
        return {};
    in.seekg(0, std::ios::end);
    const auto size = static_cast<std::streamsize>(in.tellg());
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(size));
    if (size > 0)
        in.read(reinterpret_cast<char*>(buf.data()), size);
    return buf;
}

std::filesystem::path factoryPresetRoot()
{
    // The test binary runs from build/.../bin/Release, so walk up to find
    // plugins/membrum/resources/presets/Kit Presets.
    auto cur = std::filesystem::current_path();
    for (int i = 0; i < 8; ++i)
    {
        const auto candidate =
            cur / "plugins" / "membrum" / "resources" / "presets" / "Kit Presets";
        if (std::filesystem::exists(candidate))
            return candidate;
        if (!cur.has_parent_path() || cur.parent_path() == cur)
            break;
        cur = cur.parent_path();
    }
    return {};
}

} // namespace

TEST_CASE("Factory kit presets: every .vstpreset under resources/ parses as a "
          "VST3 preset and round-trips through the state codec",
          "[membrum][preset][factory][kit_preset]")
{
    const auto root = factoryPresetRoot();
    if (root.empty())
    {
        WARN("Factory preset root not found -- skipping (CI build running "
             "from an unexpected working directory).");
        return;
    }

    int count = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".vstpreset")
            continue;

        INFO("factory preset: " << entry.path().string());
        const auto bytes = readFile(entry.path());
        REQUIRE(!bytes.empty());

        // Parse as a VST3 PresetFile.
        auto presetStream = owned(new MemoryStream(
            const_cast<std::uint8_t*>(bytes.data()),
            static_cast<TSize>(bytes.size())));
        presetStream->seek(0, IBStream::kIBSeekSet, nullptr);

        PresetFile pf(presetStream);
        REQUIRE(pf.readChunkList());
        CHECK(pf.getClassID() == Membrum::kProcessorUID);

        const auto* compEntry = pf.getEntry(kComponentState);
        REQUIRE(compEntry != nullptr);

        // Pull out the component-state chunk bytes.
        std::vector<std::uint8_t> componentBytes(
            static_cast<std::size_t>(compEntry->size));
        presetStream->seek(compEntry->offset, IBStream::kIBSeekSet, nullptr);
        int32 got = 0;
        REQUIRE(presetStream->read(componentBytes.data(),
                                   static_cast<int32>(componentBytes.size()),
                                   &got) == kResultOk);
        CHECK(got == static_cast<int32>(componentBytes.size()));

        // Read -> KitSnapshot must succeed.
        MemoryStream componentStream;
        int32 written = 0;
        componentStream.write(componentBytes.data(),
                              static_cast<int32>(componentBytes.size()),
                              &written);
        componentStream.seek(0, IBStream::kIBSeekSet, nullptr);

        KitSnapshot kit;
        REQUIRE(readKitBlob(&componentStream, kit) == kResultOk);

        // Re-encode and require the bytes match the original component-state
        // chunk exactly. With a single supported on-wire version, every
        // factory preset must be byte-exact to its writer-emitted form.
        MemoryStream rewrite;
        REQUIRE(writeKitBlob(&rewrite, kit) == kResultOk);
        int64 rewriteSize = 0;
        rewrite.seek(0, IBStream::kIBSeekEnd, &rewriteSize);

        CHECK(static_cast<std::size_t>(rewriteSize) == componentBytes.size());
        rewrite.seek(0, IBStream::kIBSeekSet, nullptr);
        std::vector<std::uint8_t> rewriteBytes(
            static_cast<std::size_t>(rewriteSize));
        rewrite.read(rewriteBytes.data(),
                     static_cast<int32>(rewriteSize), &got);
        CHECK(rewriteBytes == componentBytes);
        ++count;
    }

    // All three factory kits must be present and verified.
    CHECK(count >= 3);
}

// ------------------------------------------------------------------------------
// Fix plan T1.22 (MB-084): the factory kits store base values and keep their
// macro positions, and every kit plays what it played before macros became
// offsets. golden/factory_kit_fields_v5.bin holds, per kit (the order below)
// and per pad, the 59 sound slots, couplingAmount and the five macros (float32)
// as the version-5 kits loaded before T1.22.
// ------------------------------------------------------------------------------
namespace {

// `nonNeutralMacros`: the kit's non-neutral macro values before T1.22 (281 in
// all). `keptMacros`: those the kit keeps. Under the offset model the
// generator sets back to 0.5 only a macro whose position no base in [0, 1]
// reproduces at the pad's written fields (user ruling 2026-11-24, option A:
// 148 values, Brightness 61, Complexity 85, Punch 2); those positions never
// reached the sound (MB-084).
struct FactoryKitRef
{
    const char* category;
    const char* name;
    int nonNeutralMacros;
    int keptMacros;
};

constexpr std::array<FactoryKitRef, 20> kFactoryKits{{
    {"Acoustic", "Acoustic Studio Kit", 0, 0}, {"Acoustic", "Jazz Brushes", 46, 32},
    {"Acoustic", "Orchestral", 23, 8}, {"Acoustic", "Rock Big Room", 27, 18},
    {"Acoustic", "Vintage Wood", 25, 11}, {"Electronic", "808 Electronic Kit", 3, 0},
    {"Electronic", "909 Drum Machine", 11, 7}, {"Electronic", "LinnDrum CR-78", 10, 6},
    {"Electronic", "Modular West Coast", 10, 2}, {"Electronic", "Trap Modern", 15, 11},
    {"Percussive", "Cajon and Frames", 5, 2}, {"Percussive", "Hand Drums", 6, 6},
    {"Percussive", "Latin Perc", 13, 5}, {"Percussive", "Tabla", 9, 2},
    {"Percussive", "World Metal", 5, 0}, {"Unnatural", "Chaos Engine", 15, 4},
    {"Unnatural", "Drone and Sustain", 14, 4}, {"Unnatural", "Experimental FX Kit", 25, 15},
    {"Unnatural", "Ghost Bones", 16, 0}, {"Unnatural", "Glass Bell Garden", 3, 0},
}};

constexpr std::size_t kFixtureFloatsPerPad = 59 + 1 + 5;

KitSnapshot loadFactoryKit(const std::filesystem::path& root, const FactoryKitRef& ref)
{
    const auto bytes = readFile(root / ref.category / (std::string(ref.name) + ".vstpreset"));
    REQUIRE(!bytes.empty());
    auto presetStream = owned(new MemoryStream(const_cast<std::uint8_t*>(bytes.data()),
                                               static_cast<TSize>(bytes.size())));
    presetStream->seek(0, IBStream::kIBSeekSet, nullptr);
    PresetFile pf(presetStream);
    REQUIRE(pf.readChunkList());
    const auto* compEntry = pf.getEntry(kComponentState);
    REQUIRE(compEntry != nullptr);
    std::vector<std::uint8_t> comp(static_cast<std::size_t>(compEntry->size));
    presetStream->seek(compEntry->offset, IBStream::kIBSeekSet, nullptr);
    int32 got = 0;
    REQUIRE(presetStream->read(comp.data(), static_cast<int32>(comp.size()), &got) == kResultOk);
    MemoryStream componentStream;
    componentStream.write(comp.data(), static_cast<int32>(comp.size()), nullptr);
    componentStream.seek(0, IBStream::kIBSeekSet, nullptr);
    KitSnapshot kit;
    REQUIRE(readKitBlob(&componentStream, kit) == kResultOk);
    return kit;
}

} // namespace

TEST_CASE("Factory kit presets: stored macros reach the sound and every kit plays as before",
          "[membrum][preset][factory][kit_preset]")
{
    const auto root = factoryPresetRoot();
    REQUIRE(!root.empty());

    const auto fixtureBytes = readFile(std::filesystem::path(MEMBRUM_RESOURCES_DIR) / ".." /
                                       "tests" / "golden" / "factory_kit_fields_v5.bin");
    REQUIRE(fixtureBytes.size() ==
            kFactoryKits.size() * Membrum::kNumPads * kFixtureFloatsPerPad * sizeof(float));
    std::vector<float> fixture(fixtureBytes.size() / sizeof(float));
    std::memcpy(fixture.data(), fixtureBytes.data(), fixtureBytes.size());

    int totalNonNeutral = 0;
    int totalKept = 0;
    for (std::size_t k = 0; k < kFactoryKits.size(); ++k)
    {
        const auto& ref = kFactoryKits[k];
        CAPTURE(ref.name);
        const KitSnapshot kit = loadFactoryKit(root, ref);

        int nonNeutral = 0;
        int kept = 0;
        int mismatches = 0;
        double worst = 0.0;
        for (int p = 0; p < Membrum::kNumPads; ++p)
        {
            const auto* want =
                fixture.data() + (k * Membrum::kNumPads + static_cast<std::size_t>(p)) *
                                     kFixtureFloatsPerPad;
            Membrum::PadConfig stored;
            Membrum::State::applyPadSnapshot(kit.pads[static_cast<std::size_t>(p)], stored);

            // Every stored macro is the fixture's macro, or set back to
            // neutral (option A).
            const std::array<float, 5> macros{stored.macroTightness, stored.macroBrightness,
                                              stored.macroBodySize, stored.macroPunch,
                                              stored.macroComplexity};
            for (std::size_t m = 0; m < macros.size(); ++m)
            {
                if (macros[m] != want[60 + m] && macros[m] != 0.5f)
                {
                    ++mismatches;
                    UNSCOPED_INFO("pad " << p << " macro " << m << " stored " << macros[m]
                                  << ", was " << want[60 + m]);
                }
                if (want[60 + m] != 0.5f)
                    ++nonNeutral;
                if (macros[m] != 0.5f && macros[m] == want[60 + m])
                    ++kept;
            }

            // The macros applied to the stored base play the fixture's fields.
            const auto effective = Membrum::applyMacroOffsets(stored);
            const auto played = Membrum::State::toPadSnapshot(effective);

            // Fix plan T1.23: body damping (sound[42] / sound[43]) is stored
            // relative to the pad's derived law; the fixture holds the
            // version-5 ABSOLUTE encoding (b1 = 0.2 + n * 49.8 s^-1, b3 = n *
            // 1e-3 s). The law the pad plays must be that absolute law.
            {
                const auto law = Membrum::Bodies::playedBodyDamping(effective);
                const double wantB1 = 0.2 + static_cast<double>(want[42]) * 49.8;
                const double wantB3 = static_cast<double>(want[43]) * 1.0e-3;
                const double dB1 = std::abs(static_cast<double>(law.b1) - wantB1) / wantB1;
                const double dB3 = std::abs(static_cast<double>(law.b3) - wantB3);
                if (dB1 > 1.0e-5 || dB3 > 1.0e-9)
                {
                    ++mismatches;
                    UNSCOPED_INFO("pad " << p << " damping plays b1 " << law.b1 << " b3 "
                                  << law.b3 << ", was " << wantB1 << " / " << wantB3);
                }
            }

            for (std::size_t i = 0; i < played.sound.size(); ++i)
            {
                if (i == 42 || i == 43)
                    continue;
                const double d = std::abs(played.sound[i] - static_cast<double>(want[i]));
                worst = std::max(worst, d);
                if (d > 1e-6)
                {
                    ++mismatches;
                    UNSCOPED_INFO("pad " << p << " sound[" << i << "] plays " << played.sound[i]
                                  << ", was " << want[i]);
                }
            }
            const double dc = std::abs(played.couplingAmount - static_cast<double>(want[59]));
            worst = std::max(worst, dc);
            if (dc > 1e-6)
                ++mismatches;
        }
        INFO("worst field difference " << worst);
        CHECK(mismatches == 0);
        CHECK(nonNeutral == ref.nonNeutralMacros);
        CHECK(kept == ref.keptMacros);
        totalNonNeutral += nonNeutral;
        totalKept += kept;
    }
    CHECK(totalNonNeutral == 281);
    CHECK(totalKept == 133);
}

// ------------------------------------------------------------------------------
// Phase 8C: one-shot regenerator. Rebuilds the Acoustic / Electronic /
// Experimental kit presets straight from `DefaultKit::apply` so pad-level
// edits (e.g. Phase 8C noise/click rebalance, airLoading defaults) take
// effect without hand-editing the binary blobs. Hidden behind tag.
// ------------------------------------------------------------------------------
TEST_CASE("Factory kit presets: regenerate from DefaultKit::apply (hidden)",
          "[.regen_factory_presets]")
{
    const auto root = factoryPresetRoot();
    REQUIRE(!root.empty());

    std::array<Membrum::PadConfig, Membrum::kNumPads> pads;
    Membrum::DefaultKit::apply(pads);

    Membrum::State::KitSnapshot kit;
    for (int i = 0; i < Membrum::kNumPads; ++i)
        kit.pads[static_cast<std::size_t>(i)] =
            Membrum::State::toPadSnapshot(pads[static_cast<std::size_t>(i)]);

    // Write the freshly-generated kit to every known factory .vstpreset.
    int regenerated = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".vstpreset")
            continue;
        MemoryStream dst;
        REQUIRE(Membrum::State::writeKitBlob(&dst, kit) == kResultOk);
        const std::string name   = entry.path().stem().string();
        const std::string subcat = entry.path().parent_path().filename().string();
        REQUIRE(Membrum::Preset::writePresetFile(entry.path(), &dst, name, subcat));
        ++regenerated;
    }
    WARN("regenerated " << regenerated << " factory preset file(s) from DefaultKit");
    CHECK(regenerated >= 3);
}
