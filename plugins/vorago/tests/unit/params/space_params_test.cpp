// ==============================================================================
// Vorago Phase 12 - space parameter pack contract (T025)
// ==============================================================================
// The Group 7 shared test shape (specs/vorago-phase12-parameters/tasks.md) for the
// Space pack: 16 IDs 1100-1115, stream B = 64 bytes (15 floats + int32 freeze).
// Defaults are additionally pinned to CavernVerb::kDefault* (cavern_verb.h:221,
// 247-265) so a DSP default change cannot silently diverge from the surface.
//
// Built with -fno-fast-math (tests/CMakeLists.txt): NaN/Inf payloads are produced
// from bit patterns through a volatile uint32 + memcpy.
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "parameters/param_mapping.h"
#include "parameters/space_params.h"
#include "plugin_ids.h"

#include <krate/dsp/effects/cavern_verb.h>

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <array>
#include <atomic>
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
using Vorago::SpaceParams;
using Vorago::Taper;

struct SpaceFloatSpec {
    ParamID id;
    const char* title;
    const char* units;
    double mn;
    double mx;
    double def;
    double n0;
    Taper taper;
    double eps;
    std::atomic<float> SpaceParams::*field;
};

// Independent restatement of tasks.md T025 / plan section 3.2 (ascending ID order).
const std::array<SpaceFloatSpec, 15> kSpecs = {{
    {.id=Vorago::kSpaceSizeId, .title="Space Size", .units="%", .mn=0.0, .mx=1.0, .def=0.50, .n0=0.50, .taper=Taper::Linear, .eps=0.0,
     .field=&SpaceParams::size},
    {.id=Vorago::kSpaceDarknessId, .title="Space Darkness", .units="%", .mn=0.0, .mx=1.0, .def=0.80, .n0=0.80, .taper=Taper::Linear, .eps=0.0,
     .field=&SpaceParams::darkness},
    {.id=Vorago::kSpaceDecayId, .title="Space Decay", .units="s", .mn=0.5, .mx=60.0, .def=20.0, .n0=0.770524453, .taper=Taper::Log, .eps=0.0,
     .field=&SpaceParams::decaySeconds},
    {.id=Vorago::kSpaceFogId, .title="Space Fog", .units="%", .mn=0.0, .mx=1.0, .def=0.30, .n0=0.30, .taper=Taper::Linear, .eps=0.0,
     .field=&SpaceParams::fog},
    {.id=Vorago::kSpaceDamperDepthId, .title="Space Damper Depth", .units="%", .mn=0.0, .mx=1.0, .def=0.35, .n0=0.35, .taper=Taper::Linear,
     .eps=0.0, .field=&SpaceParams::damperDepth},
    {.id=Vorago::kSpaceMixId, .title="Space Mix", .units="%", .mn=0.0, .mx=1.0, .def=1.00, .n0=1.00, .taper=Taper::Linear, .eps=0.0,
     .field=&SpaceParams::mix},
    {.id=Vorago::kSpaceWidthId, .title="Space Width", .units="%", .mn=0.0, .mx=1.0, .def=1.00, .n0=1.00, .taper=Taper::Linear, .eps=0.0,
     .field=&SpaceParams::width},
    {.id=Vorago::kSpaceDensityId, .title="Space Density", .units="%", .mn=0.0, .mx=1.0, .def=0.75, .n0=0.75, .taper=Taper::Linear, .eps=0.0,
     .field=&SpaceParams::density},
    {.id=Vorago::kSpaceDimensionalityId, .title="Space Dimensionality", .units="%", .mn=0.0, .mx=1.0, .def=0.50, .n0=0.50,
     .taper=Taper::Linear, .eps=0.0, .field=&SpaceParams::dimensionality},
    {.id=Vorago::kSpaceBreathId, .title="Space Breath", .units="%", .mn=0.0, .mx=1.0, .def=0.50, .n0=0.50, .taper=Taper::Linear, .eps=0.0,
     .field=&SpaceParams::breath},
    {.id=Vorago::kSpaceEarlySizeId, .title="Space Early Size", .units="ms", .mn=80.0, .mx=300.0, .def=220.0, .n0=0.765346277,
     .taper=Taper::Log, .eps=0.0, .field=&SpaceParams::earlySizeMs},
    {.id=Vorago::kSpaceEarlyLevelId, .title="Space Early Level", .units="%", .mn=0.0, .mx=1.0, .def=0.80, .n0=0.80, .taper=Taper::Linear,
     .eps=0.0, .field=&SpaceParams::earlyLevel},
    {.id=Vorago::kSpaceEarlyAbsorptionId, .title="Space Early Absorption", .units="%", .mn=0.0, .mx=1.0, .def=0.60, .n0=0.60,
     .taper=Taper::Linear, .eps=0.0, .field=&SpaceParams::earlyAbsorption},
    {.id=Vorago::kSpaceEarlySendId, .title="Space Early Send", .units="%", .mn=0.0, .mx=1.0, .def=0.70, .n0=0.70, .taper=Taper::Linear, .eps=0.0,
     .field=&SpaceParams::earlySend},
    {.id=Vorago::kSpaceDamperRateId, .title="Space Damper Rate", .units="", .mn=0.0, .mx=1.0, .def=0.15, .n0=0.600761933,
     .taper=Taper::OffsetLog, .eps=0.01, .field=&SpaceParams::damperRate},
}};

constexpr std::size_t kNumFloats = 15;
constexpr std::size_t kStreamBytes = 64;  // B = 15 * 4 + 4

double specFromNormalized(const SpaceFloatSpec& s, double n) {
    switch (s.taper) {
        case Taper::Log:
            return Krate::Plugins::logMapFromNormalized(n, s.mn, s.mx);
        case Taper::OffsetLog:
            return Vorago::offsetLogFromNormalized(n, s.mn, s.mx, s.eps);
        default:
            return Vorago::linearFromNormalized(n, s.mn, s.mx);
    }
}

double specToNormalized(const SpaceFloatSpec& s, double u) {
    switch (s.taper) {
        case Taper::Log:
            return Krate::Plugins::logMapToNormalized(u, s.mn, s.mx);
        case Taper::OffsetLog:
            return Vorago::offsetLogToNormalized(u, s.mn, s.mx, s.eps);
        default:
            return Vorago::linearToNormalized(u, s.mn, s.mx);
    }
}

std::string toAsciiString(const Steinberg::Vst::String128 s) {
    char buf[128] = {};
    Steinberg::UString(const_cast<Steinberg::Vst::TChar*>(s), 128)  // NOLINT(cppcoreguidelines-pro-type-const-cast)
        .toAscii(buf, 128);
    return {buf};
}

std::uint32_t bitsOf(float f) { return std::bit_cast<std::uint32_t>(f); }

float floatFromBits(std::uint32_t bits) {
    volatile std::uint32_t v = bits;
    const std::uint32_t b = v;
    float f = 0.0f;
    std::memcpy(&f, &b, sizeof(f));
    return f;
}

using Snapshot = std::array<std::uint32_t, kNumFloats + 1>;

Snapshot snapshot(const SpaceParams& p) {
    Snapshot out{};
    for (std::size_t i = 0; i < kNumFloats; ++i)
        out[i] = bitsOf((p.*(kSpecs[i].field)).load());
    out[kNumFloats] = static_cast<std::uint32_t>(p.freeze.load());
    return out;
}

// Non-default, in-range plain values: fraction 0.13 + 0.05 i of each range
// (checked against every default in tasks.md T025 - none coincide).
std::array<float, kNumFloats> nonDefaultPlains() {
    std::array<float, kNumFloats> v{};
    for (std::size_t i = 0; i < kNumFloats; ++i) {
        const auto& s = kSpecs[i];
        v[i] = static_cast<float>(s.mn + (s.mx - s.mn) * (0.13 + 0.05 * static_cast<double>(i)));
    }
    return v;
}

// A "dirty" struct distinct from both the defaults and nonDefaultPlains().
void dirty(SpaceParams& p) {
    for (const auto& s : kSpecs)
        (p.*(s.field)).store(static_cast<float>(s.mn + (s.mx - s.mn) * 0.97));
    p.freeze.store(0);
}

void setNonDefault(SpaceParams& p) {
    const auto v = nonDefaultPlains();
    for (std::size_t i = 0; i < kNumFloats; ++i)
        (p.*(kSpecs[i].field)).store(v[i]);
    p.freeze.store(1);
}

Steinberg::IPtr<Steinberg::MemoryStream> makeStream() {
    return Steinberg::owned(new Steinberg::MemoryStream());
}

void rewindStream(Steinberg::MemoryStream& s) {
    REQUIRE(s.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr) == Steinberg::kResultOk);
}

std::vector<char> streamBytes(const Steinberg::MemoryStream& s) {
    const auto n = static_cast<std::size_t>(s.getSize());
    return {s.getData(), s.getData() + n};
}

Steinberg::IPtr<Steinberg::MemoryStream> streamFromBytes(const std::vector<char>& bytes,
                                                         std::size_t count) {
    auto s = makeStream();
    if (count > 0) {
        std::vector<char> copy(bytes.begin(),
                               bytes.begin() + static_cast<std::ptrdiff_t>(count));
        REQUIRE(s->write(copy.data(), static_cast<Steinberg::int32>(count), nullptr) ==
                Steinberg::kResultOk);
    }
    rewindStream(*s);
    return s;
}

// Hand-built stream: 15 floats (ascending ID) + int32 freeze.
Steinberg::IPtr<Steinberg::MemoryStream> handStream(const std::array<float, kNumFloats>& floats,
                                                    Steinberg::int32 freeze) {
    auto s = makeStream();
    {
        Steinberg::IBStreamer w(s, kLittleEndian);
        for (float f : floats)
            REQUIRE(w.writeFloat(f));
        REQUIRE(w.writeInt32(freeze));
    }
    rewindStream(*s);
    return s;
}

std::vector<char> savedNonDefaultBytes() {
    SpaceParams p;
    setNonDefault(p);
    auto s = makeStream();
    {
        Steinberg::IBStreamer w(s, kLittleEndian);
        Vorago::saveSpaceParams(p, w);
    }
    return streamBytes(*s);
}

std::uint32_t leWordAt(const std::vector<char>& b, std::size_t offset) {
    REQUIRE(offset + 4u <= b.size());
    const auto u = [&](std::size_t k) {
        return static_cast<std::uint32_t>(static_cast<unsigned char>(b[offset + k]));
    };
    return u(0) | (u(1) << 8u) | (u(2) << 16u) | (u(3) << 24u);
}

}  // namespace

TEST_CASE("Vorago_SpaceParamsContract", "[vorago][params]") {
    using namespace Steinberg;
    using namespace Steinberg::Vst;
    using Krate::DSP::CavernVerb;

    SECTION("Registration") {
        ParameterContainer pc;
        Vorago::registerSpaceParams(pc);
        REQUIRE(pc.getParameterCount() == 16);

        for (const auto& s : kSpecs) {
            INFO("id " << s.id);
            auto* p = pc.getParameter(s.id);
            REQUIRE(p != nullptr);
            const auto& info = p->getInfo();
            REQUIRE(toAsciiString(info.title) == s.title);
            REQUIRE(toAsciiString(info.units) == s.units);
            REQUIRE(info.stepCount == 0);
            REQUIRE(info.flags == ParameterInfo::kCanAutomate);
            REQUIRE(info.defaultNormalizedValue == Approx(s.n0).margin(1e-9));
            // The spec table's n0 is the inverse map of the default (plan 3.3).
            REQUIRE(specToNormalized(s, s.def) == Approx(s.n0).margin(1e-9));
        }

        auto* fz = pc.getParameter(Vorago::kSpaceFreezeId);
        REQUIRE(fz != nullptr);
        const auto& fi = fz->getInfo();
        REQUIRE(toAsciiString(fi.title) == "Space Freeze");
        REQUIRE(fi.stepCount == 1);
        REQUIRE(fi.flags == (ParameterInfo::kCanAutomate | ParameterInfo::kIsList));
        REQUIRE(fi.defaultNormalizedValue == Approx(0.0).margin(1e-9));
        String128 sOff{};
        String128 sOn{};
        fz->toString(0.0, sOff);
        fz->toString(1.0, sOn);
        REQUIRE(toAsciiString(sOff) == "Off");
        REQUIRE(toAsciiString(sOn) == "On");
    }

    SECTION("DefaultsDenormalizeToPlain") {
        const SpaceParams fresh;
        for (const auto& s : kSpecs) {
            INFO("id " << s.id);
            REQUIRE((fresh.*(s.field)).load() == Approx(s.def).epsilon(1e-6));

            SpaceParams p;
            dirty(p);
            Vorago::handleSpaceParamChange(p, s.id, s.n0);
            REQUIRE((p.*(s.field)).load() == Approx(s.def).epsilon(1e-6));

            Vorago::handleSpaceParamChange(p, s.id, 0.0);
            REQUIRE((p.*(s.field)).load() == Approx(s.mn).margin(1e-6));
            Vorago::handleSpaceParamChange(p, s.id, 1.0);
            REQUIRE((p.*(s.field)).load() == Approx(s.mx).epsilon(1e-6));

            Vorago::handleSpaceParamChange(p, s.id, 0.5);
            REQUIRE((p.*(s.field)).load() == Approx(specFromNormalized(s, 0.5)).epsilon(1e-6));
        }

        // Taper midpoints (plan 3.3 / 3.3.1).
        SpaceParams t;
        Vorago::handleSpaceParamChange(t, Vorago::kSpaceDecayId, 0.5);
        REQUIRE(t.decaySeconds.load() == Approx(5.4772).epsilon(1e-4));
        Vorago::handleSpaceParamChange(t, Vorago::kSpaceEarlySizeId, 0.5);
        REQUIRE(t.earlySizeMs.load() == Approx(154.919).epsilon(1e-4));
        Vorago::handleSpaceParamChange(t, Vorago::kSpaceDamperRateId, 0.5);
        REQUIRE(t.damperRate.load() == Approx(0.090499).margin(1e-6));
        Vorago::handleSpaceParamChange(t, Vorago::kSpaceDamperRateId, 0.0);
        REQUIRE(t.damperRate.load() == 0.0f);  // offset-log reaches exactly 0

        // Freeze: L(2) Off/On, default Off.
        REQUIRE(fresh.freeze.load() == 0);
        SpaceParams f;
        Vorago::handleSpaceParamChange(f, Vorago::kSpaceFreezeId, 1.0);
        REQUIRE(f.freeze.load() == 1);
        Vorago::handleSpaceParamChange(f, Vorago::kSpaceFreezeId, 0.0);
        REQUIRE(f.freeze.load() == 0);

        // Pin every default the DSP owns to CavernVerb (cavern_verb.h:221, 247-265).
        REQUIRE(fresh.size.load() == CavernVerb::kDefaultSize);
        REQUIRE(fresh.darkness.load() == CavernVerb::kDefaultDarkness);
        REQUIRE(fresh.decaySeconds.load() == CavernVerb::kDefaultDecaySeconds);
        REQUIRE(fresh.fog.load() == CavernVerb::kDefaultFog);
        REQUIRE(fresh.damperDepth.load() == CavernVerb::kDefaultDamperDepth);
        REQUIRE(fresh.mix.load() == CavernVerb::kDefaultMix);
        REQUIRE(fresh.width.load() == CavernVerb::kDefaultWidth);
        REQUIRE(fresh.density.load() == CavernVerb::kDefaultDensity);
        REQUIRE(fresh.dimensionality.load() == CavernVerb::kDefaultDimensionality);
        REQUIRE(fresh.breath.load() == CavernVerb::kDefaultBreath);
        REQUIRE(fresh.earlySizeMs.load() == CavernVerb::kDefaultEarlySizeMs);
        REQUIRE(fresh.earlyLevel.load() == CavernVerb::kDefaultEarlyLevel);
        REQUIRE(fresh.earlyAbsorption.load() == CavernVerb::kDefaultEarlyAbsorption);
        REQUIRE(fresh.earlySend.load() == CavernVerb::kDefaultEarlySend);
        REQUIRE(fresh.damperRate.load() == CavernVerb::kDefaultDamperRate);

        // Range ends are the DSP's (decay: kCavernDecay*; early size: plan D-P5).
        REQUIRE(kSpecs[2].mn == Approx(CavernVerb::kCavernDecayMinSeconds));
        REQUIRE(kSpecs[2].mx == Approx(CavernVerb::kCavernDecayMaxSeconds));
        REQUIRE(kSpecs[10].mn == Approx(CavernVerb::kEarlySizeMinMs));
        REQUIRE(kSpecs[10].mx == Approx(CavernVerb::kDefaultMaxEarlySeconds * 1000.0));
    }

    SECTION("UnregisteredInBandIgnored") {
        SpaceParams p;
        setNonDefault(p);
        const Snapshot before = snapshot(p);
        for (ParamID id = Vorago::kSpaceFreezeId + 1; id < Vorago::kSpaceParamRangeEnd; ++id) {
            for (double v : {0.0, 0.5, 1.0}) {
                Vorago::handleSpaceParamChange(p, id, v);
            }
        }
        REQUIRE(snapshot(p) == before);
    }

    SECTION("SaveLoadRoundTrip") {
        SpaceParams src;
        setNonDefault(src);
        const Snapshot expected = snapshot(src);
        REQUIRE(expected != snapshot(SpaceParams{}));

        const auto bytes = savedNonDefaultBytes();
        REQUIRE(bytes.size() == kStreamBytes);

        // Ascending ID order: float i at offset 4 i, freeze int32 at 60.
        for (std::size_t i = 0; i < kNumFloats; ++i)
            REQUIRE(leWordAt(bytes, 4 * i) == expected[i]);
        REQUIRE(leWordAt(bytes, 60) == 1u);

        auto s = streamFromBytes(bytes, bytes.size());
        SpaceParams dst;
        Steinberg::IBStreamer r(s, kLittleEndian);
        REQUIRE(Vorago::loadSpaceParams(dst, r));
        REQUIRE(snapshot(dst) == expected);
    }

    SECTION("TruncationEveryOffset") {
        const auto bytes = savedNonDefaultBytes();
        REQUIRE(bytes.size() == kStreamBytes);
        SpaceParams saved;
        setNonDefault(saved);
        const Snapshot savedBits = snapshot(saved);
        SpaceParams d;
        dirty(d);
        const Snapshot dirtyBits = snapshot(d);

        for (std::size_t c = 0; c < kStreamBytes; ++c) {
            INFO("cut " << c);
            auto s = streamFromBytes(bytes, c);
            SpaceParams p;
            dirty(p);
            Steinberg::IBStreamer r(s, kLittleEndian);
            REQUIRE_FALSE(Vorago::loadSpaceParams(p, r));
            const Snapshot got = snapshot(p);
            for (std::size_t i = 0; i < kNumFloats; ++i) {
                const bool contained = 4 * (i + 1) <= c;
                REQUIRE(got[i] == (contained ? savedBits[i] : dirtyBits[i]));
            }
            REQUIRE(got[kNumFloats] == dirtyBits[kNumFloats]);  // freeze never fully read
        }
    }

    SECTION("NonFiniteAndClamp") {
        const auto base = nonDefaultPlains();
        const std::array<std::uint32_t, 3> badBits = {0x7FC00000u, 0x7F800000u, 0xFF800000u};

        for (std::size_t k = 0; k < kNumFloats; ++k) {
            for (std::uint32_t bad : badBits) {
                INFO("field " << k << " bits " << bad);
                auto floats = base;
                floats[k] = floatFromBits(bad);
                auto s = handStream(floats, 1);
                SpaceParams p;
                dirty(p);
                SpaceParams d;
                dirty(d);
                const Snapshot dirtyBits = snapshot(d);
                Steinberg::IBStreamer r(s, kLittleEndian);
                REQUIRE(Vorago::loadSpaceParams(p, r));
                const Snapshot got = snapshot(p);
                for (std::size_t i = 0; i < kNumFloats; ++i)
                    REQUIRE(got[i] == (i == k ? dirtyBits[i] : bitsOf(base[i])));
                REQUIRE(got[kNumFloats] == 1u);
            }
        }

        // Finite out-of-range floats clamp to the plain range ends.
        for (std::size_t k = 0; k < kNumFloats; ++k) {
            const auto& spec = kSpecs[k];
            INFO("field " << k);
            for (const auto& [raw, want] :
                 {std::pair<float, double>{static_cast<float>(spec.mn - 1000.0), spec.mn},
                  std::pair<float, double>{static_cast<float>(spec.mx + 1000.0), spec.mx}}) {
                auto floats = base;
                floats[k] = raw;
                auto s = handStream(floats, 0);
                SpaceParams p;
                Steinberg::IBStreamer r(s, kLittleEndian);
                REQUIRE(Vorago::loadSpaceParams(p, r));
                REQUIRE((p.*(spec.field)).load() == static_cast<float>(want));
            }
        }

        // Index clamps: -1 -> 0, 2 -> 1, 99 -> 1.
        for (const auto& [raw, want] : {std::pair<Steinberg::int32, int>{-1, 0},
                                        std::pair<Steinberg::int32, int>{2, 1},
                                        std::pair<Steinberg::int32, int>{99, 1}}) {
            auto s = handStream(base, raw);
            SpaceParams p;
            Steinberg::IBStreamer r(s, kLittleEndian);
            REQUIRE(Vorago::loadSpaceParams(p, r));
            REQUIRE(p.freeze.load() == want);
        }
    }

    SECTION("ControllerMirror") {
        const auto bytes = savedNonDefaultBytes();
        const auto plains = nonDefaultPlains();
        auto s = streamFromBytes(bytes, bytes.size());
        Steinberg::IBStreamer r(s, kLittleEndian);

        std::vector<std::pair<ParamID, double>> calls;
        Vorago::loadSpaceParamsToController(
            r, [&](ParamID id, double n) { calls.emplace_back(id, n); });

        REQUIRE(calls.size() == kNumFloats + 1);
        for (std::size_t i = 0; i < kNumFloats; ++i) {
            INFO("id " << kSpecs[i].id);
            REQUIRE(calls[i].first == kSpecs[i].id);
            REQUIRE(calls[i].second ==
                    Approx(specToNormalized(kSpecs[i], static_cast<double>(plains[i])))
                        .margin(1e-9));
        }
        REQUIRE(calls[kNumFloats].first == Vorago::kSpaceFreezeId);
        REQUIRE(calls[kNumFloats].second == Approx(1.0).margin(1e-9));
    }

    SECTION("FormatNonEmpty") {
        for (const auto& spec : kSpecs) {
            for (double v : {0.0, 0.5, 1.0}) {
                INFO("id " << spec.id << " v " << v);
                String128 str{};
                REQUIRE(Vorago::formatSpaceParam(spec.id, v, str) == kResultOk);
                REQUIRE_FALSE(toAsciiString(str).empty());
            }
        }
        String128 sFreeze{};
        REQUIRE(Vorago::formatSpaceParam(Vorago::kSpaceFreezeId, 1.0, sFreeze) == kResultFalse);
        String128 sUnknown{};
        REQUIRE(Vorago::formatSpaceParam(Vorago::kSpaceFreezeId + 1, 0.5, sUnknown) ==
                kResultFalse);
    }
}
