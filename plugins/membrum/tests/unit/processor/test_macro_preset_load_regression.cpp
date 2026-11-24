// ==============================================================================
// Macro preset-load regression tests (rewritten for fix plan T1.22)
// ==============================================================================
// Pin the contract that surfaced from the v0.8.0 "808 toms all sound the same
// pitch" investigation, under the offset model of fix plan T1.22: a pad's
// stored fields are base values, a macro is an offset applied where the engine
// reads the pad (dsp/macro_offsets.h), and nothing ever writes a macro's delta
// into a stored field. Every test here guards a failure mode that once shipped:
//
//   1. Neutral macros (0.5) must leave every field exactly as the preset
//      placed it (the original bug collapsed every 808 tom size to 0.5).
//
//   2. Reading a pad's played config repeatedly must not drift it.
//
//   3. A NON-neutral macro offsets the pad's OWN base value, not a registered
//      default: a base size 0.85 with Body Size 0.7 plays 0.85 + 0.08 = 0.93.
//
//   4. A new base value under a non-neutral macro plays the new base plus the
//      same offset.
//
//   5. Processor::setState loads the stored base and macros verbatim; a load
//      never re-bakes a macro into a field, so save-load cycles cannot drift.
//
//   6. The kit-preset-load path (Controller -> processor) writes macros
//      before the per-pad fields (the order no longer matters, it is kept).
// ==============================================================================

#include "controller/controller_state_codec.h"
#include "dsp/macro_offsets.h"
#include "dsp/pad_config.h"
#include "plugin_ids.h"
#include "processor/processor.h"
#include "state/state_codec.h"

#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <vector>
#include "vst_param_changes.h"

using Catch::Matchers::WithinAbs;
using namespace Membrum;
using namespace Membrum::MacroCurves;

// ==============================================================================
// (1) Neutral macros leave a non-default field untouched. This is the
// headline regression: every 808 tom rendered at 158 Hz because a neutral
// macro arrival wrote `cfg.size = defaults[kPadSize] (=0.5) + linDelta(0.5)`,
// clobbering the preset's per-tom 0.85, 0.75, 0.65, 0.55, 0.48, 0.40.
// ==============================================================================
TEST_CASE("Regression: neutral macros + preset-loaded cfg.size leaves cfg.size untouched",
          "[macro_mapper][regression][preset_load]")
{
    PadConfig cfg{};
    cfg.size = 0.85f;        // simulate the 808 low tom's preset size
    cfg.material = 0.18f;    //              "        material
    cfg.decay = 0.65f;       //              "        decay
    cfg.tsPitchEnvStart = 0.521f;
    cfg.tsPitchEnvTime  = 0.50f;
    // Macros default to 0.5 (neutral) on a fresh PadConfig.

    REQUIRE(cfg.macroTightness  == 0.5f);
    REQUIRE(cfg.macroBrightness == 0.5f);
    REQUIRE(cfg.macroBodySize   == 0.5f);
    REQUIRE(cfg.macroPunch      == 0.5f);
    REQUIRE(cfg.macroComplexity == 0.5f);

    const PadConfig eff = applyMacroOffsets(cfg);

    // Critical fields preserved bit-exact.
    REQUIRE(eff.size            == 0.85f);
    REQUIRE(eff.material        == 0.18f);
    REQUIRE(eff.decay           == 0.65f);
    REQUIRE(eff.tsPitchEnvStart == 0.521f);
    REQUIRE(eff.tsPitchEnvTime  == 0.50f);
}

// ==============================================================================
// (2) Reading the played config repeatedly does not drift the pad: the
// offset is a pure function of the stored base and the macro.
// ==============================================================================
TEST_CASE("Regression: repeat apply with unchanged macros does not drift cfg",
          "[macro_mapper][regression][drift]")
{
    PadConfig cfg{};
    cfg.size = 0.40f; // 808 high tom
    cfg.macroBodySize = 0.7f;

    const float afterFirst = applyMacroOffsets(cfg).size;
    for (int i = 0; i < 100; ++i)
        REQUIRE(applyMacroOffsets(cfg).size == afterFirst);
    REQUIRE(cfg.size == 0.40f);
}

// ==============================================================================
// (3) A non-neutral macro offsets the pad's own base value. Pre-fix
// `applyBodySize` wrote `cfg.size = defaults_[kPadSize] + linDelta(0.7) =
// 0.5 + (0.7-0.5)*0.40 = 0.58`, clobbering a preset size of 0.85. It must
// play 0.85 + 0.08 = 0.93.
// ==============================================================================
TEST_CASE("Regression: non-neutral macro layers delta on existing cfg, not on defaults",
          "[macro_mapper][regression][delta]")
{
    PadConfig cfg{};
    cfg.size = 0.85f;
    cfg.macroBodySize = 0.7f;

    // 0.85 + (0.7 - 0.5) * kBodySizeSizeSpan (= 0.40) = 0.85 + 0.08 = 0.93.
    REQUIRE_THAT(applyMacroOffsets(cfg).size, WithinAbs(0.85f + 0.2f * kBodySizeSizeSpan, 1e-6f));
}

// ==============================================================================
// (4) A new base value under a non-neutral macro plays the new base plus the
// same offset (the old "invalidateCache + re-apply" contract).
// ==============================================================================
TEST_CASE("Regression: a new base under a non-neutral macro plays base + offset",
          "[macro_mapper][regression][invalidate]")
{
    PadConfig cfg{};
    cfg.size = 0.6f;
    cfg.macroBodySize = 0.75f;
    const float before = applyMacroOffsets(cfg).size;

    cfg.size = 0.40f; // user-set (e.g. preset reload)
    const float expectedDelta = 0.25f * kBodySizeSizeSpan;
    REQUIRE_THAT(applyMacroOffsets(cfg).size, WithinAbs(0.40f + expectedDelta, 1e-6f));
    // Sanity: the offset is the SAME magnitude that produced `before` from
    // 0.6 (not zero, not "defaults + delta = 0.5 + delta").
    REQUIRE_THAT(before, WithinAbs(0.6f + expectedDelta, 1e-6f));
}

// ==============================================================================
// (5) A loaded state's stored base and macros play base + offset and are not
// re-baked on load (the old syncCacheFromCfg contract).
// ==============================================================================
namespace {

void runLoadBlock(Membrum::Processor& p)
{
    constexpr int kBlock = 256;
    std::array<float, kBlock> outL{};
    std::array<float, kBlock> outR{};
    float* outChans[2] = {outL.data(), outR.data()};
    Steinberg::Vst::AudioBusBuffers outBus{};
    outBus.numChannels = 2;
    outBus.channelBuffers32 = outChans;
    Steinberg::Vst::ProcessData d{};
    d.processMode = Steinberg::Vst::kRealtime;
    d.symbolicSampleSize = Steinberg::Vst::kSample32;
    d.numSamples = kBlock;
    d.numOutputs = 1;
    d.outputs = &outBus;
    p.process(d);
}

} // namespace

TEST_CASE("Regression: a loaded base and macro are stored verbatim, not re-baked",
          "[macro_mapper][regression][sync_cache]")
{
    Membrum::State::KitSnapshot kit;
    std::array<PadConfig, kNumPads> pads{};
    pads[0].size = 0.83f;            // stored base
    pads[0].macroBodySize = 0.7f;    // macro position
    for (int i = 0; i < kNumPads; ++i)
        kit.pads[static_cast<std::size_t>(i)] =
            Membrum::State::toPadSnapshot(pads[static_cast<std::size_t>(i)]);
    Steinberg::MemoryStream blob;
    REQUIRE(Membrum::State::writeKitBlob(&blob, kit) == Steinberg::kResultOk);

    Membrum::Processor p;
    p.initialize(nullptr);
    Steinberg::Vst::ProcessSetup ps{};
    ps.processMode = Steinberg::Vst::kRealtime;
    ps.symbolicSampleSize = Steinberg::Vst::kSample32;
    ps.maxSamplesPerBlock = 256;
    ps.sampleRate = 48000.0;
    p.setupProcessing(ps);
    p.setActive(true);
    blob.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    REQUIRE(p.setState(&blob) == Steinberg::kResultOk);
    for (int i = 0; i < 4; ++i)
        runLoadBlock(p);

    const auto& cfg = p.voicePoolForTest().padConfig(0);
    REQUIRE(cfg.size == 0.83f);
    REQUIRE(cfg.macroBodySize == 0.7f);
    REQUIRE_THAT(applyMacroOffsets(cfg).size, WithinAbs(0.83f + 0.2f * kBodySizeSizeSpan, 1e-6f));

    p.setActive(false);
    p.terminate();
}

// ==============================================================================
// (6) Ordering invariant: applyPadSnapshotToParams writes macros BEFORE the
// per-pad fields. Pre-fix order (per-pad fields first, macros last) drifted
// cfg fields by one macro_delta on every save-load cycle for any non-neutral
// macro. We capture the order of setter() invocations and assert that every
// macro id is written before its targeted per-pad ids.
// ==============================================================================
TEST_CASE("Regression: applyPadSnapshotToParams writes macros before per-pad fields",
          "[controller][regression][param_order]")
{
    Membrum::State::PadSnapshot snap;
    snap.exciterType = ExciterType::Mallet;
    snap.bodyModel   = BodyModelType::Membrane;
    // Sound block: 0..27 contiguous, 28..29 choke/bus, 30..33 exciter sec,
    // 34..51 Phase 7+ slots. Filling with sentinel positions doesn't matter
    // for an ordering check; we just need the call to run without crashing.
    for (auto& s : snap.sound) s = 0.5;
    snap.couplingAmount = 0.5;
    for (auto& mv : snap.macros) mv = 0.5;
    snap.chokeGroup = 0;
    snap.outputBus  = 0;

    std::vector<Steinberg::Vst::ParamID> writeOrder;
    Membrum::ControllerState::ParamSetter capture =
        [&writeOrder](Steinberg::Vst::ParamID id, double) {
            writeOrder.push_back(id);
        };

    Membrum::ControllerState::applyPadSnapshotToParams(0, snap, capture);

    auto indexOf = [&writeOrder](int padOff) {
        const auto target = static_cast<Steinberg::Vst::ParamID>(padParamId(0, padOff));
        for (std::size_t i = 0; i < writeOrder.size(); ++i)
            if (writeOrder[i] == target) return static_cast<int>(i);
        return -1;
    };

    // Per-pad fields that ANY macro writes (must arrive AFTER all macros so
    // the per-pad write overwrites the macro-computed intermediate value).
    const int macroTargets[] = {
        kPadMaterial, kPadSize, kPadDecay, kPadDecaySkew,
        kPadModeStretch, kPadModeInjectAmount, kPadNonlinearCoupling,
        kPadCouplingAmount, kPadTSFilterCutoff,
        kPadTSPitchEnvStart, kPadTSPitchEnvTime,
        kPadNoiseBurstDuration,
    };
    const int macroIds[] = {
        kPadMacroTightness, kPadMacroBrightness, kPadMacroBodySize,
        kPadMacroPunch,     kPadMacroComplexity,
    };

    int lastMacroIdx = -1;
    for (int macroOff : macroIds) {
        const int idx = indexOf(macroOff);
        REQUIRE(idx >= 0);
        if (idx > lastMacroIdx) lastMacroIdx = idx;
    }
    REQUIRE(lastMacroIdx >= 0);

    int firstTargetIdx = static_cast<int>(writeOrder.size());
    for (int targetOff : macroTargets) {
        const int idx = indexOf(targetOff);
        if (idx < 0) continue; // not all targets are surfaced as direct params
        if (idx < firstTargetIdx) firstTargetIdx = idx;
    }

    INFO("Last macro write index = " << lastMacroIdx
         << ", first macro-target write index = " << firstTargetIdx);
    REQUIRE(lastMacroIdx < firstTargetIdx);
}

// ==============================================================================
// (7) End-to-end: drive `Processor::processParameterChanges` with the same
// dispatch order the host uses on a kit-preset load. Per-pad cfg fields must
// survive the macro arrivals, including for non-neutral macros.
// ==============================================================================
namespace {

// Parameter-change mocks consolidated into tests/test_helpers/vst_param_changes.h
using SingleQ = Krate::Test::ParamValueQueue;
using PCList = Krate::Test::ParameterChanges;




void runEmptyBlock(Membrum::Processor& p,
                   Steinberg::Vst::IParameterChanges* pc)
{
    constexpr int kBlock = 256;
    std::array<float, kBlock> outL{};
    std::array<float, kBlock> outR{};
    float* outChans[2] = {outL.data(), outR.data()};
    Steinberg::Vst::AudioBusBuffers outBus{};
    outBus.numChannels = 2;
    outBus.channelBuffers32 = outChans;
    Steinberg::Vst::ProcessData d{};
    d.processMode = Steinberg::Vst::kRealtime;
    d.symbolicSampleSize = Steinberg::Vst::kSample32;
    d.numSamples = kBlock;
    d.numOutputs = 1;
    d.outputs = &outBus;
    d.inputParameterChanges = pc;
    p.process(d);
}

Membrum::Processor& preparedProcessor(Membrum::Processor& p)
{
    p.initialize(nullptr);
    Steinberg::Vst::ProcessSetup ps{};
    ps.processMode = Steinberg::Vst::kRealtime;
    ps.symbolicSampleSize = Steinberg::Vst::kSample32;
    ps.maxSamplesPerBlock = 256;
    ps.sampleRate = 48000.0;
    p.setupProcessing(ps);
    p.setActive(true);
    return p;
}

} // namespace

TEST_CASE("Regression: kit-preset-load via processParameterChanges preserves per-pad cfg.size "
          "(neutral macros)",
          "[processor][regression][preset_load][param_dispatch]")
{
    // The exact sequence the host performs when the user loads the 808 kit:
    // - Per-pad fields written first via performEdit (kPadSize, etc.)
    // - Macros written last (all neutral 0.5).
    // A macro arrival once clobbered cfg.size; this test pins the fixed
    // behaviour (since T1.22 a macro never writes a field).
    Membrum::Processor p;
    preparedProcessor(p);

    auto pid = [](int padIdx, int off) {
        return static_cast<Steinberg::Vst::ParamID>(Membrum::padParamId(padIdx, off));
    };

    // Six toms exactly as the 808 preset writes them.
    const int   tomPads[6] = {5, 7, 9, 11, 12, 14};
    const float tomSize[6] = {0.85f, 0.75f, 0.65f, 0.55f, 0.48f, 0.40f};

    PCList pc;
    for (int i = 0; i < 6; ++i) {
        pc.add(pid(tomPads[i], kPadSize),     tomSize[i]);
        pc.add(pid(tomPads[i], kPadMaterial), 0.18 + i * 0.08);
    }
    // Macros at neutral 0.5 (the 808 preset's actual state).
    for (int i = 0; i < 6; ++i) {
        pc.add(pid(tomPads[i], kPadMacroTightness),  0.5);
        pc.add(pid(tomPads[i], kPadMacroBrightness), 0.5);
        pc.add(pid(tomPads[i], kPadMacroBodySize),   0.5);
        pc.add(pid(tomPads[i], kPadMacroPunch),      0.5);
        pc.add(pid(tomPads[i], kPadMacroComplexity), 0.5);
    }
    runEmptyBlock(p, static_cast<Steinberg::Vst::IParameterChanges*>(&pc));

    for (int i = 0; i < 6; ++i) {
        const auto& cfg = p.voicePoolForTest().padConfig(tomPads[i]);
        INFO("tom" << (i+1) << " (pad " << tomPads[i] << "): cfg.size=" << cfg.size);
        REQUIRE_THAT(cfg.size, WithinAbs(tomSize[i], 1e-6f));
    }

    p.setActive(false);
    p.terminate();
}

TEST_CASE("Regression: kit-preset-load via processParameterChanges preserves per-pad cfg.size "
          "(non-neutral macros, host-correct ordering)",
          "[processor][regression][preset_load][param_dispatch][drift]")
{
    // applyPadSnapshotToParams sends macros BEFORE per-pad fields.
    // This test reproduces that order and verifies cfg.size lands at the
    // saved preset value, not at `defaults + macro_delta`.
    Membrum::Processor p;
    preparedProcessor(p);

    auto pid = [](int padIdx, int off) {
        return static_cast<Steinberg::Vst::ParamID>(Membrum::padParamId(padIdx, off));
    };

    PCList pc;
    // Macros first (non-neutral).
    pc.add(pid(0, kPadMacroBodySize),  0.7);
    pc.add(pid(0, kPadMacroTightness), 0.3);
    // Per-pad fields second.
    pc.add(pid(0, kPadSize),     0.85);
    pc.add(pid(0, kPadMaterial), 0.18);
    pc.add(pid(0, kPadDecay),    0.65);
    runEmptyBlock(p, static_cast<Steinberg::Vst::IParameterChanges*>(&pc));

    const auto& cfg = p.voicePoolForTest().padConfig(0);
    REQUIRE_THAT(cfg.size,     WithinAbs(0.85f, 1e-6f));
    REQUIRE_THAT(cfg.material, WithinAbs(0.18f, 1e-6f));
    REQUIRE_THAT(cfg.decay,    WithinAbs(0.65f, 1e-6f));

    p.setActive(false);
    p.terminate();
}

// ==============================================================================
// (8) Save-load-save byte-exact: a kit blob with non-neutral macros must
// survive round-trip without drift. This was specifically broken pre-fix:
// load called reapplyAll which re-wrote cfg.x = defaults + delta on top of
// the loaded cfg.x, then save serialised the doubled value, then next load
// shifted again, etc.
// ==============================================================================
TEST_CASE("Regression: Processor::setState round-trip is byte-exact with non-neutral macros",
          "[processor][regression][round_trip][drift]")
{
    Membrum::Processor p;
    preparedProcessor(p);

    auto& pads = p.voicePoolForTest().padConfigsArray();
    pads[3].size = 0.85f;
    pads[3].material = 0.30f;
    pads[3].decay = 0.55f;
    pads[3].macroBodySize = 0.7f;
    pads[3].macroTightness = 0.3f;
    pads[3].macroComplexity = 0.65f;
    // MB-001: the pad was written straight into the engine; republish it for
    // getState.
    p.publishPadConfigsForTest();

    Steinberg::MemoryStream s1;
    REQUIRE(p.getState(&s1) == Steinberg::kResultOk);

    Membrum::Processor p2;
    preparedProcessor(p2);
    s1.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    REQUIRE(p2.setState(&s1) == Steinberg::kResultOk);
    // MB-001: a load on an active processor applies in the next block.
    runEmptyBlock(p2, nullptr);

    Steinberg::MemoryStream s2;
    REQUIRE(p2.getState(&s2) == Steinberg::kResultOk);

    // Compare blob bytes. Pre-fix this CHECK would fail because the second
    // setState re-applied macro deltas on top of the loaded post-macro values,
    // producing a different blob on the next save.
    Steinberg::int64 sz1 = 0, sz2 = 0;
    s1.seek(0, Steinberg::IBStream::kIBSeekEnd, &sz1);
    s2.seek(0, Steinberg::IBStream::kIBSeekEnd, &sz2);
    REQUIRE(sz1 == sz2);

    s1.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    s2.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    std::vector<std::uint8_t> b1(static_cast<std::size_t>(sz1));
    std::vector<std::uint8_t> b2(static_cast<std::size_t>(sz2));
    Steinberg::int32 g = 0;
    s1.read(b1.data(), static_cast<Steinberg::int32>(sz1), &g);
    s2.read(b2.data(), static_cast<Steinberg::int32>(sz2), &g);
    REQUIRE(b1 == b2);

    // Per-pad fields landed where they should (no drift).
    const auto& cfg = p2.voicePoolForTest().padConfig(3);
    REQUIRE_THAT(cfg.size,           WithinAbs(0.85f, 1e-6f));
    REQUIRE_THAT(cfg.material,       WithinAbs(0.30f, 1e-6f));
    REQUIRE_THAT(cfg.decay,          WithinAbs(0.55f, 1e-6f));
    REQUIRE_THAT(cfg.macroBodySize,  WithinAbs(0.7f,  1e-7f));
    REQUIRE_THAT(cfg.macroTightness, WithinAbs(0.3f,  1e-7f));
    REQUIRE_THAT(cfg.macroComplexity,WithinAbs(0.65f, 1e-7f));

    p.setActive(false);
    p.terminate();
    p2.setActive(false);
    p2.terminate();
}
