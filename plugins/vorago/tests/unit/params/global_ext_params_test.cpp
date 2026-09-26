// ==============================================================================
// Vorago Phase 12 - global_ext parameter pack contract (T030)
// ==============================================================================
// Group 7 shared test shape (specs/vorago-phase12-parameters/tasks.md) for the
// four Phase 12 Global IDs, plus the v1 block guard:
//
//   2 Seed               L(16) "Seed 1".."Seed 16"   default 0  n0 0.0  stepCount 15
//   3 Output Saturation  %  [0, 1]  0.12  n0 0.12  lin
//   4 Sustain Pedal      R  kCanAutomate | kIsHidden  [0, 1]  0  n0 0.0  (not persisted)
//   5 Channel Pressure   %  R  kCanAutomate | kIsHidden  [0, 1]  0  n0 0.0  (not persisted)
//
// v2 extension stream B = 8 bytes: int32 seedIndex + float outputSaturation.
// The v1 saveGlobalParams block stays 8 bytes: float masterGain + int32 polyphony.
//
// Built with -fno-fast-math (tests/CMakeLists.txt): NaN/Inf payloads are built
// from bit patterns through a volatile uint32 + memcpy.
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "parameters/global_params.h"
#include "parameters/param_mapping.h"
#include "plugin_ids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {

using Catch::Approx;
using Steinberg::Vst::ParamID;

constexpr int kNTotal = 6;   // registerGlobalParams: 0, 1 (v1) + 2..5 (Phase 12)
constexpr std::size_t kB = 8;  // saveGlobalParamsV2Ext

std::string toAsciiString(const Steinberg::Vst::String128 s) {
    char buf[128] = {};
    Steinberg::UString(const_cast<Steinberg::Vst::TChar*>(s), 128)  // NOLINT(cppcoreguidelines-pro-type-const-cast)
        .toAscii(buf, 128);
    return {buf};
}

Steinberg::IPtr<Steinberg::MemoryStream> makeStream() {
    return Steinberg::owned(new Steinberg::MemoryStream());
}

void rewindStream(Steinberg::MemoryStream& s) {
    REQUIRE(s.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr) == Steinberg::kResultOk);
}

std::vector<unsigned char> bytesOf(Steinberg::MemoryStream& s) {
    const auto* p = reinterpret_cast<const unsigned char*>(s.getData());  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
    return {p, p + s.getSize()};
}

std::uint32_t bitsOf(float f) { return std::bit_cast<std::uint32_t>(f); }

float floatFromBits(std::uint32_t pattern) {
    volatile std::uint32_t bits = pattern;
    const std::uint32_t copy = bits;
    float f = 0.0f;
    std::memcpy(&f, &copy, sizeof(f));
    return f;
}

// Every GlobalParams field (v1 + Phase 12), floats as bit patterns.
struct Snapshot {
    std::uint32_t masterGain = 0;
    int polyphony = 0;
    int seedIndex = 0;
    std::uint32_t outputSaturation = 0;
    std::uint32_t sustainPedal = 0;
    std::uint32_t channelPressure = 0;
    bool operator==(const Snapshot&) const = default;
};

Snapshot snapshot(const ::Vorago::GlobalParams& p) {
    return {.masterGain=bitsOf(p.masterGain.load()),       .polyphony=p.polyphony.load(),
            .seedIndex=p.seedIndex.load(),                .outputSaturation=bitsOf(p.outputSaturation.load()),
            .sustainPedal=bitsOf(p.sustainPedal.load()),     .channelPressure=bitsOf(p.channelPressure.load())};
}

void setExt(::Vorago::GlobalParams& p, int seed, float sat) {
    p.seedIndex.store(seed);
    p.outputSaturation.store(sat);
}

// Save values written in the stream (SECTION 4 non-defaults).
constexpr int kSavedSeed = 11;
constexpr float kSavedSat = 0.63f;

// Pre-dirtied values, distinct from the saved values.
constexpr int kDirtySeed = 3;
constexpr float kDirtySat = 0.27f;

Steinberg::IPtr<Steinberg::MemoryStream> savedStream() {
    ::Vorago::GlobalParams p;
    setExt(p, kSavedSeed, kSavedSat);
    auto stream = makeStream();
    {
        Steinberg::IBStreamer w(stream, kLittleEndian);
        ::Vorago::saveGlobalParamsV2Ext(p, w);
    }
    rewindStream(*stream);
    return stream;
}

// A v2-extension stream holding one int32 + one float, little-endian.
Steinberg::IPtr<Steinberg::MemoryStream> streamOf(Steinberg::int32 seed, float sat) {
    auto stream = makeStream();
    {
        Steinberg::IBStreamer w(stream, kLittleEndian);
        w.writeInt32(seed);
        w.writeFloat(sat);
    }
    rewindStream(*stream);
    return stream;
}

struct Expected {
    ParamID id;
    const char* title;
    const char* units;
    Steinberg::int32 stepCount;
    Steinberg::int32 flags;
    double n0;
};

constexpr Steinberg::int32 kAuto = Steinberg::Vst::ParameterInfo::kCanAutomate;
constexpr Steinberg::int32 kList = Steinberg::Vst::ParameterInfo::kIsList;
constexpr Steinberg::int32 kHidden = Steinberg::Vst::ParameterInfo::kIsHidden;

constexpr std::array<Expected, 4> kExpected = {{
    {.id=::Vorago::kSeedId, .title="Seed", .units="", .stepCount=15, .flags=kAuto | kList, .n0=0.0},
    {.id=::Vorago::kOutputSaturationId, .title="Output Saturation", .units="%", .stepCount=0, .flags=kAuto, .n0=0.12},
    {.id=::Vorago::kSustainPedalId, .title="Sustain Pedal", .units="", .stepCount=0, .flags=kAuto | kHidden, .n0=0.0},
    {.id=::Vorago::kChannelPressureId, .title="Channel Pressure", .units="%", .stepCount=0, .flags=kAuto | kHidden, .n0=0.0},
}};

}  // namespace

TEST_CASE("Vorago_GlobalExtParamsContract", "[vorago][params]") {
    using namespace ::Vorago;

    SECTION("Registration") {
        Steinberg::Vst::ParameterContainer pc;
        registerGlobalParams(pc);
        REQUIRE(pc.getParameterCount() == kNTotal);

        for (const auto& e : kExpected) {
            INFO("id " << e.id);
            auto* p = pc.getParameter(e.id);
            REQUIRE(p != nullptr);
            const auto& info = p->getInfo();
            REQUIRE(toAsciiString(info.title) == e.title);
            REQUIRE(toAsciiString(info.units) == e.units);
            REQUIRE(info.stepCount == e.stepCount);
            REQUIRE(info.flags == e.flags);
            REQUIRE(info.defaultNormalizedValue == Approx(e.n0).margin(1e-9));
        }

        // Seed labels "Seed 1" .. "Seed 16" in index order.
        auto* seed = pc.getParameter(kSeedId);
        REQUIRE(seed != nullptr);
        for (int i = 0; i < 16; ++i) {
            INFO("seed index " << i);
            Steinberg::Vst::String128 s{};
            seed->toString(indexToNormalized(i, 16), s);
            REQUIRE(toAsciiString(s) == "Seed " + std::to_string(i + 1));
        }

        // The v1 IDs are still registered with their Phase 11 defaults.
        REQUIRE(pc.getParameter(kMasterGainId) != nullptr);
        REQUIRE(pc.getParameter(kMasterGainId)->getInfo().defaultNormalizedValue ==
                Approx(0.5).margin(1e-9));
        REQUIRE(pc.getParameter(kPolyphonyId) != nullptr);
        REQUIRE(pc.getParameter(kPolyphonyId)->getInfo().defaultNormalizedValue ==
                Approx(0.6).margin(1e-9));
    }

    SECTION("DefaultsDenormalizeToPlain") {
        const GlobalParams fresh;
        REQUIRE(fresh.seedIndex.load() == 0);
        REQUIRE(fresh.outputSaturation.load() == Approx(0.12f).epsilon(1e-6));
        REQUIRE(fresh.sustainPedal.load() == 0.0f);
        REQUIRE(fresh.channelPressure.load() == 0.0f);

        GlobalParams p;
        p.seedIndex.store(9);
        p.outputSaturation.store(0.9f);
        p.sustainPedal.store(1.0f);
        p.channelPressure.store(0.7f);
        for (const auto& e : kExpected) {
            handleGlobalParamChange(p, e.id, e.n0);
        }
        REQUIRE(p.seedIndex.load() == fresh.seedIndex.load());
        REQUIRE(p.outputSaturation.load() == Approx(fresh.outputSaturation.load()).epsilon(1e-6));
        REQUIRE(p.sustainPedal.load() == fresh.sustainPedal.load());
        REQUIRE(p.channelPressure.load() == fresh.channelPressure.load());

        // Range ends.
        for (const auto& e : kExpected) {
            handleGlobalParamChange(p, e.id, 0.0);
        }
        REQUIRE(p.seedIndex.load() == 0);
        REQUIRE(p.outputSaturation.load() == 0.0f);
        REQUIRE(p.sustainPedal.load() == 0.0f);
        REQUIRE(p.channelPressure.load() == 0.0f);

        for (const auto& e : kExpected) {
            handleGlobalParamChange(p, e.id, 1.0);
        }
        REQUIRE(p.seedIndex.load() == 15);
        REQUIRE(p.outputSaturation.load() == 1.0f);
        REQUIRE(p.sustainPedal.load() == 1.0f);
        REQUIRE(p.channelPressure.load() == 1.0f);

        // Every seed index round-trips through its normalized value.
        for (int i = 0; i < 16; ++i) {
            handleGlobalParamChange(p, kSeedId, indexToNormalized(i, 16));
            REQUIRE(p.seedIndex.load() == i);
        }

        // The Phase 12 IDs never touch the v1 fields.
        GlobalParams q;
        for (const auto& e : kExpected) {
            handleGlobalParamChange(q, e.id, 0.83);
        }
        REQUIRE(bitsOf(q.masterGain.load()) == bitsOf(1.0f));
        REQUIRE(q.polyphony.load() == 4);
    }

    SECTION("UnregisteredInBandIgnored") {
        GlobalParams p;
        p.masterGain.store(1.3f);
        p.polyphony.store(2);
        setExt(p, 7, 0.41f);
        p.sustainPedal.store(1.0f);
        p.channelPressure.store(0.36f);
        const Snapshot before = snapshot(p);

        // Band 0-99: 0-5 used; every other ID is a gap.
        for (ParamID id = kChannelPressureId + 1; id < kGlobalParamRangeEnd; ++id) {
            handleGlobalParamChange(p, id, 0.0);
            handleGlobalParamChange(p, id, 0.93);
            handleGlobalParamChange(p, id, 1.0);
        }
        REQUIRE(snapshot(p) == before);
    }

    SECTION("SaveLoadRoundTrip") {
        GlobalParams out;
        setExt(out, kSavedSeed, kSavedSat);
        // Performance controllers are set but must NEVER reach the stream.
        out.sustainPedal.store(1.0f);
        out.channelPressure.store(0.8f);
        auto stream = makeStream();
        {
            Steinberg::IBStreamer w(stream, kLittleEndian);
            saveGlobalParamsV2Ext(out, w);
        }
        REQUIRE(std::cmp_equal(stream->getSize(), kB));

        // Byte-for-byte: int32 seedIndex then float outputSaturation, nothing else.
        {
            auto expected = streamOf(kSavedSeed, kSavedSat);
            REQUIRE(bytesOf(*stream) == bytesOf(*expected));
        }

        rewindStream(*stream);
        GlobalParams in;
        {
            Steinberg::IBStreamer r(stream, kLittleEndian);
            REQUIRE(loadGlobalParamsV2Ext(in, r));
        }
        REQUIRE(in.seedIndex.load() == kSavedSeed);
        REQUIRE(bitsOf(in.outputSaturation.load()) == bitsOf(kSavedSat));
        // Not persisted: the loaded struct keeps its defaults for both controllers,
        // and the v1 fields are untouched by the extension loader.
        REQUIRE(in.sustainPedal.load() == 0.0f);
        REQUIRE(in.channelPressure.load() == 0.0f);
        REQUIRE(bitsOf(in.masterGain.load()) == bitsOf(1.0f));
        REQUIRE(in.polyphony.load() == 4);
    }

    SECTION("V1BlockUnchanged") {
        // v1 saveGlobalParams still writes exactly 8 bytes (masterGain, polyphony),
        // byte-for-byte equal to a hand-built stream at defaults, regardless of the
        // Phase 12 fields.
        GlobalParams p;
        setExt(p, kSavedSeed, kSavedSat);
        p.sustainPedal.store(1.0f);
        p.channelPressure.store(0.5f);
        auto got = makeStream();
        {
            Steinberg::IBStreamer w(got, kLittleEndian);
            saveGlobalParams(p, w);
        }
        REQUIRE(std::cmp_equal(got->getSize(), 8u));

        auto expected = makeStream();
        {
            Steinberg::IBStreamer w(expected, kLittleEndian);
            w.writeFloat(1.0f);
            w.writeInt32(4);
        }
        REQUIRE(bytesOf(*got) == bytesOf(*expected));

        // v1 load reads exactly those 8 bytes and leaves every Phase 12 field alone.
        rewindStream(*got);
        GlobalParams in;
        setExt(in, kDirtySeed, kDirtySat);
        {
            Steinberg::IBStreamer r(got, kLittleEndian);
            REQUIRE(loadGlobalParams(in, r));
        }
        REQUIRE(in.seedIndex.load() == kDirtySeed);
        REQUIRE(bitsOf(in.outputSaturation.load()) == bitsOf(kDirtySat));
    }

    SECTION("TruncationEveryOffset") {
        auto full = savedStream();
        REQUIRE(std::cmp_equal(full->getSize(), kB));
        const auto* bytes = reinterpret_cast<const unsigned char*>(full->getData());  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)

        for (std::size_t c = 0; c < kB; ++c) {
            INFO("cut " << c);
            auto cut = makeStream();
            if (c > 0) {
                Steinberg::int32 written = 0;
                REQUIRE(cut->write(const_cast<unsigned char*>(bytes),  // NOLINT(cppcoreguidelines-pro-type-const-cast)
                                   static_cast<Steinberg::int32>(c), &written) ==
                        Steinberg::kResultOk);
                REQUIRE(std::cmp_equal(written, c));
            }
            rewindStream(*cut);

            GlobalParams p;
            setExt(p, kDirtySeed, kDirtySat);
            {
                Steinberg::IBStreamer r(cut, kLittleEndian);
                REQUIRE_FALSE(loadGlobalParamsV2Ext(p, r));
            }
            // seedIndex occupies [0, 4); outputSaturation [4, 8) is never fully
            // contained for c < 8.
            REQUIRE(p.seedIndex.load() == (c >= 4u ? kSavedSeed : kDirtySeed));
            REQUIRE(bitsOf(p.outputSaturation.load()) == bitsOf(kDirtySat));
        }
    }

    SECTION("NonFiniteAndClamp") {
        const std::array<float, 3> bad = {floatFromBits(0x7FC00000u),   // NaN
                                          floatFromBits(0x7F800000u),   // +Inf
                                          floatFromBits(0xFF800000u)};  // -Inf
        for (const float b : bad) {
            INFO("bits " << bitsOf(b));
            auto s = streamOf(kSavedSeed, b);
            GlobalParams p;
            setExt(p, kDirtySeed, kDirtySat);
            {
                Steinberg::IBStreamer r(s, kLittleEndian);
                REQUIRE(loadGlobalParamsV2Ext(p, r));
            }
            REQUIRE(p.seedIndex.load() == kSavedSeed);  // earlier field loaded
            REQUIRE(bitsOf(p.outputSaturation.load()) == bitsOf(kDirtySat));
        }

        // Finite out-of-range saturation clamps to [0, 1].
        {
            auto s = streamOf(0, 1.5f);
            GlobalParams p;
            Steinberg::IBStreamer r(s, kLittleEndian);
            REQUIRE(loadGlobalParamsV2Ext(p, r));
            REQUIRE(p.outputSaturation.load() == 1.0f);
        }
        {
            auto s = streamOf(0, -0.5f);
            GlobalParams p;
            Steinberg::IBStreamer r(s, kLittleEndian);
            REQUIRE(loadGlobalParamsV2Ext(p, r));
            REQUIRE(p.outputSaturation.load() == 0.0f);
        }

        // Seed index -1 / 16 clamp to 0 / 15.
        {
            auto s = streamOf(-1, 0.5f);
            GlobalParams p;
            p.seedIndex.store(9);
            Steinberg::IBStreamer r(s, kLittleEndian);
            REQUIRE(loadGlobalParamsV2Ext(p, r));
            REQUIRE(p.seedIndex.load() == 0);
        }
        {
            auto s = streamOf(16, 0.5f);
            GlobalParams p;
            Steinberg::IBStreamer r(s, kLittleEndian);
            REQUIRE(loadGlobalParamsV2Ext(p, r));
            REQUIRE(p.seedIndex.load() == 15);
        }
    }

    SECTION("ControllerMirror") {
        auto s = savedStream();
        std::vector<std::pair<ParamID, double>> calls;
        {
            Steinberg::IBStreamer r(s, kLittleEndian);
            loadGlobalParamsV2ExtToController(r, [&calls](ParamID id, double v) {
                calls.emplace_back(id, v);
            });
        }
        REQUIRE(calls.size() == 2u);
        REQUIRE(calls[0].first == kSeedId);
        REQUIRE(calls[0].second == Approx(indexToNormalized(kSavedSeed, 16)).margin(1e-9));
        REQUIRE(calls[1].first == kOutputSaturationId);
        REQUIRE(calls[1].second ==
                Approx(linearToNormalized(static_cast<double>(kSavedSat), 0.0, 1.0)).margin(1e-9));
    }

    SECTION("FormatNonEmpty") {
        for (const auto& e : kExpected) {
            if (e.id == kSeedId) {
                continue;  // StringListParameter formats itself
            }
            for (const double v : {0.0, 0.5, 1.0}) {
                INFO("id " << e.id << " value " << v);
                Steinberg::Vst::String128 s{};
                REQUIRE(formatGlobalParam(e.id, v, s) == Steinberg::kResultOk);
                REQUIRE_FALSE(toAsciiString(s).empty());
            }
        }
        Steinberg::Vst::String128 s{};
        REQUIRE(formatGlobalParam(kSeedId, 1.0, s) == Steinberg::kResultFalse);
        REQUIRE(formatGlobalParam(kChannelPressureId + 1, 0.5, s) == Steinberg::kResultFalse);
    }
}
