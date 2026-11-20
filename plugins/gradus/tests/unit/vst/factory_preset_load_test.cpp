// ==============================================================================
// Factory presets and v1 state streams load (regression, Gradus 1.10.5)
// ==============================================================================
// The factory .vstpreset files carry state version 1 (tools/gradus_preset_format.h), the version Gradus 1.0-1.5
// wrote; its body is the v2 arp block. Until 1.8.0 every loader ignored the version word. Spec 142 (1.8.0) made the
// processor accept only 2 and 3, and the audit remediation (1.10.x) gave the controller the same gate, so no factory
// preset loaded and neither did a project saved by Gradus 1.0-1.5. No test loaded a shipped preset, so nothing
// noticed. These cases drive every shipped preset through each load path a host or the preset browser uses.

#include "controller/controller.h"
#include "parameters/arpeggiator_params.h"
#include "plugin_ids.h"
#include "processor/processor.h"

#include <public.sdk/source/common/memorystream.h>
#include <public.sdk/source/vst/vstpresetfile.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

[[nodiscard]] std::vector<std::filesystem::path> factoryPresets() {
    std::vector<std::filesystem::path> files;
    const std::filesystem::path root = std::filesystem::path(GRADUS_RESOURCES_DIR) / "presets";
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_regular_file() && entry.path().extension() == ".vstpreset") {
            files.push_back(entry.path());
        }
    }
    return files;
}

[[nodiscard]] IPtr<IBStream> openPreset(const std::filesystem::path& file) {
    return owned(FileStream::open(file.string().c_str(), "rb"));
}

/// A v1 stream as Gradus 1.0-1.5 and the preset generator write it: version 1, then the arp block.
[[nodiscard]] IPtr<MemoryStream> v1Stream(const Gradus::ArpeggiatorParams& params) {
    auto stream = owned(new MemoryStream());
    IBStreamer writer(stream, kLittleEndian);
    writer.writeInt32(1);
    Gradus::saveArpParams(params, writer);
    stream->seek(0, IBStream::kIBSeekSet, nullptr);
    return stream;
}

}  // namespace

TEST_CASE("Gradus factory presets load in the processor and the controller", "[gradus][vst][state][preset]") {
    const std::vector<std::filesystem::path> presets = factoryPresets();
    REQUIRE(presets.size() == 60u);

    for (const std::filesystem::path& file : presets) {
        INFO(file.lexically_relative(GRADUS_RESOURCES_DIR).string());

        Gradus::Processor processor;
        REQUIRE(processor.initialize(nullptr) == kResultOk);
        {
            IPtr<IBStream> stream = openPreset(file);
            REQUIRE(stream != nullptr);
            PresetFile preset(stream);
            REQUIRE(preset.readChunkList());
            CHECK(preset.restoreComponentState(&processor));
        }
        REQUIRE(processor.terminate() == kResultOk);

        Gradus::Controller controller;
        REQUIRE(controller.initialize(nullptr) == kResultOk);
        {
            IPtr<IBStream> stream = openPreset(file);
            REQUIRE(stream != nullptr);
            PresetFile preset(stream);
            REQUIRE(preset.readChunkList());
            CHECK(preset.restoreComponentState(&controller));

            // The preset browser's path: the component chunk handed to loadComponentStateWithNotify.
            const PresetFile::Entry* entry = preset.getEntry(kComponentState);
            REQUIRE(entry != nullptr);
            auto chunk = owned(new ReadOnlyBStream(stream, entry->offset, entry->size));
            CHECK(controller.loadComponentStateWithNotify(chunk));
        }
        REQUIRE(controller.terminate() == kResultOk);
    }
}

TEST_CASE("Gradus loads a v1 state stream into both components", "[gradus][vst][state][migration]") {
    Gradus::ArpeggiatorParams src;
    src.mode.store(3, std::memory_order_relaxed);
    src.velocityLaneLength.store(7, std::memory_order_relaxed);

    SECTION("processor applies the arp block") {
        Gradus::Processor processor;
        REQUIRE(processor.initialize(nullptr) == kResultOk);
        REQUIRE(processor.setState(v1Stream(src)) == kResultOk);

        auto saved = owned(new MemoryStream());
        REQUIRE(processor.getState(saved) == kResultOk);
        saved->seek(0, IBStream::kIBSeekSet, nullptr);
        IBStreamer reader(saved, kLittleEndian);
        int32 version = 0;
        REQUIRE(reader.readInt32(version));
        CHECK(version == Gradus::kCurrentStateVersion);
        Gradus::ArpeggiatorParams back;
        REQUIRE(Gradus::loadArpParams(back, reader));
        CHECK(back.mode.load() == 3);
        CHECK(back.velocityLaneLength.load() == 7);
        REQUIRE(processor.terminate() == kResultOk);
    }

    SECTION("controller applies the arp block") {
        Gradus::Controller controller;
        REQUIRE(controller.initialize(nullptr) == kResultOk);
        const ParamValue before = controller.getParamNormalized(Gradus::kArpVelocityLaneLengthId);
        REQUIRE(controller.setComponentState(v1Stream(src)) == kResultOk);
        CHECK(controller.getParamNormalized(Gradus::kArpVelocityLaneLengthId) != before);
        REQUIRE(controller.terminate() == kResultOk);
    }

    SECTION("version 0 is still refused") {
        auto stream = owned(new MemoryStream());
        {
            IBStreamer writer(stream, kLittleEndian);
            writer.writeInt32(0);
            Gradus::saveArpParams(src, writer);
        }
        stream->seek(0, IBStream::kIBSeekSet, nullptr);
        Gradus::Processor processor;
        REQUIRE(processor.initialize(nullptr) == kResultOk);
        CHECK(processor.setState(stream) == kResultFalse);
        REQUIRE(processor.terminate() == kResultOk);
    }
}
