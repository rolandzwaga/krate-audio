// ==============================================================================
// Layer 3: System Tests - ProfundumCore excitation, non-finite TU
//                                    (Profundum Phase 2, specs/profundum-phase2-edge-fold)
// ==============================================================================
// Reference: specs/profundum-phase2-edge-fold/plan.md (S5.3, S8.1, S8.2)
//            specs/profundum-phase2-edge-fold/tasks.md (T006 wires this TU,
//                                                       T011 fills it)
//
// SCOPE OF THIS TU (plan S8.1): SC-016 excitation (ExcitationNonFinite) and
//   ProfundumCore_ExcitationTableLookupAtCorners (S5.3). Listed in the
//   -fno-fast-math block of dsp/tests/CMakeLists.txt: it injects NaN/Inf by
//   bit pattern and uses NaN table sentinels.
// ==============================================================================

#include "profundum_excitation_test_helpers.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/processors/spectral_shape_recipe.h>
#include <krate/dsp/systems/profundum_core.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ios>
#include <memory>

using Krate::DSP::ProfundumCore;
using Krate::DSP::SpectralShapeRecipe;
namespace PT = Krate::DSP::ProfundumTest;
namespace KD = Krate::DSP::detail;

namespace {

using Excitation = ProfundumCore::ExcitationControls;

constexpr double kFs = 48000.0;
constexpr float kC2 = 65.41f;

/// One ExcitationControls field, by name.
struct ExcitationField {
    const char* name;
    float Excitation::*field;
};

constexpr std::array<ExcitationField, 3> kExcitationFields{{
    {"mix", &Excitation::mix},
    {"drive", &Excitation::drive},
    {"asymmetry", &Excitation::asymmetry},
}};

/// Non-finite values, built from their bit patterns.
constexpr std::array<std::uint32_t, 3> kNonFiniteBits{0x7FC00000u, 0x7F800000u, 0xFF800000u};

bool sameBits(float a, float b) noexcept { return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b); }

constexpr Excitation kValid{.mix = 0.25f, .drive = 0.75f, .asymmetry = 0.375f};
constexpr Excitation kMax{.mix = 1.0f, .drive = 1.0f, .asymmetry = 1.0f};

}  // namespace

TEST_CASE("ProfundumCore_ExcitationNonFinite", "[systems][profundum][edge-fold]") {
    SECTION("setExcitation before prepare is a no-op") {
        auto core = std::make_unique<ProfundumCore>();
        core->setExcitation(kMax);
        const Excitation e = core->excitation();
        REQUIRE(e.mix == ProfundumCore::kDefaultExcitation.mix);
        REQUIRE(e.drive == ProfundumCore::kDefaultExcitation.drive);
        REQUIRE(e.asymmetry == ProfundumCore::kDefaultExcitation.asymmetry);
    }

    auto core = std::make_unique<ProfundumCore>();
    core->prepare(kFs);

    SECTION("NaN / +Inf / -Inf keep the previous field") {
        for (const ExcitationField& f : kExcitationFields) {
            for (const std::uint32_t bits : kNonFiniteBits) {
                const float bad = std::bit_cast<float>(bits);
                INFO("field " << f.name << " bits 0x" << std::hex << bits);
                REQUIRE_FALSE(KD::isFinite(bad));
                core->setExcitation(kValid);
                Excitation e = kValid;
                e.*f.field = bad;
                core->setExcitation(e);
                const Excitation got = core->excitation();
                REQUIRE(sameBits(got.*f.field, kValid.*f.field));
                for (const ExcitationField& other : kExcitationFields)
                    REQUIRE(sameBits(got.*other.field, kValid.*other.field));
            }
        }
    }

    SECTION("denormals read back exactly 0.0f") {
        for (const ExcitationField& f : kExcitationFields) {
            for (const float denormal : {std::bit_cast<float>(0x00000001u), 1e-40f}) {
                INFO("field " << f.name << " value " << denormal);
                REQUIRE((std::bit_cast<std::uint32_t>(denormal) & 0x7F800000u) == 0u);
                core->setExcitation(kValid);
                Excitation e = kValid;
                e.*f.field = denormal;
                core->setExcitation(e);
                REQUIRE(sameBits(core->excitation().*f.field, 0.0f));
            }
        }
    }

    SECTION("max-excitation render with a NaN field stays finite and bounded") {
        Excitation e = kMax;
        core->setExcitation(e);
        e.drive = std::bit_cast<float>(0x7FC00000u);
        const PT::Render r = PT::renderExcited(*core, SpectralShapeRecipe::kSawAnchor, e, kC2, 0.5);
        REQUIRE(!r.L.empty());
        for (std::size_t i = 0; i < r.L.size(); ++i) {
            if (!KD::isFinite(r.L[i]) || !KD::isFinite(r.R[i])
                || std::abs(r.L[i]) > ProfundumCore::kExcitationCeiling
                || std::abs(r.R[i]) > ProfundumCore::kExcitationCeiling) {
                INFO("sample " << i << " L " << r.L[i] << " R " << r.R[i]);
                REQUIRE(false);
            }
        }
        REQUIRE(core->excitation().drive == 1.0f);
        REQUIRE(core->stateFinite());
    }
}

TEST_CASE("ProfundumCore_ExcitationTableLookupAtCorners", "[systems][profundum][edge-fold]") {
    // Far-corner indices clamp so the (i + 1, j + 1) reads stay inside the table (S5.3).
    static_assert(KD::excitationDriveIndex(48.0f) == 47 && KD::excitationBiasIndex(8.0f) == 7);
    static_assert(KD::excitationDriveIndex(0.0f) == 0 && KD::excitationBiasIndex(0.0f) == 0);

    constexpr std::size_t kRows = KD::kExcitationTableDrivePoints;
    constexpr std::size_t kCols = KD::kExcitationTableBiasPoints;
    static_assert(kRows == 49 && kCols == 9);

    // NaN sentinel directly after the table: any out-of-range read would poison the result.
    struct Sentinelled {
        std::array<float, 49 * 9> t;
        float sentinel;
    };
    Sentinelled s{};
    for (std::size_t i = 0; i < kRows; ++i) {
        for (std::size_t j = 0; j < kCols; ++j)
            s.t[i * kCols + j] = 1.0f + static_cast<float>(i) + 0.01f * static_cast<float>(j);
    }
    s.sentinel = std::bit_cast<float>(0x7FC00000u);

    struct Corner {
        float l2;
        float b;
        std::size_t i;
        std::size_t j;
    };
    constexpr std::array<Corner, 4> kCorners{{
        {-4.0f, 0.0f, 0, 0},
        {-4.0f, 0.5f, 0, 8},
        {2.0f, 0.0f, 48, 0},
        {2.0f, 0.5f, 48, 8},
    }};
    for (const Corner& c : kCorners) {
        INFO("log2 D " << c.l2 << " b " << c.b);
        const float v = KD::bilinearExcitationTable(s.t, c.l2, c.b);
        REQUIRE(KD::isFinite(v));
        REQUIRE(sameBits(v, s.t[c.i * kCols + c.j]));
    }
    // Beyond the corners the lookup clamps to them.
    REQUIRE(sameBits(KD::bilinearExcitationTable(s.t, 5.0f, 2.0f), s.t[48 * kCols + 8]));
    REQUIRE(sameBits(KD::bilinearExcitationTable(s.t, -9.0f, -1.0f), s.t[0]));
}
