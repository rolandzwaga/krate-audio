// =============================================================================
// Factory Preset Mix Floor Test
// =============================================================================
// Loads every shipped .vstpreset the way a host does (the "Comp" chunk through the controller's sync functions, in
// Processor::getState order) and checks the Mix of the preset's own mode: a factory preset at 0 % Mix plays only
// the dry signal, so the preset does nothing. Prints every preset's mode and Mix.
//
// ITERUM_PRESET_DIR points the case at another preset tree (e.g. the installed one) instead of the resources folder.
// =============================================================================

#include "parameters/granular_params.h"
#include "parameters/spectral_params.h"
#include "parameters/freeze_params.h"
#include "parameters/reverse_params.h"
#include "parameters/shimmer_params.h"
#include "parameters/tape_params.h"
#include "parameters/bbd_params.h"
#include "parameters/digital_params.h"
#include "parameters/pingpong_params.h"
#include "parameters/multitap_params.h"
#include "plugin_ids.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace Steinberg;

namespace {

/// Stores what the sync functions set, as the controller's parameters would hold it.
class ParamStore : public Vst::EditControllerEx1 {
public:
    std::map<Vst::ParamID, double> values;

    tresult PLUGIN_API setParamNormalized(Vst::ParamID id, Vst::ParamValue value) override {
        values[id] = value;
        return kResultOk;
    }

    Vst::ParamValue PLUGIN_API getParamNormalized(Vst::ParamID id) override {
        const auto it = values.find(id);
        return it != values.end() ? it->second : 0.0;
    }
};

/// The Mix parameter of each DelayMode, by mode index.
constexpr std::array<Vst::ParamID, 10> kModeMixIds = {
    Iterum::kGranularMixId, Iterum::kSpectralMixId, Iterum::kShimmerMixId, Iterum::kTapeMixId,
    Iterum::kBBDMixId,      Iterum::kDigitalMixId,  Iterum::kPingPongMixId, Iterum::kReverseMixId,
    Iterum::kMultiTapMixId, Iterum::kFreezeMixId,
};

/// The lowest Mix a factory preset may carry on its own mode.
constexpr double kMixFloor = 0.1;

/// The component-state chunk ("Comp") of a .vstpreset file; empty when the file has none.
[[nodiscard]] std::vector<char> componentChunk(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    const std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.size() < 48 || std::memcmp(bytes.data(), "VST3", 4) != 0) {
        return {};
    }
    int64_t listOffset = 0;
    std::memcpy(&listOffset, bytes.data() + 40, sizeof(listOffset));
    if (listOffset < 0 || static_cast<size_t>(listOffset) + 8 > bytes.size()
        || std::memcmp(bytes.data() + listOffset, "List", 4) != 0) {
        return {};
    }
    int32_t count = 0;
    std::memcpy(&count, bytes.data() + listOffset + 4, sizeof(count));
    for (int32_t i = 0; i < count; ++i) {
        const char* entry = bytes.data() + listOffset + 8 + static_cast<ptrdiff_t>(i) * 20;
        if (std::memcmp(entry, "Comp", 4) == 0) {
            int64_t offset = 0;
            int64_t size = 0;
            std::memcpy(&offset, entry + 4, sizeof(offset));
            std::memcpy(&size, entry + 12, sizeof(size));
            return {bytes.begin() + offset, bytes.begin() + offset + size};
        }
    }
    return {};
}

} // anonymous namespace

TEST_CASE("Every factory preset carries an audible Mix on its own mode", "[preset][mix]") {
    const char* overrideDir = std::getenv("ITERUM_PRESET_DIR");
    const std::filesystem::path root =
        overrideDir != nullptr ? std::filesystem::path(overrideDir)
                               : std::filesystem::path(ITERUM_RESOURCES_DIR) / "presets";
    REQUIRE(std::filesystem::is_directory(root));

    int presets = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.path().extension() != ".vstpreset") {
            continue;
        }
        const std::string name = std::filesystem::relative(entry.path(), root).generic_string();
        CAPTURE(name);
        std::vector<char> chunk = componentChunk(entry.path());
        REQUIRE(!chunk.empty());

        auto* stream = new MemoryStream(chunk.data(), static_cast<TSize>(chunk.size()));
        IBStreamer streamer(stream, kLittleEndian);
        ParamStore store;
        float gain = 0.0f;
        int32 mode = -1;
        REQUIRE(streamer.readFloat(gain));
        REQUIRE(streamer.readInt32(mode));
        Iterum::syncGranularParamsToController(streamer, store);
        Iterum::syncSpectralParamsToController(streamer, store);
        Iterum::syncFreezeParamsToController(streamer, store);
        Iterum::syncReverseParamsToController(streamer, store);
        Iterum::syncShimmerParamsToController(streamer, store);
        Iterum::syncTapeParamsToController(streamer, store);
        Iterum::syncBBDParamsToController(streamer, store);
        Iterum::syncDigitalParamsToController(streamer, store);
        Iterum::syncPingPongParamsToController(streamer, store);
        Iterum::syncMultiTapParamsToController(streamer, store);
        Iterum::syncTapeSyncParamsToController(streamer, store);
        Iterum::syncMultiTapLengthParamsToController(streamer, store);
        Iterum::syncFreezeCaptureParamsToController(streamer, store);
        stream->release();

        ++presets;
        if (mode < 0 || std::cmp_greater_equal(mode, kModeMixIds.size())) {
            std::printf("%-40s mode %d  (no such mode)\n", name.c_str(), mode);
            FAIL_CHECK("no such mode: " << mode);
            continue;
        }
        const double mix = store.getParamNormalized(kModeMixIds[static_cast<size_t>(mode)]);
        std::printf("%-40s mode %d  mix %5.1f %%\n", name.c_str(), mode, mix * 100.0);
        CHECK(mix >= kMixFloor);
    }
    REQUIRE(presets > 0);
}
