// ==============================================================================
// Vorago Phase 12 - ghost parameter pack contract (T028)
// ==============================================================================
// Group 7 shared test shape (specs/vorago-phase12-parameters/tasks.md) for the
// Ghost pack: IDs 1400-1403, N = 4, stream B = 16 bytes (3 float + 1 int32).
//
//   1400 Ghost Peak Level          %  [0, 1]  0.60  n0 0.60  lin
//   1401 Ghost Blur                %  [0, 1]  0.85  n0 0.85  lin
//   1402 Ghost Reverse Probability %  [0, 1]  0.0   n0 0.0   lin
//   1403 Ghost Event Triggers      L(2) Off, On   0  n0 0.0
//
// Built with -fno-fast-math (tests/CMakeLists.txt): NaN/Inf payloads are built
// from bit patterns through a volatile uint32 + memcpy.
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "parameters/ghost_params.h"
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

constexpr int kN = 4;
constexpr std::size_t kB = 16;

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

std::uint32_t bitsOf(float f) { return std::bit_cast<std::uint32_t>(f); }

float floatFromBits(std::uint32_t pattern) {
    volatile std::uint32_t bits = pattern;
    const std::uint32_t copy = bits;
    float f = 0.0f;
    std::memcpy(&f, &copy, sizeof(f));
    return f;
}

// Four fields in stream order: three floats (bit patterns) then one index.
struct Snapshot {
    std::array<std::uint32_t, 3> floats{};
    int triggers = 0;
    bool operator==(const Snapshot&) const = default;
};

Snapshot snapshot(const ::Vorago::GhostParams& p) {
    return {.floats={bitsOf(p.peakLevel.load()), bitsOf(p.blur.load()),
             bitsOf(p.reverseProbability.load())},
            .triggers=p.eventTriggers.load()};
}

void setAll(::Vorago::GhostParams& p, float peak, float blur, float rev, int trig) {
    p.peakLevel.store(peak);
    p.blur.store(blur);
    p.reverseProbability.store(rev);
    p.eventTriggers.store(trig);
}

// Save values written in the stream (SECTION 4 non-defaults).
constexpr float kSavedPeak = 0.27f;
constexpr float kSavedBlur = 0.43f;
constexpr float kSavedRev = 0.71f;
constexpr int kSavedTrig = 1;

// Pre-dirtied values, distinct from the saved values.
constexpr float kDirtyPeak = 0.11f;
constexpr float kDirtyBlur = 0.22f;
constexpr float kDirtyRev = 0.33f;
constexpr int kDirtyTrig = 0;

Steinberg::IPtr<Steinberg::MemoryStream> savedStream() {
    ::Vorago::GhostParams p;
    setAll(p, kSavedPeak, kSavedBlur, kSavedRev, kSavedTrig);
    auto stream = makeStream();
    {
        Steinberg::IBStreamer w(stream, kLittleEndian);
        ::Vorago::saveGhostParams(p, w);
    }
    rewindStream(*stream);
    return stream;
}

// A stream holding the given three floats + one int32, little-endian.
Steinberg::IPtr<Steinberg::MemoryStream> streamOf(float a, float b, float c, Steinberg::int32 i) {
    auto stream = makeStream();
    {
        Steinberg::IBStreamer w(stream, kLittleEndian);
        w.writeFloat(a);
        w.writeFloat(b);
        w.writeFloat(c);
        w.writeInt32(i);
    }
    rewindStream(*stream);
    return stream;
}

struct Expected {
    ParamID id;
    const char* title;
    const char* units;
    Steinberg::int32 stepCount;
    bool isList;
    double n0;
};

constexpr std::array<Expected, kN> kExpected = {{
    {.id=::Vorago::kGhostPeakLevelId, .title="Ghost Peak Level", .units="%", .stepCount=0, .isList=false, .n0=0.60},
    {.id=::Vorago::kGhostBlurId, .title="Ghost Blur", .units="%", .stepCount=0, .isList=false, .n0=0.85},
    {.id=::Vorago::kGhostReverseProbabilityId, .title="Ghost Reverse Probability", .units="%", .stepCount=0, .isList=false, .n0=0.0},
    {.id=::Vorago::kGhostEventTriggersId, .title="Ghost Event Triggers", .units="", .stepCount=1, .isList=true, .n0=0.0},
}};

}  // namespace

TEST_CASE("Vorago_GhostParamsContract", "[vorago][params]") {
    using namespace ::Vorago;

    SECTION("Registration") {
        Steinberg::Vst::ParameterContainer pc;
        registerGhostParams(pc);
        REQUIRE(pc.getParameterCount() == kN);

        for (const auto& e : kExpected) {
            INFO("id " << e.id);
            auto* p = pc.getParameter(e.id);
            REQUIRE(p != nullptr);
            const auto& info = p->getInfo();
            REQUIRE(toAsciiString(info.title) == e.title);
            REQUIRE(toAsciiString(info.units) == e.units);
            REQUIRE(info.stepCount == e.stepCount);
            const Steinberg::int32 expectedFlags =
                Steinberg::Vst::ParameterInfo::kCanAutomate |
                (e.isList ? Steinberg::Vst::ParameterInfo::kIsList : 0);
            REQUIRE(info.flags == expectedFlags);
            REQUIRE(info.defaultNormalizedValue == Approx(e.n0).margin(1e-9));
        }

        // List labels, in index order.
        auto* trig = pc.getParameter(kGhostEventTriggersId);
        REQUIRE(trig != nullptr);
        Steinberg::Vst::String128 s{};
        trig->toString(0.0, s);
        REQUIRE(toAsciiString(s) == "Off");
        trig->toString(1.0, s);
        REQUIRE(toAsciiString(s) == "On");
    }

    SECTION("DefaultsDenormalizeToPlain") {
        const GhostParams fresh;
        REQUIRE(fresh.peakLevel.load() == Approx(0.60f).epsilon(1e-6));
        REQUIRE(fresh.blur.load() == Approx(0.85f).epsilon(1e-6));
        REQUIRE(fresh.reverseProbability.load() == 0.0f);
        REQUIRE(fresh.eventTriggers.load() == 0);

        GhostParams p;
        setAll(p, 0.5f, 0.5f, 0.5f, 1);
        for (const auto& e : kExpected) {
            handleGhostParamChange(p, e.id, e.n0);
        }
        REQUIRE(p.peakLevel.load() == Approx(fresh.peakLevel.load()).epsilon(1e-6));
        REQUIRE(p.blur.load() == Approx(fresh.blur.load()).epsilon(1e-6));
        REQUIRE(p.reverseProbability.load() == Approx(fresh.reverseProbability.load()).margin(1e-9));
        REQUIRE(p.eventTriggers.load() == fresh.eventTriggers.load());

        // Range ends.
        for (const auto& e : kExpected) {
            handleGhostParamChange(p, e.id, 0.0);
        }
        REQUIRE(p.peakLevel.load() == 0.0f);
        REQUIRE(p.blur.load() == 0.0f);
        REQUIRE(p.reverseProbability.load() == 0.0f);
        REQUIRE(p.eventTriggers.load() == 0);

        for (const auto& e : kExpected) {
            handleGhostParamChange(p, e.id, 1.0);
        }
        REQUIRE(p.peakLevel.load() == 1.0f);
        REQUIRE(p.blur.load() == 1.0f);
        REQUIRE(p.reverseProbability.load() == 1.0f);
        REQUIRE(p.eventTriggers.load() == 1);

        // Discrete midpoint split: < 0.5 -> Off, >= 0.5 -> On.
        handleGhostParamChange(p, kGhostEventTriggersId, 0.49);
        REQUIRE(p.eventTriggers.load() == 0);
        handleGhostParamChange(p, kGhostEventTriggersId, 0.5);
        REQUIRE(p.eventTriggers.load() == 1);
    }

    SECTION("UnregisteredInBandIgnored") {
        GhostParams p;
        setAll(p, 0.31f, 0.52f, 0.17f, 1);
        const Snapshot before = snapshot(p);

        // Band 1400-1499: 1400-1403 used; every other ID is a gap.
        for (ParamID id = kGhostEventTriggersId + 1; id < kGhostParamRangeEnd; ++id) {
            handleGhostParamChange(p, id, 0.0);
            handleGhostParamChange(p, id, 0.93);
            handleGhostParamChange(p, id, 1.0);
        }
        REQUIRE(snapshot(p) == before);
    }

    SECTION("SaveLoadRoundTrip") {
        GhostParams out;
        setAll(out, kSavedPeak, kSavedBlur, kSavedRev, kSavedTrig);
        auto stream = makeStream();
        {
            Steinberg::IBStreamer w(stream, kLittleEndian);
            saveGhostParams(out, w);
        }
        REQUIRE(std::cmp_equal(stream->getSize(), kB));

        rewindStream(*stream);
        GhostParams in;
        {
            Steinberg::IBStreamer r(stream, kLittleEndian);
            REQUIRE(loadGhostParams(in, r));
        }
        REQUIRE(snapshot(in) == snapshot(out));
    }

    SECTION("TruncationEveryOffset") {
        auto full = savedStream();
        REQUIRE(std::cmp_equal(full->getSize(), kB));
        const auto* bytes = reinterpret_cast<const unsigned char*>(full->getData());  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)

        const Snapshot saved{.floats={bitsOf(kSavedPeak), bitsOf(kSavedBlur), bitsOf(kSavedRev)}, .triggers=kSavedTrig};
        const Snapshot dirty{.floats={bitsOf(kDirtyPeak), bitsOf(kDirtyBlur), bitsOf(kDirtyRev)}, .triggers=kDirtyTrig};

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

            GhostParams p;
            setAll(p, kDirtyPeak, kDirtyBlur, kDirtyRev, kDirtyTrig);
            {
                Steinberg::IBStreamer r(cut, kLittleEndian);
                REQUIRE_FALSE(loadGhostParams(p, r));
            }
            const Snapshot got = snapshot(p);
            for (std::size_t f = 0; f < 3; ++f) {
                const bool contained = (4u * f + 4u) <= c;
                REQUIRE(got.floats[f] == (contained ? saved.floats[f] : dirty.floats[f]));
            }
            // The int32 field occupies bytes [12, 16): never fully contained for c < 16.
            REQUIRE(got.triggers == dirty.triggers);
        }
    }

    SECTION("NonFiniteAndClamp") {
        const std::array<float, 3> bad = {floatFromBits(0x7FC00000u),   // NaN
                                          floatFromBits(0x7F800000u),   // +Inf
                                          floatFromBits(0xFF800000u)};  // -Inf
        const std::array<float, 3> dirtyF = {kDirtyPeak, kDirtyBlur, kDirtyRev};
        const std::array<float, 3> savedF = {kSavedPeak, kSavedBlur, kSavedRev};

        for (std::size_t f = 0; f < 3; ++f) {
            for (const float b : bad) {
                INFO("field " << f << " bits " << bitsOf(b));
                std::array<float, 3> values = savedF;
                values[f] = b;
                auto s = streamOf(values[0], values[1], values[2], kSavedTrig);

                GhostParams p;
                setAll(p, kDirtyPeak, kDirtyBlur, kDirtyRev, kDirtyTrig);
                {
                    Steinberg::IBStreamer r(s, kLittleEndian);
                    REQUIRE(loadGhostParams(p, r));
                }
                const Snapshot got = snapshot(p);
                for (std::size_t g = 0; g < 3; ++g) {
                    REQUIRE(got.floats[g] == bitsOf(g == f ? dirtyF[g] : savedF[g]));
                }
                REQUIRE(got.triggers == kSavedTrig);  // later field still loaded
            }
        }

        // Finite out-of-range floats clamp to [0, 1].
        {
            auto s = streamOf(1.5f, -0.5f, 7.0f, 0);
            GhostParams p;
            Steinberg::IBStreamer r(s, kLittleEndian);
            REQUIRE(loadGhostParams(p, r));
            REQUIRE(p.peakLevel.load() == 1.0f);
            REQUIRE(p.blur.load() == 0.0f);
            REQUIRE(p.reverseProbability.load() == 1.0f);
        }
        {
            auto s = streamOf(-3.0f, 2.0f, -0.25f, 0);
            GhostParams p;
            Steinberg::IBStreamer r(s, kLittleEndian);
            REQUIRE(loadGhostParams(p, r));
            REQUIRE(p.peakLevel.load() == 0.0f);
            REQUIRE(p.blur.load() == 1.0f);
            REQUIRE(p.reverseProbability.load() == 0.0f);
        }

        // Index -1 / n clamp to 0 / n-1 (n = 2).
        {
            auto s = streamOf(0.5f, 0.5f, 0.5f, -1);
            GhostParams p;
            p.eventTriggers.store(1);
            Steinberg::IBStreamer r(s, kLittleEndian);
            REQUIRE(loadGhostParams(p, r));
            REQUIRE(p.eventTriggers.load() == 0);
        }
        {
            auto s = streamOf(0.5f, 0.5f, 0.5f, 2);
            GhostParams p;
            Steinberg::IBStreamer r(s, kLittleEndian);
            REQUIRE(loadGhostParams(p, r));
            REQUIRE(p.eventTriggers.load() == 1);
        }
    }

    SECTION("ControllerMirror") {
        auto s = savedStream();
        std::vector<std::pair<ParamID, double>> calls;
        {
            Steinberg::IBStreamer r(s, kLittleEndian);
            loadGhostParamsToController(r, [&calls](ParamID id, double v) {
                calls.emplace_back(id, v);
            });
        }
        REQUIRE(calls.size() == static_cast<std::size_t>(kN));

        const std::array<double, kN> expected = {
            linearToNormalized(static_cast<double>(kSavedPeak), 0.0, 1.0),
            linearToNormalized(static_cast<double>(kSavedBlur), 0.0, 1.0),
            linearToNormalized(static_cast<double>(kSavedRev), 0.0, 1.0),
            indexToNormalized(kSavedTrig, 2)};
        for (std::size_t k = 0; k < calls.size(); ++k) {
            INFO("field " << k);
            REQUIRE(calls[k].first == kExpected[k].id);
            REQUIRE(calls[k].second == Approx(expected[k]).margin(1e-9));
        }
    }

    SECTION("FormatNonEmpty") {
        for (const auto& e : kExpected) {
            if (e.isList) {
                continue;
            }
            for (const double v : {0.0, 0.5, 1.0}) {
                INFO("id " << e.id << " value " << v);
                Steinberg::Vst::String128 s{};
                REQUIRE(formatGhostParam(e.id, v, s) == Steinberg::kResultOk);
                REQUIRE_FALSE(toAsciiString(s).empty());
            }
        }
        Steinberg::Vst::String128 s{};
        REQUIRE(formatGhostParam(kGhostEventTriggersId, 1.0, s) == Steinberg::kResultFalse);
        REQUIRE(formatGhostParam(kGhostEventTriggersId + 1, 0.5, s) == Steinberg::kResultFalse);
    }
}
