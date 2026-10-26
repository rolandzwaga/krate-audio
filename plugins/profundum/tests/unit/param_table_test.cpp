#include <catch2/catch_test_macros.hpp>
#include "profundum_test_fixture.h"

// Profundum_SoundParamTableRouting, Profundum_ParamTableConformance, Profundum_ParamDisplayFormats, Profundum_PortamentoLabelFollowsGlideMode — written by T013, T014 (specs/profundum-phase11-parameters/tasks.md)

#include "controller/controller.h"
#include "parameters/morph_params.h"
#include "parameters/note_params.h"
#include "parameters/performance_params.h"
#include "parameters/sound_param_table.h"
#include "plugin_ids.h"

#include "base/source/fobject.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

#include <krate/dsp/core/note_value.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace P = ::Profundum;

/// The leaf indices whose word in `after` differs from the engine default.
std::vector<std::size_t> changedLeaves(const P::LeafBits& after) {
    std::vector<std::size_t> changed;
    for (std::size_t i = 0; i < P::kNumLeaves; ++i) {
        if (after[i] != P::kDefaultLeafBits[i]) {
            changed.push_back(i);
        }
    }
    return changed;
}

}  // namespace

// T013 (deviation 3): slot-level routing of all 60 sound rows and the Sync Rate pair writes (FR-001, FR-002,
// FR-010, FR-012, FR-054; plan 4.3).
TEST_CASE("Profundum_SoundParamTableRouting", "[profundum][params]") {
    SECTION("SixtyRowsCoverAllLeaves") {
        REQUIRE(P::kSoundRows.size() == 60);
        std::array<int, P::kNumLeaves> hits{};
        for (const auto& row : P::kSoundRows) {
            REQUIRE(static_cast<std::size_t>(row.leaf) < P::kNumLeaves);
            ++hits[row.leaf];
            if (row.leaf2 != P::kNoLeaf) {
                REQUIRE(static_cast<std::size_t>(row.leaf2) < P::kNumLeaves);
                ++hits[row.leaf2];
            }
        }
        for (std::size_t leaf = 0; leaf < P::kNumLeaves; ++leaf) {
            INFO("leaf " << leaf);
            CHECK(hits[leaf] == 1);
        }
    }

    SECTION("RouteTable") {
        int routed = 0;
        std::set<Steinberg::Vst::ParamID> noteBand;
        for (std::size_t id = 0; id < P::kParamRoutes.size(); ++id) {
            if (P::kParamRoutes[id].kind == P::RouteKind::None) {
                continue;
            }
            ++routed;
            if (id >= 400 && id < 500) {
                noteBand.insert(static_cast<Steinberg::Vst::ParamID>(id));
            }
        }
        CHECK(routed == 84);

        std::set<Steinberg::Vst::ParamID> wantNoteBand;
        for (Steinberg::Vst::ParamID id = 400; id <= 403; ++id) {
            wantNoteBand.insert(id);
        }
        for (Steinberg::Vst::ParamID id = 410; id <= 417; ++id) {
            wantNoteBand.insert(id);
        }
        for (Steinberg::Vst::ParamID id = 420; id <= 426; ++id) {
            wantNoteBand.insert(id);
        }
        CHECK(noteBand == wantNoteBand);  // FR-054

        const auto kindOf = [](Steinberg::Vst::ParamID id) { return P::kParamRoutes[id].kind; };
        for (Steinberg::Vst::ParamID id = 0; id <= 3; ++id) {
            INFO("id " << id);
            CHECK(kindOf(id) == P::RouteKind::Global);
        }
        for (Steinberg::Vst::ParamID id = 100; id <= 103; ++id) {
            INFO("id " << id);
            CHECK(kindOf(id) == P::RouteKind::Macro);
        }
        for (Steinberg::Vst::ParamID id = 4; id <= 7; ++id) {
            INFO("id " << id);
            CHECK(kindOf(id) == P::RouteKind::Performance);
        }
        CHECK(kindOf(104) == P::RouteKind::Sound);
        CHECK(kindOf(105) == P::RouteKind::MorphControl);
        for (Steinberg::Vst::ParamID id = 1000; id <= 1002; ++id) {
            INFO("id " << id);
            CHECK(kindOf(id) == P::RouteKind::MorphControl);
        }
        for (Steinberg::Vst::ParamID id = 410; id <= 417; ++id) {
            INFO("id " << id);
            CHECK(kindOf(id) == P::RouteKind::Note);
            CHECK(P::soundRowOf(id) == nullptr);
        }

        // Every sound row is reachable through soundRowOf and names itself.
        for (const auto& row : P::kSoundRows) {
            INFO("id " << row.id);
            const P::SoundParamRow* found = P::soundRowOf(row.id);
            REQUIRE(found != nullptr);
            CHECK(found->id == row.id);
            CHECK(found->leaf == row.leaf);
        }

        CHECK(P::soundRowOf(9999) == nullptr);
        CHECK(P::soundRowOf(1003) == nullptr);
    }

    SECTION("EveryRowWritesItsLeafOnly") {
        for (const auto& row : P::kSoundRows) {
            INFO("id " << row.id << " leaf " << static_cast<int>(row.leaf));
            const bool syncPair = row.leaf2 != P::kNoLeaf;
            // Drive to hi; a non-pair row whose hi word equals its default (none expected) is driven to lo instead.
            double v = 1.0;
            if (!syncPair && P::leafWordFromNormalized(row.leaf, 1.0) == P::kDefaultLeafBits[row.leaf]) {
                v = 0.0;
            }

            P::EndpointParams ep;
            ep.dirty.store(false);
            ep.syncPending.store(false);
            REQUIRE(P::handleSoundParamChange(ep, row.id, v));
            CHECK(ep.dirty.load());
            CHECK(ep.syncPending.load());

            const std::vector<std::size_t> changed = changedLeaves(ep.load());
            if (syncPair) {
                REQUIRE(changed.size() == 2);
                CHECK(changed[0] == static_cast<std::size_t>(row.leaf));
                CHECK(changed[1] == static_cast<std::size_t>(row.leaf2));
            } else {
                REQUIRE(changed.size() == 1);
                CHECK(changed[0] == static_cast<std::size_t>(row.leaf));
                CHECK(ep.leaf[row.leaf].load() == P::leafWordFromNormalized(row.leaf, v));
            }
        }
    }

    SECTION("SyncRateAll30") {
        for (const Steinberg::Vst::ParamID id : {static_cast<Steinberg::Vst::ParamID>(P::kFieldSyncRateId),
                                                  static_cast<Steinberg::Vst::ParamID>(P::kMotionCycleSyncRateId)}) {
            const P::SoundParamRow* row = P::soundRowOf(id);
            REQUIRE(row != nullptr);
            REQUIRE(row->leaf2 != P::kNoLeaf);
            for (int idx = 0; idx < Krate::DSP::kNoteValueDropdownCount; ++idx) {
                INFO("id " << id << " idx " << idx);
                P::EndpointParams ep;
                REQUIRE(P::handleSoundParamChange(ep, id, static_cast<double>(idx) / 29.0));
                const auto& m = Krate::DSP::kNoteValueDropdownMapping[static_cast<std::size_t>(idx)];
                CHECK(ep.leaf[row->leaf].load() == static_cast<std::uint32_t>(static_cast<std::uint8_t>(m.note)));
                CHECK(ep.leaf[row->leaf2].load() ==
                      static_cast<std::uint32_t>(static_cast<std::uint8_t>(m.modifier)));
                CHECK(P::syncRateIndexOf(ep.leaf[row->leaf].load(), ep.leaf[row->leaf2].load()) == idx);
            }
        }
    }

    SECTION("UnroutedIgnored") {
        P::EndpointParams ep;
        ep.dirty.store(false);
        ep.syncPending.store(false);
        CHECK_FALSE(P::handleSoundParamChange(ep, 999, 0.5));
        CHECK(ep.load() == P::kDefaultLeafBits);
        CHECK_FALSE(ep.dirty.load());
        CHECK_FALSE(ep.syncPending.load());
    }
}

// ==============================================================================
// T014 - controller registration and display (FR-001 - FR-009; SC-005, SC-016, FR-006 strings)
// ==============================================================================

namespace {

using Steinberg::int32;
using Steinberg::Vst::ParamID;
using PInfo = Steinberg::Vst::ParameterInfo;

constexpr int32 kFlagA = PInfo::kCanAutomate;                      // continuous / range
constexpr int32 kFlagL = PInfo::kCanAutomate | PInfo::kIsList;     // automatable list
constexpr int32 kFlagAH = PInfo::kCanAutomate | PInfo::kIsHidden;  // Pitch Bend, Sustain Pedal
constexpr int32 kFlagH = PInfo::kIsHidden;                         // Mod Wheel, Channel Pressure
constexpr int32 kFlagListOnly = PInfo::kIsList;                    // Edit Target (FR-004)

constexpr int kNoLeafRow = -1;

/// One registered parameter as SC-005 pins it. leaf >= 0 marks an E row whose default is derived from
/// MorphState{} (expectedLeafDefault); otherwise `def` is the exact default.
struct ExpectedParam {
    ParamID id;
    const char* title;
    const char* units;
    int32 stepCount;
    int32 flags;
    double def;
    int leaf;
};

/// SC-005: the explicit 84-row table (titles from the spec ID table, FR-009 names).
std::vector<ExpectedParam> expectedParams() {
    const double rampDefault = P::normalizedFromPlain(P::PlainLaw{.lo = 5.0, .hi = 2000.0, .logOffset = 0.0, .logarithmic = true}, 40.0);
    return {
        // ---- global 0-7 (0-3 unchanged from Phase 10) ----
        {.id = 0, .title = "Output Gain", .units = "dB", .stepCount = 0, .flags = kFlagA, .def = 1.0, .leaf = kNoLeafRow},
        {.id = 1, .title = "Voice Mode", .units = "", .stepCount = 1, .flags = kFlagL, .def = 0.0, .leaf = kNoLeafRow},
        {.id = 2, .title = "Polyphony", .units = "", .stepCount = 7, .flags = kFlagL, .def = 3.0 / 7.0, .leaf = kNoLeafRow},
        {.id = 3, .title = "Output Ceiling", .units = "dBTP", .stepCount = 0, .flags = kFlagA, .def = 11.0 / 12.0, .leaf = kNoLeafRow},
        {.id = 4, .title = "Pitch Bend", .units = "", .stepCount = 0, .flags = kFlagAH, .def = 0.5, .leaf = kNoLeafRow},
        {.id = 5, .title = "Sustain Pedal", .units = "", .stepCount = 0, .flags = kFlagAH, .def = 0.0, .leaf = kNoLeafRow},
        {.id = 6, .title = "Mod Wheel", .units = "%", .stepCount = 0, .flags = kFlagH, .def = 0.0, .leaf = kNoLeafRow},
        {.id = 7, .title = "Channel Pressure", .units = "%", .stepCount = 0, .flags = kFlagH, .def = 0.0, .leaf = kNoLeafRow},
        // ---- macros 100-103 (unchanged from Phase 10), Mass, Morph Position ----
        {.id = 100, .title = "Weight", .units = "%", .stepCount = 0, .flags = kFlagA, .def = 0.5, .leaf = kNoLeafRow},
        {.id = 101, .title = "Dark/Bright", .units = "%", .stepCount = 0, .flags = kFlagA, .def = 0.5, .leaf = kNoLeafRow},
        {.id = 102, .title = "Tight/Loose", .units = "%", .stepCount = 0, .flags = kFlagA, .def = 0.5, .leaf = kNoLeafRow},
        {.id = 103, .title = "Clean/Violent", .units = "%", .stepCount = 0, .flags = kFlagA, .def = 0.5, .leaf = kNoLeafRow},
        {.id = 104, .title = "Mass", .units = "", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = 52},
        {.id = 105, .title = "Morph Position", .units = "%", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = kNoLeafRow},
        // ---- core 200-208 ----
        {.id = 200, .title = "Depth", .units = "%", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = 0},
        {.id = 201, .title = "Body", .units = "%", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = 1},
        {.id = 202, .title = "Body Curvature", .units = "%", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = 2},
        {.id = 203, .title = "Body Emphasis", .units = "%", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = 3},
        {.id = 204, .title = "Edge", .units = "%", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = 4},
        {.id = 205, .title = "Spectral Shift", .units = "%", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = 5},
        {.id = 206, .title = "Excitation Mix", .units = "%", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = 6},
        {.id = 207, .title = "Excitation Drive", .units = "%", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = 7},
        {.id = 208, .title = "Excitation Asymmetry", .units = "%", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = 8},
        // ---- anchor 300-306 ----
        {.id = 300, .title = "Anchor Waveform", .units = "", .stepCount = 4, .flags = kFlagL, .def = 0.0, .leaf = 55},
        {.id = 301, .title = "Anchor Mode", .units = "", .stepCount = 3, .flags = kFlagL, .def = 0.0, .leaf = 56},
        {.id = 302, .title = "Anchor Level", .units = "%", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = 57},
        {.id = 303, .title = "Punch Boost", .units = "dB", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = 58},
        {.id = 304, .title = "Punch Time", .units = "ms", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = 59},
        {.id = 305, .title = "Ghost Time", .units = "ms", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = 60},
        {.id = 306, .title = "Anchor Fold Feed", .units = "%", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = 61},
        // ---- drop 400-403 ----
        {.id = 400, .title = "Drop Amount", .units = "st", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = 14},
        {.id = 401, .title = "Drop Time", .units = "ms", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = 15},
        {.id = 402, .title = "Drop Shape", .units = "%", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = 16},
        {.id = 403, .title = "Drop Rebound", .units = "%", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = 17},
        // ---- note behaviour 410-417 ----
        {.id = 410, .title = "Mono Priority", .units = "", .stepCount = 2, .flags = kFlagL, .def = 0.0, .leaf = kNoLeafRow},
        {.id = 411, .title = "Legato", .units = "", .stepCount = 1, .flags = kFlagL, .def = 0.0, .leaf = kNoLeafRow},
        {.id = 412, .title = "Portamento", .units = "ms", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = kNoLeafRow},
        {.id = 413, .title = "Portamento Mode", .units = "", .stepCount = 1, .flags = kFlagL, .def = 0.0, .leaf = kNoLeafRow},
        {.id = 414, .title = "Glide Mode", .units = "", .stepCount = 2, .flags = kFlagL, .def = 0.0, .leaf = kNoLeafRow},
        {.id = 415, .title = "Note Memory", .units = "", .stepCount = 3, .flags = kFlagL, .def = 0.0, .leaf = kNoLeafRow},
        {.id = 416, .title = "Note Retrigger", .units = "", .stepCount = 1, .flags = kFlagL, .def = 0.0, .leaf = kNoLeafRow},
        {.id = 417, .title = "Pitch Bend Range", .units = "st", .stepCount = 24, .flags = kFlagA, .def = 2.0 / 24.0, .leaf = kNoLeafRow},
        // ---- amp envelope 420-426 ----
        {.id = 420, .title = "Attack", .units = "ms", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = 18},
        {.id = 421, .title = "Decay", .units = "ms", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = 19},
        {.id = 422, .title = "Sustain", .units = "%", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = 20},
        {.id = 423, .title = "Release", .units = "ms", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = 21},
        {.id = 424, .title = "Attack Curve", .units = "%", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = 22},
        {.id = 425, .title = "Decay Curve", .units = "%", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = 23},
        {.id = 426, .title = "Release Curve", .units = "%", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = 24},
        // ---- fold / shape 500-501 ----
        {.id = 500, .title = "Harmonic Fold", .units = "", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = 9},
        {.id = 501, .title = "Shape", .units = "%", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = 10},
        // ---- character 600-602 ----
        {.id = 600, .title = "Character", .units = "", .stepCount = 5, .flags = kFlagL, .def = 0.0, .leaf = 11},
        {.id = 601, .title = "Character Amount", .units = "%", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = 12},
        {.id = 602, .title = "Character Tone", .units = "%", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = 13},
        // ---- dynamics 700-701 ----
        {.id = 700, .title = "Transient Attack", .units = "dB", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = 53},
        {.id = 701, .title = "Transient Sustain", .units = "dB", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = 54},
        // ---- field 800-809 ----
        {.id = 800, .title = "Image Mode", .units = "", .stepCount = 2, .flags = kFlagL, .def = 0.0, .leaf = 25},
        {.id = 801, .title = "Spread", .units = "%", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = 26},
        {.id = 802, .title = "Gravity", .units = "%", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = 27},
        {.id = 803, .title = "Crossover", .units = "Hz", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = 28},
        {.id = 804, .title = "Curve", .units = "%", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = 29},
        {.id = 805, .title = "Field Motion Depth", .units = "%", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = 30},
        {.id = 806, .title = "Field Motion Rate", .units = "%", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = 31},
        {.id = 807, .title = "Field Motion Source", .units = "", .stepCount = 1, .flags = kFlagL, .def = 0.0, .leaf = 32},
        {.id = 808, .title = "Field Sync", .units = "", .stepCount = 1, .flags = kFlagL, .def = 0.0, .leaf = 33},
        {.id = 809, .title = "Field Sync Rate", .units = "", .stepCount = 29, .flags = kFlagL, .def = 0.0, .leaf = 34},
        // ---- motion 900-914 ----
        {.id = 900, .title = "Motion Source", .units = "", .stepCount = 3, .flags = kFlagL, .def = 0.0, .leaf = 36},
        {.id = 901, .title = "Motion Target", .units = "", .stepCount = 4, .flags = kFlagL, .def = 0.0, .leaf = 37},
        {.id = 902, .title = "Motion Amount", .units = "%", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = 38},
        {.id = 903, .title = "Motion Depth", .units = "%", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = 39},
        {.id = 904, .title = "Cycle Rate", .units = "Hz", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = 40},
        {.id = 905, .title = "Cycle Shape", .units = "", .stepCount = 5, .flags = kFlagL, .def = 0.0, .leaf = 41},
        {.id = 906, .title = "Cycle Sync", .units = "", .stepCount = 1, .flags = kFlagL, .def = 0.0, .leaf = 42},
        {.id = 907, .title = "Cycle Sync Rate", .units = "", .stepCount = 29, .flags = kFlagL, .def = 0.0, .leaf = 43},
        {.id = 908, .title = "Cycle Phase", .units = "deg", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = 45},
        {.id = 909, .title = "Cycle Retrigger", .units = "", .stepCount = 1, .flags = kFlagL, .def = 0.0, .leaf = 46},
        {.id = 910, .title = "Cycle Fade-In", .units = "ms", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = 47},
        {.id = 911, .title = "Motion Polarity", .units = "", .stepCount = 1, .flags = kFlagL, .def = 0.0, .leaf = 48},
        {.id = 912, .title = "Drift Smoothness", .units = "%", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = 49},
        {.id = 913, .title = "Env Attack", .units = "ms", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = 50},
        {.id = 914, .title = "Env Decay", .units = "ms", .stepCount = 0, .flags = kFlagA, .def = 0.0, .leaf = 51},
        // ---- morph control 1000-1002 ----
        {.id = 1000, .title = "Anchor Morph Mode", .units = "", .stepCount = 2, .flags = kFlagL, .def = 0.5, .leaf = kNoLeafRow},  // Blend, index 1 of 2
        {.id = 1001, .title = "Edit Target", .units = "", .stepCount = 1, .flags = kFlagListOnly, .def = 0.0, .leaf = kNoLeafRow},
        {.id = 1002, .title = "Morph Ramp Time", .units = "ms", .stepCount = 0, .flags = kFlagA, .def = rampDefault, .leaf = kNoLeafRow},
    };
}

/// The registered default an E row must carry (FR-003): defaultNormalizedOf(leaf); for a Sync Rate row (note leaf
/// 34 / 43, modifier leaf + 1) the kNoteValueDropdownMapping index of the default pair over 29, because that list's
/// normalised value addresses the 30-entry product, not the note leaf alone (FR-012).
double expectedLeafDefault(int leaf) {
    const auto l = static_cast<std::size_t>(leaf);
    if (leaf == 34 || leaf == 43) {
        const int idx = P::syncRateIndexOf(P::kDefaultLeafBits[l], P::kDefaultLeafBits[l + 1]);
        REQUIRE(idx >= 0);
        return static_cast<double>(idx) / 29.0;
    }
    return P::defaultNormalizedOf(static_cast<std::uint8_t>(leaf));
}

std::string asciiOf(const Steinberg::Vst::TChar* s) {
    char buf[256] = {};
    Steinberg::UString(const_cast<Steinberg::Vst::TChar*>(s), 128)  // NOLINT(cppcoreguidelines-pro-type-const-cast)
        .toAscii(buf, 256);
    return {buf};
}

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.ends_with(suffix);
}

// Heap Controller, initialised with a null context, terminated on scope exit.
struct TableController {
    Steinberg::IPtr<::Profundum::Controller> ctl = Steinberg::owned(new ::Profundum::Controller());

    TableController() { REQUIRE(ctl->initialize(nullptr) == Steinberg::kResultOk); }
    ~TableController() { ctl->terminate(); }
    TableController(const TableController&) = delete;
    TableController& operator=(const TableController&) = delete;
    TableController(TableController&&) = delete;
    TableController& operator=(TableController&&) = delete;

    std::string str(ParamID id, double v) const {
        Steinberg::Vst::String128 s{};
        REQUIRE(ctl->getParamStringByValue(id, v, s) == Steinberg::kResultOk);
        return asciiOf(s);
    }

    PInfo info(ParamID id) const {
        PInfo i{};
        REQUIRE(ctl->getParameterInfoByTag(id, i) == Steinberg::kResultOk);
        return i;
    }
};

/// The normalised value of plain x on an E row's leaf law.
double normOfLeafPlain(ParamID id, double x) {
    const P::SoundParamRow* row = P::soundRowOf(id);
    REQUIRE(row != nullptr);
    return P::normalizedFromPlain(P::lawOf(row->leaf), x);
}

}  // namespace

// SC-005 (FR-001 - FR-004, FR-009, FR-054).
TEST_CASE("Profundum_ParamTableConformance", "[profundum][params]") {
    const TableController h;
    const std::vector<ExpectedParam> table = expectedParams();
    REQUIRE(table.size() == 84);
    REQUIRE(h.ctl->getParameterCount() == 84);

    SECTION("PerIdTable") {
        for (const ExpectedParam& e : table) {
            INFO("id " << e.id << " (" << e.title << ")");
            const PInfo info = h.info(e.id);
            CHECK(info.id == e.id);
            CHECK(asciiOf(info.title) == e.title);
            CHECK(asciiOf(info.units) == e.units);
            CHECK(info.stepCount == e.stepCount);
            CHECK(info.flags == e.flags);
            const double want = e.leaf >= 0 ? expectedLeafDefault(e.leaf) : e.def;
            CHECK(info.defaultNormalizedValue == want);
            if (e.leaf >= 0) {
                const P::SoundParamRow* row = P::soundRowOf(e.id);
                REQUIRE(row != nullptr);
                CHECK(std::cmp_equal(row->leaf, e.leaf));
            }
        }
    }

    SECTION("EveryRegisteredIdInTableAndBand") {
        std::set<ParamID> seen;
        for (int32 i = 0; i < h.ctl->getParameterCount(); ++i) {
            PInfo info{};
            REQUIRE(h.ctl->getParameterInfo(i, info) == Steinberg::kResultOk);
            INFO("index " << i << " id " << info.id);
            CHECK(seen.insert(info.id).second);  // no duplicate
            bool inTable = false;
            for (const ExpectedParam& e : table) {
                inTable = inTable || e.id == info.id;
            }
            CHECK(inTable);
            // Documented bands: 0-99 global ... 1000-1099 morph control; every registered ID is routed.
            CHECK(info.id < static_cast<ParamID>(P::kMorphParamRangeEnd));
            REQUIRE(info.id < static_cast<ParamID>(P::kNumRoutedIds));
            CHECK(P::kParamRoutes[info.id].kind != P::RouteKind::None);
        }
        CHECK(seen.size() == 84);

        // FR-054: the registered IDs in 400-499 are exactly {400-403, 410-417, 420-426}; no velocity ID.
        std::set<ParamID> noteBand;
        for (const ParamID id : seen) {
            if (id >= 400 && id < 500) {
                noteBand.insert(id);
            }
        }
        std::set<ParamID> want;
        for (ParamID id = 400; id <= 403; ++id) {
            want.insert(id);
        }
        for (ParamID id = 410; id <= 417; ++id) {
            want.insert(id);
        }
        for (ParamID id = 420; id <= 426; ++id) {
            want.insert(id);
        }
        CHECK(noteBand == want);
    }

    SECTION("Names") {
        // FR-009
        CHECK(asciiOf(h.info(P::kFoldAmountId).title) == "Harmonic Fold");

        const std::array<std::string, 6> characterLabels{"Tube", "Wavefold", "Rectify", "Bite", "Crush", "Fracture"};
        for (int k = 0; k <= 5; ++k) {
            INFO("Character k " << k);
            CHECK(h.str(P::kCharacterId, static_cast<double>(k) / 5.0) ==
                  characterLabels[static_cast<std::size_t>(k)]);
        }

        for (int32 i = 0; i < h.ctl->getParameterCount(); ++i) {
            PInfo info{};
            REQUIRE(h.ctl->getParameterInfo(i, info) == Steinberg::kResultOk);
            INFO("id " << info.id);
            CHECK(asciiOf(info.title).find("Bite") == std::string::npos);
        }

        for (const double v : {0.0, 0.5, 1.0}) {
            const std::string shape = h.str(P::kFoldShapeId, v);
            INFO("Shape at " << v << " reads " << shape);
            for (const std::string& label : characterLabels) {
                CHECK(shape.find(label) == std::string::npos);
            }
        }

        CHECK(h.str(P::kMorphAnchorModeId, 0.0) == "Hold");
        CHECK(h.str(P::kMorphAnchorModeId, 0.5) == "Blend");
        CHECK(h.str(P::kMorphAnchorModeId, 1.0) == "Yield");

        const std::array<std::string, 4> anchorModes{"Lock", "Follow", "Punch", "Ghost"};
        for (int k = 0; k <= 3; ++k) {
            INFO("Anchor Mode k " << k);
            CHECK(h.str(P::kAnchorModeId, static_cast<double>(k) / 3.0) == anchorModes[static_cast<std::size_t>(k)]);
        }
    }

    SECTION("Flags") {
        // FR-004
        for (const ParamID id : {static_cast<ParamID>(P::kModWheelId), static_cast<ParamID>(P::kChannelPressureId)}) {
            INFO("id " << id);
            const PInfo info = h.info(id);
            CHECK((info.flags & PInfo::kIsHidden) != 0);
            CHECK((info.flags & PInfo::kCanAutomate) == 0);
        }
        for (const ParamID id : {static_cast<ParamID>(P::kPitchBendId), static_cast<ParamID>(P::kSustainPedalId)}) {
            INFO("id " << id);
            const PInfo info = h.info(id);
            CHECK((info.flags & PInfo::kIsHidden) != 0);
            CHECK((info.flags & PInfo::kCanAutomate) != 0);
        }
        {
            const PInfo info = h.info(P::kMorphEditTargetId);
            CHECK((info.flags & PInfo::kIsList) != 0);
            CHECK((info.flags & PInfo::kCanAutomate) == 0);
        }
        for (int32 i = 0; i < h.ctl->getParameterCount(); ++i) {
            PInfo info{};
            REQUIRE(h.ctl->getParameterInfo(i, info) == Steinberg::kResultOk);
            INFO("id " << info.id);
            const bool isNew = info.id > 3 && (info.id < 100 || info.id > 103);
            const bool exempt = info.id == static_cast<ParamID>(P::kModWheelId) ||
                                info.id == static_cast<ParamID>(P::kChannelPressureId) ||
                                info.id == static_cast<ParamID>(P::kMorphEditTargetId);
            if (isNew && !exempt) {
                CHECK((info.flags & PInfo::kCanAutomate) != 0);
            }
            // Every list (a StringListParameter: stepCount > 0, except the Pitch Bend Range RangeParameter) is kIsList.
            if (info.stepCount > 0 && info.id != static_cast<ParamID>(P::kNotePitchBendRangeId)) {
                CHECK((info.flags & PInfo::kIsList) != 0);
            }
        }
    }
}

// FR-006 exact strings (plan 4.3; Q11 Hz ruling).
TEST_CASE("Profundum_ParamDisplayFormats", "[profundum][params]") {
    const TableController h;

    // Release 423: ms below 1000 (after rounding to 0.1), otherwise s with two decimals.
    CHECK(h.str(P::kAmpReleaseId, normOfLeafPlain(P::kAmpReleaseId, 999.9)) == "999.9 ms");
    CHECK(h.str(P::kAmpReleaseId, normOfLeafPlain(P::kAmpReleaseId, 999.96)) == "1.00 s");
    CHECK(h.str(P::kAmpReleaseId, normOfLeafPlain(P::kAmpReleaseId, 1000.0)) == "1.00 s");
    CHECK(h.str(P::kAmpReleaseId, normOfLeafPlain(P::kAmpReleaseId, 2500.0)) == "2.50 s");

    // Cycle Rate 904: two decimals below 1 Hz, one at 1 Hz and above.
    CHECK(h.str(P::kMotionCycleRateId, normOfLeafPlain(P::kMotionCycleRateId, 0.01)) == "0.01 Hz");
    CHECK(h.str(P::kMotionCycleRateId, normOfLeafPlain(P::kMotionCycleRateId, 0.3)) == "0.30 Hz");
    CHECK(h.str(P::kMotionCycleRateId, normOfLeafPlain(P::kMotionCycleRateId, 2.5)) == "2.5 Hz");
    CHECK(h.str(P::kMotionCycleRateId, normOfLeafPlain(P::kMotionCycleRateId, 20.0)) == "20.0 Hz");

    // Transient Attack 700: signed dB, one decimal.
    CHECK(h.str(P::kDynamicsTransientAttackId, normOfLeafPlain(P::kDynamicsTransientAttackId, -12.0)) ==
          "-12.0 dB");
    CHECK(h.str(P::kDynamicsTransientAttackId, normOfLeafPlain(P::kDynamicsTransientAttackId, 0.0)) == "+0.0 dB");
    CHECK(h.str(P::kDynamicsTransientAttackId, normOfLeafPlain(P::kDynamicsTransientAttackId, 12.0)) ==
          "+12.0 dB");

    // Drop Amount 400: st, one decimal.
    CHECK(h.str(P::kDropAmountId, normOfLeafPlain(P::kDropAmountId, 24.0)) == "24.0 st");

    // Spread 801: plain [0, 1] as %.
    CHECK(h.str(P::kFieldSpreadId, normOfLeafPlain(P::kFieldSpreadId, 0.5)) == "50%");

    // Motion Amount 902: bipolar %.
    CHECK(h.str(P::kMotionAmountId, normOfLeafPlain(P::kMotionAmountId, -1.0)) == "-100%");
    CHECK(h.str(P::kMotionAmountId, normOfLeafPlain(P::kMotionAmountId, 1.0)) == "+100%");

    // Mass 104 and Harmonic Fold 500: 0-100, no unit.
    CHECK(h.str(P::kMassId, normOfLeafPlain(P::kMassId, 50.0)) == "50");
    CHECK(h.str(P::kFoldAmountId, normOfLeafPlain(P::kFoldAmountId, 100.0)) == "100");

    // Cycle Phase 908: degrees.
    CHECK(h.str(P::kMotionCyclePhaseId, normOfLeafPlain(P::kMotionCyclePhaseId, 90.0)) == "90 deg");
}

// SC-016 / FR-007: the Portamento (412) suffix follows the controller's current Glide Mode (414).
TEST_CASE("Profundum_PortamentoLabelFollowsGlideMode", "[profundum][params]") {
    const TableController h;

    const std::array<double, 3> glideNorm{0.0, 0.5, 1.0};  // ConstantTime, ConstantRate, Quantized
    const std::array<std::string, 3> suffix{" ms", " ms/oct", " ms"};
    std::array<std::string, 3> prefix{};

    for (std::size_t k = 0; k < glideNorm.size(); ++k) {
        REQUIRE(h.ctl->setParamNormalized(P::kNoteGlideModeId, glideNorm[k]) == Steinberg::kResultOk);
        const std::string s = h.str(P::kNotePortamentoId, 0.3);
        INFO("glide " << glideNorm[k] << " reads " << s);
        REQUIRE(endsWith(s, suffix[k]));
        if (k != 1) {
            CHECK_FALSE(endsWith(s, " ms/oct"));  // non-vacuity: the per-octave suffix appears only under ConstantRate
        }
        prefix[k] = s.substr(0, s.size() - suffix[k].size());
        CHECK_FALSE(prefix[k].empty());
    }
    CHECK(prefix[0] == prefix[1]);
    CHECK(prefix[1] == prefix[2]);
}
