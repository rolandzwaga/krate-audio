// ==============================================================================
// Profundum - parameter latching and denormalisation (Phase 10 T009)
// ==============================================================================
// SC-010 processor arm (FR-024.1 - .2, FR-042, FR-043). Every check sends a
// one-change Krate::Test::ParameterChanges through a numOutputs = 0 process() call on an
// initialised, UNPREPARED processor and reads the latched atomics back through
// the const test seams. T010 appends the controller sections (RegisteredInfo,
// Display: SC-010 controller arm, FR-047) on a heap Controller.
//
// Phase 11 T008 appends the leaf-law sections (SC-006, FR-002, FR-003, FR-011,
// FR-012): the double mapping pair of parameters/sound_param_row.h, the stored
// word boxes, list indices, default bits, the leaf visitor and the generic row
// write. They need no processor and run on the same initialised fixture.
//
// Phase 11 T014 amends RegisteredInfo for the 84 registered parameters (the 8
// Phase 10 IDs keep their defaults; the 76 new IDs are each routed) and adds
// RegisteredParamRoundTrip (SC-006 parameter arms).
// ==============================================================================

#include "profundum_test_fixture.h"

#include "controller/controller.h"
#include "parameters/global_params.h"
#include "parameters/macro_params.h"
#include "parameters/morph_params.h"
#include "parameters/note_params.h"
#include "parameters/sound_param_row.h"
#include "parameters/sound_param_table.h"
#include "plugin_ids.h"

#include "base/source/fobject.h"
#include "pluginterfaces/base/ustring.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace ProfundumPluginTest;

namespace {

using Steinberg::Vst::ParamID;

constexpr std::array<ParamID, 8> kAllIds{
    ::Profundum::kOutputGainId,      ::Profundum::kVoiceModeId,
    ::Profundum::kPolyphonyId,       ::Profundum::kOutputCeilingId,
    ::Profundum::kMacroWeightId,     ::Profundum::kMacroDarkBrightId,
    ::Profundum::kMacroTightLooseId, ::Profundum::kMacroCleanViolentId,
};

constexpr std::array<ParamID, 4> kMacroIds{
    ::Profundum::kMacroWeightId, ::Profundum::kMacroDarkBrightId,
    ::Profundum::kMacroTightLooseId, ::Profundum::kMacroCleanViolentId,
};

// The eight latched atomics, read relaxed.
struct Snapshot {
    float gain = 0.0f;
    int voiceMode = 0;
    int polyphony = 0;
    float ceilingDb = 0.0f;
    std::array<float, 4> macros{};

    bool operator==(const Snapshot&) const = default;
};

Snapshot snapshot(const ::Profundum::Processor& p) {
    const auto& g = p.globalParamsForTest();
    const auto& m = p.macroParamsForTest();
    Snapshot s;
    s.gain = g.outputGain.load(std::memory_order_relaxed);
    s.voiceMode = g.voiceMode.load(std::memory_order_relaxed);
    s.polyphony = g.polyphony.load(std::memory_order_relaxed);
    s.ceilingDb = g.outputCeilingDb.load(std::memory_order_relaxed);
    for (int i = 0; i < ::Profundum::kNumMacroParams; ++i) {
        s.macros[static_cast<std::size_t>(i)] =
            ::Profundum::macroField(m, i).load(std::memory_order_relaxed);
    }
    return s;
}

// One queue holding one point at offset 0, sent through a parameter-only call.
void sendOne(ProcessorFixture& fx, Krate::Test::ParameterChanges& changes, ParamID id, double value) {
    changes.setChange(id, value);
    REQUIRE(fx.processNoOutputs(&changes) == Steinberg::kResultOk);
}

std::string toAscii(const Steinberg::Vst::TChar* s) {
    char buf[128] = {};
    Steinberg::UString(const_cast<Steinberg::Vst::TChar*>(s), 128)  // NOLINT(cppcoreguidelines-pro-type-const-cast)
        .toAscii(buf, 128);
    return {buf};
}

// Heap Controller (FR-064), initialised with a null context, terminated on scope exit.
struct ControllerHandle {
    Steinberg::IPtr<::Profundum::Controller> ctl =
        Steinberg::owned(new ::Profundum::Controller());

    ControllerHandle() { REQUIRE(ctl->initialize(nullptr) == Steinberg::kResultOk); }
    ~ControllerHandle() { ctl->terminate(); }
    ControllerHandle(const ControllerHandle&) = delete;
    ControllerHandle& operator=(const ControllerHandle&) = delete;
    ControllerHandle(ControllerHandle&&) = delete;
    ControllerHandle& operator=(ControllerHandle&&) = delete;

    std::string str(ParamID id, double v) const {
        Steinberg::Vst::String128 s{};
        REQUIRE(ctl->getParamStringByValue(id, v, s) == Steinberg::kResultOk);
        return toAscii(s);
    }
};

using MorphKind = Krate::DSP::MorphState::Kind;

// -0.0 built from its bit pattern through a volatile (immune to constant folding).
[[nodiscard]] double makeNegZeroD() noexcept {
    volatile std::uint64_t bits = 0x8000000000000000ULL;
    return std::bit_cast<double>(static_cast<std::uint64_t>(bits));
}

double expectedDefault(ParamID id) {
    switch (id) {
        case ::Profundum::kOutputGainId: return 1.0;
        case ::Profundum::kVoiceModeId: return 0.0;
        case ::Profundum::kPolyphonyId: return 3.0 / 7.0;
        case ::Profundum::kOutputCeilingId: return 11.0 / 12.0;
        default: return 0.5;  // the four macros, 100..103
    }
}

}  // namespace

TEST_CASE("Profundum_ParamDenormRoundTrip", "[profundum][params]") {
    ProcessorFixture fx;  // initialised, NOT prepared
    Krate::Test::ParameterChanges changes;
    const ::Profundum::Processor& proc = *fx.proc;

    SECTION("GridValues") {
        constexpr std::array<double, 5> kGrid{0.0, 0.25, 0.5, 0.75, 1.0};
        constexpr std::array<int, 5> kExpectedMode{0, 0, 1, 1, 1};
        constexpr std::array<int, 5> kExpectedPoly{1, 3, 5, 6, 8};

        for (std::size_t k = 0; k < kGrid.size(); ++k) {
            const double v = kGrid[k];
            CAPTURE(v);

            // Output gain: exact 0 / 1 at the ends, the -60 dB taper in between.
            sendOne(fx, changes, ::Profundum::kOutputGainId, v);
            const float g = proc.globalParamsForTest().outputGain.load(std::memory_order_relaxed);
            CAPTURE(g);
            REQUIRE(g >= 0.0f);
            REQUIRE(g <= 1.0f);
            if (k == 0) {
                REQUIRE(g == 0.0f);
            } else if (k == kGrid.size() - 1) {
                REQUIRE(g == 1.0f);
            } else {
                const double e = static_cast<double>(
                    static_cast<float>(std::pow(10.0, (-60.0 + 60.0 * v) / 20.0)));
                REQUIRE(std::abs(static_cast<double>(g) - e) <= 1e-5 * e);
            }

            // Voice mode: v >= 0.5 -> Poly.
            sendOne(fx, changes, ::Profundum::kVoiceModeId, v);
            REQUIRE(proc.globalParamsForTest().voiceMode.load(std::memory_order_relaxed) ==
                    kExpectedMode[k]);

            // Polyphony: "1".."8", round-to-nearest of v * 7 + 1.
            sendOne(fx, changes, ::Profundum::kPolyphonyId, v);
            REQUIRE(proc.globalParamsForTest().polyphony.load(std::memory_order_relaxed) ==
                    kExpectedPoly[k]);

            // Ceiling: [-12, 0] dBTP, linear.
            sendOne(fx, changes, ::Profundum::kOutputCeilingId, v);
            const float c =
                proc.globalParamsForTest().outputCeilingDb.load(std::memory_order_relaxed);
            REQUIRE(std::abs(static_cast<double>(c) - (-12.0 + 12.0 * v)) <= 1e-5);

            // Macros: identity.
            for (int i = 0; i < ::Profundum::kNumMacroParams; ++i) {
                const auto idx = static_cast<std::size_t>(i);
                sendOne(fx, changes, kMacroIds[idx], v);
                const float m = ::Profundum::macroField(proc.macroParamsForTest(), i)
                                    .load(std::memory_order_relaxed);
                CAPTURE(i, m);
                REQUIRE(std::abs(static_cast<double>(m) - v) <= 1e-6);
            }
        }
    }

    SECTION("LastPointWins") {
        MultiParamChanges multi;  // two points per queue: the shared single-point mock cannot carry them
        multi.reserve(1);
        {
            auto& q = multi.addQueue(::Profundum::kMacroWeightId);
            q.addTestPoint(0, 0.1);
            q.addTestPoint(100, 0.9);
        }
        REQUIRE(fx.processNoOutputs(&multi) == Steinberg::kResultOk);
        const float w = proc.macroParamsForTest().weight.load(std::memory_order_relaxed);
        REQUIRE(std::abs(static_cast<double>(w) - 0.9) <= 1e-6);

        multi.clear();
        {
            auto& q = multi.addQueue(::Profundum::kOutputCeilingId);
            q.addTestPoint(0, 0.1);
            q.addTestPoint(100, 0.9);
        }
        REQUIRE(fx.processNoOutputs(&multi) == Steinberg::kResultOk);
        const float c = proc.globalParamsForTest().outputCeilingDb.load(std::memory_order_relaxed);
        REQUIRE(std::abs(static_cast<double>(c) - (-12.0 + 12.0 * 0.9)) <= 1e-5);
    }

    SECTION("UnregisteredIds") {
        const Snapshot before = snapshot(proc);
        constexpr std::array<ParamID, 6> kUnregistered{4, 99, 104, 199, 200, 1000};
        for (const ParamID id : kUnregistered) {
            CAPTURE(id);
            sendOne(fx, changes, id, 0.3);
            REQUIRE(snapshot(proc) == before);
        }
    }

    SECTION("NaNIgnored") {
        // Known non-default values first, so a stray store of a default is visible too.
        sendOne(fx, changes, ::Profundum::kOutputGainId, 0.5);
        sendOne(fx, changes, ::Profundum::kVoiceModeId, 1.0);
        sendOne(fx, changes, ::Profundum::kPolyphonyId, 1.0);
        sendOne(fx, changes, ::Profundum::kOutputCeilingId, 0.25);
        for (const ParamID id : kMacroIds) {
            sendOne(fx, changes, id, 0.75);
        }
        const Snapshot known = snapshot(proc);
        REQUIRE(known.voiceMode == 1);
        REQUIRE(known.polyphony == 8);

        const double nan = makeNaND();
        const double posInf = static_cast<double>(makePosInf());
        for (const ParamID id : kAllIds) {
            CAPTURE(id);
            sendOne(fx, changes, id, nan);
            REQUIRE(snapshot(proc) == known);
            sendOne(fx, changes, id, posInf);
            REQUIRE(snapshot(proc) == known);
        }
    }

    SECTION("RegisteredInfo") {
        const ControllerHandle h;
        REQUIRE(h.ctl->getParameterCount() == 95);  // Phase 11 (FR-001): 8 Phase 10 + 76 new; Phase 14: + 9; Phase 15: + 2

        std::array<bool, kAllIds.size()> seen{};
        std::array<bool, ::Profundum::kNumRoutedIds> seenNew{};
        int newIds = 0;
        for (Steinberg::int32 i = 0; i < h.ctl->getParameterCount(); ++i) {
            Steinberg::Vst::ParameterInfo info{};
            REQUIRE(h.ctl->getParameterInfo(i, info) == Steinberg::kResultOk);
            CAPTURE(i, info.id, info.defaultNormalizedValue);
            bool known = false;
            for (std::size_t k = 0; k < kAllIds.size(); ++k) {
                if (kAllIds[k] == info.id) {
                    REQUIRE_FALSE(seen[k]);
                    seen[k] = true;
                    known = true;
                }
            }
            if (known) {
                // The Phase 10 IDs keep their Phase 10 defaults and step counts.
                REQUIRE(std::abs(info.defaultNormalizedValue - expectedDefault(info.id)) <= 1e-12);
                if (info.id == ::Profundum::kPolyphonyId) {
                    REQUIRE(info.stepCount == 7);
                }
                if (info.id == ::Profundum::kVoiceModeId) {
                    REQUIRE(info.stepCount == 1);
                }
                continue;
            }
            // A new ID: routed, registered once, default inside [0, 1] (exact values: Profundum_ParamTableConformance).
            REQUIRE(info.id < static_cast<ParamID>(::Profundum::kNumRoutedIds));
            REQUIRE(::Profundum::kParamRoutes[info.id].kind != ::Profundum::RouteKind::None);
            REQUIRE_FALSE(seenNew[info.id]);
            seenNew[info.id] = true;
            ++newIds;
            REQUIRE(info.defaultNormalizedValue >= 0.0);
            REQUIRE(info.defaultNormalizedValue <= 1.0);
        }
        for (const bool s : seen) {
            REQUIRE(s);
        }
        REQUIRE(newIds == 87);   // Phase 15: + 209, 210

        for (int k = 0; k <= 7; ++k) {
            CAPTURE(k);
            REQUIRE(h.str(::Profundum::kPolyphonyId, static_cast<double>(k) / 7.0) ==
                    std::to_string(k + 1));
        }
        REQUIRE(h.str(::Profundum::kVoiceModeId, 0.0) == "Mono");
        REQUIRE(h.str(::Profundum::kVoiceModeId, 1.0) == "Poly");
    }

    SECTION("Display") {
        const ControllerHandle h;
        REQUIRE(h.str(::Profundum::kOutputGainId, 0.0) == "-inf dB");
        REQUIRE(h.str(::Profundum::kOutputGainId, 1.0) == "0.0 dB");
        REQUIRE(h.str(::Profundum::kOutputGainId, 0.9) == "-6.0 dB");
        REQUIRE(h.str(::Profundum::kOutputCeilingId, 11.0 / 12.0) == "-1.0 dBTP");
        REQUIRE(h.str(::Profundum::kMacroWeightId, 0.5) == "50%");
    }

    // ==========================================================================
    // Phase 11 T008 - leaf laws (SC-006, FR-002, FR-003; plan 4.1, 4.3)
    // ==========================================================================

    SECTION("LeafLawRoundTrip") {
        for (std::size_t n = 0; n < ::Profundum::kNumLeaves; ++n) {
            const auto i = static_cast<std::uint8_t>(n);
            const auto& f = ::Profundum::lawOf(i);
            if (f.kind != MorphKind::F32) {
                continue;
            }
            CAPTURE(n, f.name);
            for (int k = 0; k <= 100; ++k) {
                const double v = static_cast<double>(k) / 100.0;
                const double plain = ::Profundum::plainFromNormalized(f, v);
                const double back = ::Profundum::normalizedFromPlain(f, plain);
                CAPTURE(k, plain, back);
                REQUIRE(std::abs(back - v) <= 1e-9);
            }
            // Exact endpoints (the explicit snaps, independent of pow / log rounding).
            REQUIRE(::Profundum::plainFromNormalized(f, 0.0) == static_cast<double>(f.lo));
            REQUIRE(::Profundum::normalizedFromPlain(f, static_cast<double>(f.lo)) == 0.0);
            REQUIRE(::Profundum::plainFromNormalized(f, 1.0) == static_cast<double>(f.hi));
            REQUIRE(::Profundum::normalizedFromPlain(f, static_cast<double>(f.hi)) == 1.0);
        }
    }

    SECTION("LeafLawBoxes") {
        int hiOpenRows = 0;
        for (std::size_t n = 0; n < ::Profundum::kNumLeaves; ++n) {
            const auto i = static_cast<std::uint8_t>(n);
            const auto& f = ::Profundum::lawOf(i);
            if (f.hiOpen) {
                ++hiOpenRows;
            }
            if (f.kind != MorphKind::F32) {
                continue;
            }
            CAPTURE(n, f.name);
            // The stored plain range equals the table box bitwise (SC-006 last bullet).
            REQUIRE(::Profundum::leafWordFromNormalized(i, 0.0) == std::bit_cast<std::uint32_t>(f.lo));
            if (n != 45) {
                REQUIRE(::Profundum::leafWordFromNormalized(i, 1.0) == std::bit_cast<std::uint32_t>(f.hi));
            }
        }
        // motion.cyclePhaseDeg is the one open box: normalised 1 stores the float just below 360.
        REQUIRE(::Profundum::lawOf(45).hiOpen);
        REQUIRE(::Profundum::leafWordFromNormalized(45, 1.0) ==
                std::bit_cast<std::uint32_t>(std::nextafter(360.0f, 0.0f)));
        REQUIRE(hiOpenRows == 1);
    }

    SECTION("ListIndexRoundTrip") {
        int listLeaves = 0;
        for (std::size_t n = 0; n < ::Profundum::kNumLeaves; ++n) {
            const auto i = static_cast<std::uint8_t>(n);
            const auto& f = ::Profundum::lawOf(i);
            if (f.kind == MorphKind::F32) {
                continue;
            }
            ++listLeaves;
            const int maxEnum = static_cast<int>(f.maxEnum);
            REQUIRE(maxEnum >= 1);
            for (int k = 0; k <= maxEnum; ++k) {
                const double v = static_cast<double>(k) / static_cast<double>(maxEnum);
                CAPTURE(n, f.name, k, v);
                REQUIRE(::Profundum::indexFromNormalized(v, maxEnum) == k);
                REQUIRE(::Profundum::leafWordFromNormalized(i, v) == static_cast<std::uint32_t>(k));
            }
        }
        REQUIRE(listLeaves == 16);  // 13 enums + 3 bools (morph_state.h:132)
    }

    SECTION("DefaultLeafBitsRoundTrip") {
        // FR-003 / Risk R2: the registered default denormalises to MorphState{} bit-for-bit.
        for (std::size_t n = 0; n < ::Profundum::kNumLeaves; ++n) {
            const auto i = static_cast<std::uint8_t>(n);
            const double d = ::Profundum::defaultNormalizedOf(i);
            CAPTURE(n, ::Profundum::lawOf(i).name, d);
            REQUIRE(d >= 0.0);
            REQUIRE(d <= 1.0);
            REQUIRE(::Profundum::leafWordFromNormalized(i, d) == ::Profundum::kDefaultLeafBits[n]);
        }
    }

    SECTION("LeafBitsVisitor") {
        REQUIRE(Krate::DSP::detail::morph_state_io::leafBitsEqual(
            ::Profundum::morphStateFromLeafBits(::Profundum::kDefaultLeafBits), Krate::DSP::MorphState{}));

        Krate::DSP::MorphState s{};
        s.recipe.depth = 0.25f;
        s.characterAlgorithm = static_cast<Krate::DSP::BassCharacterStage::Algorithm>(3);
        s.motion.cycleRetrigger = false;
        const ::Profundum::LeafBits bits = ::Profundum::leafBitsOf(s);
        // Non-vacuity: the three edits are visible in the words.
        REQUIRE(bits[0] == std::bit_cast<std::uint32_t>(0.25f));
        REQUIRE(bits[11] == 3u);
        REQUIRE(bits[46] == 0u);
        REQUIRE(bits != ::Profundum::kDefaultLeafBits);
        REQUIRE(Krate::DSP::detail::morph_state_io::leafBitsEqual(::Profundum::morphStateFromLeafBits(bits), s));
        REQUIRE_FALSE(Krate::DSP::detail::morph_state_io::leafBitsEqual(
            ::Profundum::morphStateFromLeafBits(bits), Krate::DSP::MorphState{}));
    }

    SECTION("NegativeZeroSnapsToLo") {
        const double negZero = makeNegZeroD();
        REQUIRE(std::signbit(negZero));
        // The plugin never writes -0 into a [0, x] leaf.
        REQUIRE(::Profundum::leafWordFromNormalized(0, negZero) == std::bit_cast<std::uint32_t>(0.0f));
    }

    SECTION("SyncRateIndex") {
        for (int idx = 0; idx < Krate::DSP::kNoteValueDropdownCount; ++idx) {
            const auto& m = Krate::DSP::kNoteValueDropdownMapping[static_cast<std::size_t>(idx)];
            CAPTURE(idx);
            REQUIRE(::Profundum::syncRateIndexOf(static_cast<std::uint8_t>(m.note),
                                                 static_cast<std::uint8_t>(m.modifier)) == idx);
        }
    }

    SECTION("WriteSoundRowSetsFlags") {
        ::Profundum::EndpointParams ep;
        REQUIRE(ep.dirty.load(std::memory_order_relaxed));
        REQUIRE_FALSE(ep.syncPending.load(std::memory_order_relaxed));
        REQUIRE(ep.load() == ::Profundum::kDefaultLeafBits);
        ep.dirty.store(false, std::memory_order_relaxed);

        const ::Profundum::SoundParamRow row{.id = ::Profundum::kCoreDepthId,
                                             .leaf = 0,
                                             .leaf2 = ::Profundum::kNoLeaf,
                                             .title = STR16("Depth"),
                                             .units = STR16("%"),
                                             .labels = nullptr,
                                             .labelCount = 0,
                                             .display = ::Profundum::DisplayUnit::Percent};
        ::Profundum::writeSoundRow(ep, row, 1.0);

        const ::Profundum::LeafBits after = ep.load();
        REQUIRE(after[0] == std::bit_cast<std::uint32_t>(::Profundum::lawOf(0).hi));
        REQUIRE(after[0] != ::Profundum::kDefaultLeafBits[0]);  // non-vacuity: depth's default is 0.5, not hi
        REQUIRE(ep.dirty.load(std::memory_order_relaxed));
        REQUIRE(ep.syncPending.load(std::memory_order_relaxed));
        for (std::size_t n = 1; n < ::Profundum::kNumLeaves; ++n) {
            CAPTURE(n);
            REQUIRE(after[n] == ::Profundum::kDefaultLeafBits[n]);
        }
    }

    SECTION("RegisteredParamRoundTrip") {
        // SC-006 parameter arms (T014): the continuous laws of the registered non-leaf parameters (412, 1002, 105,
        // 4, 6, 7) and of every continuous E row, then every list through the controller's toPlain / toNormalized.
        struct NamedLaw {
            ParamID id;
            ::Profundum::PlainLaw law;
        };
        std::vector<NamedLaw> laws{
            {.id = ::Profundum::kNotePortamentoId, .law = ::Profundum::PlainLaw{.lo = 0.0, .hi = 10000.0, .logOffset = 1.0, .logarithmic = true}},
            {.id = ::Profundum::kMorphRampTimeId, .law = ::Profundum::PlainLaw{.lo = 5.0, .hi = 2000.0, .logOffset = 0.0, .logarithmic = true}},
            {.id = ::Profundum::kMorphPositionId, .law = ::Profundum::PlainLaw{.lo = 0.0, .hi = 1.0, .logOffset = 0.0, .logarithmic = false}},
            {.id = ::Profundum::kPitchBendId, .law = ::Profundum::PlainLaw{.lo = 0.0, .hi = 1.0, .logOffset = 0.0, .logarithmic = false}},
            {.id = ::Profundum::kModWheelId, .law = ::Profundum::PlainLaw{.lo = 0.0, .hi = 1.0, .logOffset = 0.0, .logarithmic = false}},
            {.id = ::Profundum::kChannelPressureId, .law = ::Profundum::PlainLaw{.lo = 0.0, .hi = 1.0, .logOffset = 0.0, .logarithmic = false}},
        };
        // The packs' own laws are the ones checked (non-vacuity: a drifted pack constant fails here).
        REQUIRE(::Profundum::kPortamentoLaw.lo == laws[0].law.lo);
        REQUIRE(::Profundum::kPortamentoLaw.hi == laws[0].law.hi);
        REQUIRE(::Profundum::kPortamentoLaw.logOffset == laws[0].law.logOffset);
        REQUIRE(::Profundum::kMorphRampLaw.lo == laws[1].law.lo);
        REQUIRE(::Profundum::kMorphRampLaw.hi == laws[1].law.hi);
        int continuousRows = 0;
        for (const auto& row : ::Profundum::kSoundRows) {
            const auto& f = ::Profundum::lawOf(row.leaf);
            if (row.leaf2 != ::Profundum::kNoLeaf || f.kind != MorphKind::F32) {
                continue;
            }
            ++continuousRows;
            laws.push_back({.id = row.id, .law = ::Profundum::plainLawOf(f)});
        }
        // 62 rows (Phase 15: + Stiffness, Phase) minus 12 enum / bool lists and the 2 Sync Rate pairs: the 48 F32 leaves.
        REQUIRE(continuousRows == 62 - 14);

        const ControllerHandle h;
        for (const NamedLaw& nl : laws) {
            CAPTURE(nl.id, nl.law.lo, nl.law.hi, nl.law.logOffset, nl.law.logarithmic);
            Steinberg::Vst::ParameterInfo info{};
            REQUIRE(h.ctl->getParameterInfoByTag(nl.id, info) == Steinberg::kResultOk);
            REQUIRE(info.stepCount == 0);  // continuous
            for (int k = 0; k <= 100; ++k) {
                const double v = static_cast<double>(k) / 100.0;
                const double back =
                    ::Profundum::normalizedFromPlain(nl.law, ::Profundum::plainFromNormalized(nl.law, v));
                CAPTURE(k, back);
                REQUIRE(std::abs(back - v) <= 1e-9);
            }
            REQUIRE(::Profundum::normalizedFromPlain(nl.law, nl.law.lo) == 0.0);
            REQUIRE(::Profundum::normalizedFromPlain(nl.law, nl.law.hi) == 1.0);
        }

        // Every list (and the stepped Pitch Bend Range): index -> normalised -> index exact.
        int steppedParams = 0;
        for (Steinberg::int32 i = 0; i < h.ctl->getParameterCount(); ++i) {
            Steinberg::Vst::ParameterInfo info{};
            REQUIRE(h.ctl->getParameterInfo(i, info) == Steinberg::kResultOk);
            if (info.stepCount <= 0) {
                continue;
            }
            ++steppedParams;
            for (Steinberg::int32 k = 0; k <= info.stepCount; ++k) {
                const double n = h.ctl->plainParamToNormalized(info.id, static_cast<double>(k));
                CAPTURE(info.id, info.stepCount, k, n);
                REQUIRE(n == static_cast<double>(k) / static_cast<double>(info.stepCount));
                REQUIRE(h.ctl->normalizedParamToPlain(info.id, n) == static_cast<double>(k));
            }
        }
        // 2 Phase 10 lists + 14 E lists + 6 note lists + Pitch Bend Range + 2 morph-control lists + (Phase 14) the
        // 4 morph-modulation lists (1003, 1008, 1010, 1011).
        REQUIRE(steppedParams == 29);
    }
}
