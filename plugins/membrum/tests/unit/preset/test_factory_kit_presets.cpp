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

#include "dsp/default_kit.h"
#include "dsp/pad_config.h"
#include "plugin_ids.h"
#include "preset/membrum_preset_container.h"
#include "state/state_codec.h"

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/vstpresetfile.h"
#include "pluginterfaces/base/ibstream.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
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

        // Inspect the on-disk version (first int32 of the blob).
        int32 onDiskVersion = 0;
        std::memcpy(&onDiskVersion, componentBytes.data(), sizeof(onDiskVersion));

        // Write back and compare against the original component-state chunk.
        // This guards against codec drift, but only for blobs that were
        // already at the current version. Factory presets authored against an
        // earlier version (e.g. v6 before Phase 7) are allowed to re-encode
        // to a larger v7 blob -- we still require the decode succeeded above.
        MemoryStream rewrite;
        REQUIRE(writeKitBlob(&rewrite, kit) == kResultOk);
        int64 rewriteSize = 0;
        rewrite.seek(0, IBStream::kIBSeekEnd, &rewriteSize);

        if (onDiskVersion == Membrum::State::kBlobVersion)
        {
            CHECK(static_cast<std::size_t>(rewriteSize) == componentBytes.size());
            rewrite.seek(0, IBStream::kIBSeekSet, nullptr);
            std::vector<std::uint8_t> rewriteBytes(
                static_cast<std::size_t>(rewriteSize));
            rewrite.read(rewriteBytes.data(),
                         static_cast<int32>(rewriteSize), &got);
            CHECK(rewriteBytes == componentBytes);
        }
        else
        {
            INFO("legacy factory preset (v" << onDiskVersion
                 << ") -- byte-exact drift check skipped, decode verified");
            // Decode already REQUIRE'd above; additional sanity: re-read the
            // v7 round-trip and verify it comes back the same logical state.
            rewrite.seek(0, IBStream::kIBSeekSet, nullptr);
            KitSnapshot kit2;
            REQUIRE(readKitBlob(&rewrite, kit2) == kResultOk);
            CHECK(kit2.maxPolyphony == kit.maxPolyphony);
        }
        ++count;
    }

    // All three factory kits must be present and verified.
    CHECK(count >= 3);
}

// ------------------------------------------------------------------------------
// Hidden one-shot utility: upgrade every factory .vstpreset to the current
// blob version. Run explicitly with Catch2 tag filter
// `"[.upgrade_factory_presets]"` after bumping the blob version. The rewriter
// decodes the existing blob (legacy versions are still accepted) and re-emits
// via the current writer, then overwrites the .vstpreset container.
// ------------------------------------------------------------------------------
TEST_CASE("Factory kit presets: upgrade to current blob version (hidden)",
          "[.upgrade_factory_presets]")
{
    const auto root = factoryPresetRoot();
    REQUIRE(!root.empty());

    int upgraded = 0;
    int skipped  = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".vstpreset")
            continue;

        const auto bytes = readFile(entry.path());
        REQUIRE(!bytes.empty());

        auto presetStream = owned(new MemoryStream(
            const_cast<std::uint8_t*>(bytes.data()),
            static_cast<TSize>(bytes.size())));
        presetStream->seek(0, IBStream::kIBSeekSet, nullptr);

        PresetFile pf(presetStream);
        REQUIRE(pf.readChunkList());
        REQUIRE(pf.getClassID() == Membrum::kProcessorUID);

        const auto* compEntry = pf.getEntry(kComponentState);
        REQUIRE(compEntry != nullptr);

        std::vector<std::uint8_t> compBytes(
            static_cast<std::size_t>(compEntry->size));
        presetStream->seek(compEntry->offset, IBStream::kIBSeekSet, nullptr);
        int32 got = 0;
        presetStream->read(compBytes.data(),
                           static_cast<int32>(compBytes.size()), &got);

        int32 onDiskVersion = 0;
        std::memcpy(&onDiskVersion, compBytes.data(), sizeof(onDiskVersion));
        if (onDiskVersion == Membrum::State::kBlobVersion)
        {
            ++skipped;
            continue;  // already current
        }

        MemoryStream src;
        int32 written = 0;
        src.write(compBytes.data(), static_cast<int32>(compBytes.size()), &written);
        src.seek(0, IBStream::kIBSeekSet, nullptr);

        KitSnapshot kit;
        REQUIRE(readKitBlob(&src, kit) == kResultOk);

        MemoryStream dst;
        REQUIRE(writeKitBlob(&dst, kit) == kResultOk);

        // Re-derive a reasonable preset-name + subcategory. Use the filename
        // stem for the name and the parent directory as the subcategory.
        const std::string name =
            entry.path().stem().string();
        const std::string subcat =
            entry.path().parent_path().filename().string();

        REQUIRE(Membrum::Preset::writePresetFile(entry.path(), &dst, name, subcat));
        ++upgraded;
    }

    WARN("factory preset upgrade: " << upgraded << " upgraded, "
         << skipped << " already current");
    CHECK(upgraded + skipped >= 3);
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
