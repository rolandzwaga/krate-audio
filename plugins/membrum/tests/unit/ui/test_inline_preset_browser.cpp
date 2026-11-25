// ==============================================================================
// Inline kit / pad preset browsers (T2.32, MB-103)
// ==============================================================================
// Prev / Next cycles from the current preset by path, not by name (a user and a
// factory preset can share a name), a project load clears the kit label, and
// the pad label follows the selected pad.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "controller/controller.h"
#include "processor/processor.h"
#include "plugin_ids.h"
#include "preset/preset_info.h"
#include "ui/preset_inline_browser_view.h"

#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/base/ibstream.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

Krate::Plugins::PresetInfo makeInfo(const char* name, const char* path)
{
    Krate::Plugins::PresetInfo info;
    info.name = name;
    info.path = path;
    return info;
}

// A controller's own preset blob as a seekable stream, as the PresetManager
// hands the load provider.
std::vector<char> blobBytes(IBStream* stream)
{
    std::vector<char> bytes;
    std::array<char, 4096> chunk{};
    int32 got = 0;
    while (stream->read(chunk.data(), static_cast<int32>(chunk.size()), &got) == kResultOk &&
           got > 0)
        bytes.insert(bytes.end(), chunk.data(), chunk.data() + got);
    return bytes;
}

std::vector<char> kitBlob(Membrum::Controller& controller)
{
    IBStream* s = controller.kitPresetStateProvider();
    REQUIRE(s != nullptr);
    auto bytes = blobBytes(s);
    s->release();
    return bytes;
}

std::vector<char> padBlob(Membrum::Controller& controller)
{
    IBStream* s = controller.padPresetStateProvider();
    REQUIRE(s != nullptr);
    auto bytes = blobBytes(s);
    s->release();
    return bytes;
}

bool loadKit(Membrum::Controller& c, const std::vector<char>& bytes,
             const Krate::Plugins::PresetInfo& info)
{
    MemoryStream stream(const_cast<char*>(bytes.data()), static_cast<TSize>(bytes.size()));
    return c.kitPresetLoadProvider(&stream, info);
}

bool loadPad(Membrum::Controller& c, const std::vector<char>& bytes,
             const Krate::Plugins::PresetInfo& info)
{
    MemoryStream stream(const_cast<char*>(bytes.data()), static_cast<TSize>(bytes.size()));
    return c.padPresetLoadProvider(&stream, info);
}

} // namespace

TEST_CASE("Inline preset browser: Next from the second of two same-named presets reaches the next preset",
          "[ui][preset_browser]")
{
    const std::vector<Krate::Plugins::PresetInfo> list{
        makeInfo("X", "user/X.vstpreset"),
        makeInfo("X", "factory/Acoustic/X.vstpreset"),
        makeInfo("Y", "factory/Acoustic/Y.vstpreset"),
    };
    using View = Membrum::UI::InlinePresetBrowserView;
    const std::filesystem::path second = "factory/Acoustic/X.vstpreset";

    CHECK(View::cycleIndex(list, second, "X", +1) == 2);
    CHECK(View::cycleIndex(list, second, "X", -1) == 0);
    // Nothing current: Next starts at the first, Prev at the last.
    CHECK(View::cycleIndex(list, {}, "", +1) == 0);
    CHECK(View::cycleIndex(list, {}, "", -1) == 2);
    // A name with no path still cycles by name (the first match), and wraps.
    CHECK(View::cycleIndex(list, {}, "Y", +1) == 0);
    // A path that is not in the list starts over.
    CHECK(View::cycleIndex(list, "gone/Z.vstpreset", "Z", +1) == 0);
}

TEST_CASE("Inline preset browser: a project load clears the kit name and a pad switch shows that pad's name",
          "[controller][preset_browser]")
{
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    const auto kit = kitBlob(controller);
    REQUIRE_FALSE(kit.empty());

    // A kit-browser load names the kit.
    REQUIRE(loadKit(controller, kit, makeInfo("Kit A", "kits/Kit A.vstpreset")));
    CHECK(controller.lastKitPresetNameForTest() == "Kit A");

    // A project load (host setComponentState from the processor's state) clears it.
    {
        Membrum::Processor proc;
        REQUIRE(proc.initialize(nullptr) == kResultOk);
        MemoryStream state;
        REQUIRE(proc.getState(&state) == kResultOk);
        state.seek(0, IBStream::kIBSeekSet, nullptr);
        REQUIRE(controller.setComponentState(&state) == kResultOk);
        proc.terminate();
    }
    CHECK(controller.lastKitPresetNameForTest().empty());

    // A pad-preset load on pad 2 names pad 2 only.
    controller.setParamNormalized(Membrum::kSelectedPadId, 2.0 / 31.0);
    REQUIRE(controller.selectedPadIndexForTest() == 2);
    REQUIRE(loadPad(controller, padBlob(controller), makeInfo("Snare One", "pads/Snare One.vstpreset")));
    CHECK(controller.padPresetNameForTest(2) == "Snare One");
    CHECK(controller.padPresetNameForTest(5).empty());

    // Selecting pad 5 leaves pad 2's entry in place; a load there names pad 5.
    controller.setParamNormalized(Membrum::kSelectedPadId, 5.0 / 31.0);
    REQUIRE(loadPad(controller, padBlob(controller), makeInfo("Tom Two", "pads/Tom Two.vstpreset")));
    CHECK(controller.padPresetNameForTest(5) == "Tom Two");
    CHECK(controller.padPresetNameForTest(2) == "Snare One");

    // A kit load replaces every pad's sound: no pad entry survives it.
    REQUIRE(loadKit(controller, kit, makeInfo("Kit B", "kits/Kit B.vstpreset")));
    CHECK(controller.lastKitPresetNameForTest() == "Kit B");
    CHECK(controller.padPresetNameForTest(2).empty());
    CHECK(controller.padPresetNameForTest(5).empty());

    REQUIRE(controller.terminate() == kResultOk);
}
