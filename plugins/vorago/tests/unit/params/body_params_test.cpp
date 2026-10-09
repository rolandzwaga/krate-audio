// ==============================================================================
// Vorago Phase 12 - body parameter pack contract (T024)
// ==============================================================================
// Shared pack contract (specs/vorago-phase12-parameters/tasks.md Group 7), N = 6,
// B = 24 (4 float + 2 int32), IDs 1000-1005, plus the SC-018 Q5 assertion:
// material list index n == ContinuousBody::BodyMaterial enum value n.
//
// ODR note: Vorago::BodyParams is a near-name of Seraphis::BodyParams
// (plugins/seraphis/src/parameters/body_params.h:108); different namespaces, and
// no TU may `using namespace` both.
//
// Built with -fno-fast-math (tests/CMakeLists.txt): NaN/Inf payloads are produced
// from bit patterns through a volatile uint32 + memcpy.
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "parameters/body_params.h"
#include "parameters/param_mapping.h"
#include "plugin_ids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <krate/dsp/systems/continuous_body.h>

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {

using Steinberg::Vst::ParamID;
using Steinberg::Vst::ParameterInfo;
using BodyMaterial = Krate::DSP::ContinuousBody::BodyMaterial;

constexpr int kN = 6;
constexpr std::size_t kB = 24;

constexpr std::array<ParamID, 4> kFloatIds = {Vorago::kBodyBlendId, Vorago::kBodyDampingId,
                                              Vorago::kBodyResonanceId, Vorago::kBodyMixId};
constexpr std::array<double, 4> kFloatDefaults = {0.35, 0.25, 0.70, 1.00};
constexpr std::array<ParamID, 2> kIndexIds = {Vorago::kBodyMaterialAId,
                                              Vorago::kBodyMaterialBId};
constexpr std::array<int, 2> kIndexDefaults = {5, 6};
constexpr int kMaterials = 11;

Steinberg::IPtr<Steinberg::MemoryStream> makeStream() {
    return Steinberg::owned(new Steinberg::MemoryStream());
}

void rewindStream(Steinberg::MemoryStream& s) {
    REQUIRE(s.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr) == Steinberg::kResultOk);
}

std::uint32_t bitsOf(float f) { return std::bit_cast<std::uint32_t>(f); }

float floatFromBits(std::uint32_t bits) {
    volatile std::uint32_t v = bits;
    const std::uint32_t copy = v;
    float f = 0.0f;
    std::memcpy(&f, &copy, sizeof(f));
    return f;
}

std::string ascii(const Steinberg::Vst::TChar* s) {
    Steinberg::Vst::String128 buf{};
    for (int i = 0; i < 127 && s[i] != 0; ++i)
        buf[i] = s[i];
    char out[256] = {};
    Steinberg::UString(buf, 128).toAscii(out, 256);
    return {out};
}

std::atomic<float>& floatField(Vorago::BodyParams& p, std::size_t k) {
    switch (k) {
        case 0: return p.blend;
        case 1: return p.damping;
        case 2: return p.resonance;
        default: return p.mix;
    }
}

std::atomic<int>& indexField(Vorago::BodyParams& p, std::size_t k) {
    return (k == 0) ? p.materialA : p.materialB;
}

float getF(Vorago::BodyParams& p, std::size_t k) {
    return floatField(p, k).load(std::memory_order_relaxed);
}

int getI(Vorago::BodyParams& p, std::size_t k) {
    return indexField(p, k).load(std::memory_order_relaxed);
}

// Non-default in-range values for SECTION 4; distinct "dirty" values for SECTION 5.
constexpr std::array<float, 4> kSavedF = {0.8f, 0.6f, 0.1f, 0.4f};
constexpr std::array<int, 2> kSavedI = {9, 2};
constexpr std::array<float, 4> kDirtyF = {0.05f, 0.95f, 0.55f, 0.15f};
constexpr std::array<int, 2> kDirtyI = {1, 10};

void setAll(Vorago::BodyParams& p, const std::array<float, 4>& f, const std::array<int, 2>& i) {
    for (std::size_t k = 0; k < 4; ++k)
        floatField(p, k).store(f[k], std::memory_order_relaxed);
    for (std::size_t k = 0; k < 2; ++k)
        indexField(p, k).store(i[k], std::memory_order_relaxed);
}

std::array<std::uint32_t, 6> snapshot(Vorago::BodyParams& p) {
    std::array<std::uint32_t, 6> s{};
    for (std::size_t k = 0; k < 4; ++k)
        s[k] = bitsOf(getF(p, k));
    for (std::size_t k = 0; k < 2; ++k)
        s[4 + k] = static_cast<std::uint32_t>(getI(p, k));
    return s;
}

Steinberg::IPtr<Steinberg::MemoryStream> savedStream() {
    Vorago::BodyParams p;
    setAll(p, kSavedF, kSavedI);
    auto s = makeStream();
    Steinberg::IBStreamer w(s, kLittleEndian);
    Vorago::saveBodyParams(p, w);
    return s;
}

}  // namespace

TEST_CASE("Vorago_BodyParamsContract", "[vorago][params]") {
    // Header-level pins (plan section 3.4 / Q5): list index == enum value.
    static_assert(Krate::DSP::ContinuousBody::kNumMaterials == 11);
    static_assert(static_cast<int>(BodyMaterial::StoneChamber) == 5);
    static_assert(static_cast<int>(BodyMaterial::SteelTank) == 6);

    SECTION("Registration") {
        Steinberg::Vst::ParameterContainer pc;
        Vorago::registerBodyParams(pc);
        REQUIRE(pc.getParameterCount() == kN);

        const std::array<const char*, 4> titles = {"Body Blend", "Body Damping", "Body Resonance",
                                                   "Body Mix"};
        for (std::size_t k = 0; k < 4; ++k) {
            auto* param = pc.getParameter(kFloatIds[k]);
            REQUIRE(param != nullptr);
            const auto& info = param->getInfo();
            CHECK(ascii(info.title) == titles[k]);
            CHECK(ascii(info.units) == "%");
            CHECK(info.stepCount == 0);
            CHECK(info.flags == ParameterInfo::kCanAutomate);
            CHECK(info.defaultNormalizedValue == Catch::Approx(kFloatDefaults[k]).margin(1e-9));
        }

        const std::array<const char*, 2> listTitles = {"Body Material A", "Body Material B"};
        const std::array<const char*, kMaterials> labels = {
            "Glass",         "Strings",   "Metal Plate", "Chamber",          "Ice",
            "Stone Chamber", "Steel Tank", "Wooden Hull", "Cathedral Column", "Cavern Wall",
            "Glass Sphere"};
        for (std::size_t k = 0; k < 2; ++k) {
            auto* param = pc.getParameter(kIndexIds[k]);
            REQUIRE(param != nullptr);
            const auto& info = param->getInfo();
            CHECK(ascii(info.title) == listTitles[k]);
            CHECK(ascii(info.units).empty());
            CHECK(info.stepCount == kMaterials - 1);
            CHECK(info.flags == (ParameterInfo::kCanAutomate | ParameterInfo::kIsList));
            CHECK(info.defaultNormalizedValue ==
                  Catch::Approx(kIndexDefaults[k] / 10.0).margin(1e-9));
            for (int i = 0; i < kMaterials; ++i) {
                Steinberg::Vst::String128 str{};
                param->toString(Vorago::indexToNormalized(i, kMaterials), str);
                CHECK(ascii(str) == labels[static_cast<std::size_t>(i)]);
            }
        }
    }

    SECTION("DefaultsDenormalizeToPlain") {
        Vorago::BodyParams fresh;
        for (std::size_t k = 0; k < 4; ++k) {
            CHECK(getF(fresh, k) == Catch::Approx(kFloatDefaults[k]).epsilon(1e-6));
            Vorago::BodyParams p;
            floatField(p, k).store(-1.0f);
            Vorago::handleBodyParamChange(p, kFloatIds[k], kFloatDefaults[k]);
            CHECK(getF(p, k) == Catch::Approx(kFloatDefaults[k]).epsilon(1e-6));
            Vorago::handleBodyParamChange(p, kFloatIds[k], 0.0);
            CHECK(getF(p, k) == 0.0f);
            Vorago::handleBodyParamChange(p, kFloatIds[k], 1.0);
            CHECK(getF(p, k) == 1.0f);
        }
        for (std::size_t k = 0; k < 2; ++k) {
            CHECK(getI(fresh, k) == kIndexDefaults[k]);
            Vorago::BodyParams p;
            indexField(p, k).store(-1);
            Vorago::handleBodyParamChange(p, kIndexIds[k], kIndexDefaults[k] / 10.0);
            CHECK(getI(p, k) == kIndexDefaults[k]);
            Vorago::handleBodyParamChange(p, kIndexIds[k], 0.0);
            CHECK(getI(p, k) == 0);
            Vorago::handleBodyParamChange(p, kIndexIds[k], 1.0);
            CHECK(getI(p, k) == kMaterials - 1);
        }
        CHECK(static_cast<BodyMaterial>(getI(fresh, 0)) == BodyMaterial::StoneChamber);
        CHECK(static_cast<BodyMaterial>(getI(fresh, 1)) == BodyMaterial::SteelTank);

        // SC-018 Q5: index n <-> BodyMaterial n, no mapping table.
        for (int n = 0; n < kMaterials; ++n) {
            Vorago::BodyParams p;
            Vorago::handleBodyParamChange(p, Vorago::kBodyMaterialAId, n / 10.0);
            REQUIRE(p.materialA.load() == n);
            const auto m = static_cast<BodyMaterial>(p.materialA.load());
            CHECK(static_cast<int>(m) == n);
            Vorago::handleBodyParamChange(p, Vorago::kBodyMaterialBId, n / 10.0);
            CHECK(p.materialB.load() == n);
        }
    }

    SECTION("UnregisteredInBandIgnored") {
        Vorago::BodyParams p;
        setAll(p, kSavedF, kSavedI);
        const auto before = snapshot(p);
        for (ParamID id : {ParamID{1006}, ParamID{1007}, ParamID{1050}, ParamID{1098},
                           ParamID{1099}}) {
            for (double v : {0.0, 0.3, 1.0}) {
                Vorago::handleBodyParamChange(p, id, v);
                CHECK(snapshot(p) == before);
            }
        }
    }

    SECTION("SaveLoadRoundTrip") {
        auto s = savedStream();
        REQUIRE(std::cmp_equal(s->getSize(), kB));
        rewindStream(*s);
        Vorago::BodyParams loaded;
        Steinberg::IBStreamer r(s, kLittleEndian);
        REQUIRE(Vorago::loadBodyParams(loaded, r));
        Vorago::BodyParams expected;
        setAll(expected, kSavedF, kSavedI);
        CHECK(snapshot(loaded) == snapshot(expected));
    }

    SECTION("TruncationEveryOffset") {
        auto full = savedStream();
        REQUIRE(std::cmp_equal(full->getSize(), kB));
        char* data = full->getData();
        for (std::size_t c = 0; c < kB; ++c) {
            auto cut = makeStream();
            if (c > 0) {
                Steinberg::int32 written = 0;
                REQUIRE(cut->write(data, static_cast<Steinberg::int32>(c), &written) ==
                        Steinberg::kResultOk);
                REQUIRE(std::cmp_equal(written, c));
            }
            rewindStream(*cut);
            Vorago::BodyParams p;
            setAll(p, kDirtyF, kDirtyI);
            Steinberg::IBStreamer r(cut, kLittleEndian);
            CHECK_FALSE(Vorago::loadBodyParams(p, r));
            for (std::size_t k = 0; k < 4; ++k) {
                const bool restored = (k + 1) * 4 <= c;
                CHECK(bitsOf(getF(p, k)) == bitsOf(restored ? kSavedF[k] : kDirtyF[k]));
            }
            for (std::size_t k = 0; k < 2; ++k) {
                const bool restored = (4 + k + 1) * 4 <= c;
                CHECK(getI(p, k) == (restored ? kSavedI[k] : kDirtyI[k]));
            }
        }
    }

    SECTION("NonFiniteAndClamp") {
        const std::array<std::uint32_t, 3> badBits = {0x7FC00000u, 0x7F800000u, 0xFF800000u};
        for (std::size_t bad = 0; bad < 4; ++bad) {
            for (std::uint32_t bits : badBits) {
                auto s = makeStream();
                {
                    Steinberg::IBStreamer w(s, kLittleEndian);
                    for (std::size_t k = 0; k < 4; ++k)
                        w.writeFloat(k == bad ? floatFromBits(bits) : kSavedF[k]);
                    for (std::size_t k = 0; k < 2; ++k)
                        w.writeInt32(kSavedI[k]);
                }
                rewindStream(*s);
                Vorago::BodyParams p;
                setAll(p, kDirtyF, kDirtyI);
                Steinberg::IBStreamer r(s, kLittleEndian);
                CHECK(Vorago::loadBodyParams(p, r));
                for (std::size_t k = 0; k < 4; ++k)
                    CHECK(bitsOf(getF(p, k)) == bitsOf(k == bad ? kDirtyF[k] : kSavedF[k]));
                for (std::size_t k = 0; k < 2; ++k)
                    CHECK(getI(p, k) == kSavedI[k]);
            }
        }

        // Finite out-of-range floats clamp; indices -1 / 11 clamp to 0 / 10.
        for (const auto& [f, i, expF, expI] :
             {std::tuple{-0.5f, -1, 0.0f, 0}, std::tuple{1.7f, kMaterials, 1.0f, kMaterials - 1},
              std::tuple{-1.0e30f, -1000, 0.0f, 0},
              std::tuple{1.0e30f, 1000, 1.0f, kMaterials - 1}}) {
            auto s = makeStream();
            {
                Steinberg::IBStreamer w(s, kLittleEndian);
                for (std::size_t k = 0; k < 4; ++k)
                    w.writeFloat(f);
                for (std::size_t k = 0; k < 2; ++k)
                    w.writeInt32(i);
            }
            rewindStream(*s);
            Vorago::BodyParams p;
            Steinberg::IBStreamer r(s, kLittleEndian);
            CHECK(Vorago::loadBodyParams(p, r));
            for (std::size_t k = 0; k < 4; ++k)
                CHECK(getF(p, k) == expF);
            for (std::size_t k = 0; k < 2; ++k)
                CHECK(getI(p, k) == expI);
        }
    }

    SECTION("ControllerMirror") {
        auto s = savedStream();
        rewindStream(*s);
        std::vector<std::pair<ParamID, double>> got;
        Steinberg::IBStreamer r(s, kLittleEndian);
        Vorago::loadBodyParamsToController(
            r, [&got](ParamID id, double v) { got.emplace_back(id, v); });
        REQUIRE(got.size() == static_cast<std::size_t>(kN));
        for (std::size_t k = 0; k < 4; ++k) {
            CHECK(got[k].first == kFloatIds[k]);
            CHECK(got[k].second == Catch::Approx(Vorago::linearToNormalized(
                                                     static_cast<double>(kSavedF[k]), 0.0, 1.0))
                                       .margin(1e-9));
        }
        for (std::size_t k = 0; k < 2; ++k) {
            CHECK(got[4 + k].first == kIndexIds[k]);
            CHECK(got[4 + k].second ==
                  Catch::Approx(Vorago::indexToNormalized(kSavedI[k], kMaterials)).margin(1e-9));
        }
    }

    SECTION("FormatNonEmpty") {
        for (ParamID id : kFloatIds) {
            for (double v : {0.0, 0.5, 1.0}) {
                Steinberg::Vst::String128 str{};
                CHECK(Vorago::formatBodyParam(id, v, str) == Steinberg::kResultOk);
                CHECK_FALSE(ascii(str).empty());
            }
        }
        Steinberg::Vst::String128 str{};
        CHECK(Vorago::formatBodyParam(Vorago::kBodyMaterialAId, 0.5, str) == Steinberg::kResultFalse);
        CHECK(Vorago::formatBodyParam(ParamID{1050}, 0.5, str) == Steinberg::kResultFalse);
    }
}
