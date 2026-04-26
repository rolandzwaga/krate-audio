// ==============================================================================
// MacroMapper preset-load regression tests
// ==============================================================================
// Pin the contract that surfaced from the v0.8.0 "808 toms all sound the same
// pitch" investigation. Every test here is a guard against a SPECIFIC failure
// mode that was once shipped:
//
//   1. MacroMapper::apply must NOT clobber cfg fields with defaults+delta on
//      first apply when macros are at neutral (0.5). That was the original
//      bug: kit-preset load fired processParameterChanges -> apply for every
//      macro arrival, the -1.0 sentinel forced a first-apply, and the
//      applier wrote `cfg.size = defaults_[kPadSize] (0.5) + linDelta(0.5) =
//      0.5`, collapsing every per-pad preset size to 0.5 (mode 0 = 158 Hz
//      uniform across all six 808 toms).
//
//   2. MacroMapper::apply must layer (newDelta - oldDelta), so repeated
//      applies with the same macro values are no-ops (no drift).
//
//   3. MacroMapper::apply with a NON-neutral macro must layer the delta on
//      top of the EXISTING cfg field, not on top of `defaults_`. Pre-fix,
//      a per-pad cfg.size = 0.85 with macroBodySize = 0.7 was clobbered to
//      `0.5 + 0.08 = 0.58`; post-fix it must end at `0.85 + 0.08 = 0.93`.
//
//   4. Processor::setState must NOT call reapplyAll. That call drifted bytes
//      across save-load cycles under the new incremental contract (it would
//      re-add the macro deltas onto already-baked cfg fields). It must
//      instead sync the cache to the loaded macro values via
//      MacroMapper::syncCacheFromCfg so subsequent macro-knob movements see
//      the correct previous state.
//
//   5. The kit-preset-load path (Controller -> performEdit -> host ->
//      Processor::processParameterChanges) must preserve per-pad cfg fields
//      even when the kit's macros are non-neutral. Concretely: the
//      Controller writes macros BEFORE the per-pad fields so the per-pad
//      fields overwrite anything the macros' first apply changed; cache
//      ends up at the preset's macro values.
// ==============================================================================

#include "controller/controller_state_codec.h"
#include "dsp/pad_config.h"
#include "plugin_ids.h"
#include "processor/macro_mapper.h"
#include "processor/processor.h"
#include "state/state_codec.h"

#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace Membrum;
using namespace Membrum::MacroCurves;

namespace {

// Mirror of the helper in test_macro_mapper.cpp -- the registered defaults
// table the Controller would build from kPadParamSpecs.
RegisteredDefaultsTable makeNeutralDefaults()
{
    RegisteredDefaultsTable t{};
    t.byOffset[kPadMaterial]           = 0.5f;
    t.byOffset[kPadSize]               = 0.5f;
    t.byOffset[kPadDecay]              = 0.3f;
    t.byOffset[kPadDecaySkew]          = 0.5f;
    t.byOffset[kPadModeInjectAmount]   = 0.0f;
    t.byOffset[kPadModeStretch]        = 0.333333f;
    t.byOffset[kPadNonlinearCoupling]  = 0.0f;
    t.byOffset[kPadCouplingAmount]     = 0.5f;
    t.byOffset[kPadTSFilterCutoff]     = 1.0f;
    t.byOffset[kPadTSPitchEnvStart]    = 0.0f;
    t.byOffset[kPadTSPitchEnvEnd]      = 0.0f;
    t.byOffset[kPadTSPitchEnvTime]     = 0.0f;
    t.byOffset[kPadNoiseBurstDuration] = 0.5f;
    return t;
}

} // namespace

// ==============================================================================
// (1) Neutral macros must leave a non-default cfg field UNTOUCHED on first
// apply. This is the headline regression: every 808 tom rendered at 158 Hz
// because the fresh PadCache (-1.0 sentinel) plus neutral macros (0.5)
// triggered first-apply, which wrote `cfg.size = defaults[kPadSize] (=0.5)
// + linDelta(0.5) = 0.5`, clobbering the preset's per-tom 0.85, 0.75, 0.65,
// 0.55, 0.48, 0.40.
// ==============================================================================
TEST_CASE("Regression: neutral macros + preset-loaded cfg.size leaves cfg.size untouched",
          "[macro_mapper][regression][preset_load]")
{
    MacroMapper m;
    m.prepare(makeNeutralDefaults());

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

    m.apply(0, cfg);

    // Critical fields preserved bit-exact -- the bug was these getting
    // overwritten with defaults_.byOffset[X] + linDelta(0.5, span).
    REQUIRE_THAT(cfg.size,            WithinAbs(0.85f,  1e-7f));
    REQUIRE_THAT(cfg.material,        WithinAbs(0.18f,  1e-7f));
    REQUIRE_THAT(cfg.decay,           WithinAbs(0.65f,  1e-7f));
    REQUIRE_THAT(cfg.tsPitchEnvStart, WithinAbs(0.521f, 1e-7f));
    REQUIRE_THAT(cfg.tsPitchEnvTime,  WithinAbs(0.50f,  1e-7f));
}

// ==============================================================================
// (2) Repeated apply with the SAME macro values is a no-op (idempotent under
// re-apply). Pre-fix code was idempotent because `cfg.x = defaults + delta`
// always recomputed the same value; the new incremental contract only stays
// idempotent if (a) the cache reflects what was previously applied, and (b)
// each apply layers (newDelta - oldDelta) instead of `defaults + delta`.
// ==============================================================================
TEST_CASE("Regression: repeat apply with unchanged macros does not drift cfg",
          "[macro_mapper][regression][drift]")
{
    MacroMapper m;
    m.prepare(makeNeutralDefaults());

    PadConfig cfg{};
    cfg.size = 0.40f; // 808 high tom
    cfg.macroBodySize = 0.7f;

    m.apply(0, cfg);
    const float afterFirst = cfg.size;

    for (int i = 0; i < 100; ++i)
        m.apply(0, cfg);

    REQUIRE_THAT(cfg.size, WithinAbs(afterFirst, 1e-6f));
}

// ==============================================================================
// (3) Non-neutral macro layers its delta onto the existing cfg field. Pre-fix
// `applyBodySize` wrote `cfg.size = defaults_[kPadSize] + linDelta(0.7) =
// 0.5 + (0.7-0.5)*0.40 = 0.58`, clobbering a preset size of 0.85. Post-fix it
// must layer the delta: 0.85 + 0.08 = 0.93.
// ==============================================================================
TEST_CASE("Regression: non-neutral macro layers delta on existing cfg, not on defaults",
          "[macro_mapper][regression][delta]")
{
    MacroMapper m;
    m.prepare(makeNeutralDefaults());

    PadConfig cfg{};
    cfg.size = 0.85f;
    cfg.macroBodySize = 0.7f;

    m.apply(0, cfg);

    // 0.85 + (0.7 - 0.5) * kBodySizeSizeSpan (= 0.40) = 0.85 + 0.08 = 0.93.
    REQUIRE_THAT(cfg.size, WithinAbs(0.85f + 0.2f * kBodySizeSizeSpan, 1e-6f));
}

// ==============================================================================
// (4) After invalidateCache, the next apply re-layers the macro delta on the
// CURRENT cfg state. This is the new contract; the old "defaults+delta"
// contract was overwriting cfg with a value derived solely from the macro
// position, ignoring any user-set or preset-set value.
// ==============================================================================
TEST_CASE("Regression: invalidateCache + re-apply layers delta on current cfg",
          "[macro_mapper][regression][invalidate]")
{
    MacroMapper m;
    m.prepare(makeNeutralDefaults());

    PadConfig cfg{};
    cfg.size = 0.6f;
    cfg.macroBodySize = 0.75f;
    m.apply(0, cfg);
    const float beforeStomp = cfg.size;

    cfg.size = 0.40f; // user-stomped (e.g. preset reload)
    m.invalidateCache();
    m.apply(0, cfg);

    // Post-fix: re-apply layers the same macro_delta on top of the stomped
    // cfg.size. delta = (0.75 - 0.5) * kBodySizeSizeSpan.
    const float expectedDelta = 0.25f * kBodySizeSizeSpan;
    REQUIRE_THAT(cfg.size, WithinAbs(0.40f + expectedDelta, 1e-6f));
    // Sanity: the delta is the SAME magnitude that produced beforeStomp from
    // 0.6 (not zero, not "defaults + delta = 0.5 + delta").
    REQUIRE_THAT(beforeStomp, WithinAbs(0.6f + expectedDelta, 1e-6f));
}

// ==============================================================================
// (5) syncCacheFromCfg makes a subsequent re-apply a no-op even when macros
// are non-neutral. This is the mechanism Processor::setState now uses to
// avoid drifting cfg fields across save-load cycles.
// ==============================================================================
TEST_CASE("Regression: syncCacheFromCfg + apply with same macros is a no-op",
          "[macro_mapper][regression][sync_cache]")
{
    MacroMapper m;
    m.prepare(makeNeutralDefaults());

    PadConfig cfg{};
    cfg.size = 0.93f;            // saved post-macro effective value
    cfg.macroBodySize = 0.7f;    // matching macro position

    // Simulate Processor::setState: blob already carries consistent cfg +
    // macro values, no need to re-derive cfg from the macro.
    m.invalidateCache();
    m.syncCacheFromCfg(0, cfg);
    m.apply(0, cfg);

    REQUIRE_THAT(cfg.size, WithinAbs(0.93f, 1e-7f));
    REQUIRE_FALSE(m.isDirty(0, cfg));
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

class SingleQ : public Steinberg::Vst::IParamValueQueue
{
public:
    SingleQ(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue v)
        : id_(id), v_(v) {}
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override
    { return Steinberg::kNoInterface; }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }
    Steinberg::Vst::ParamID PLUGIN_API getParameterId() override { return id_; }
    Steinberg::int32 PLUGIN_API getPointCount() override { return 1; }
    Steinberg::tresult PLUGIN_API getPoint(Steinberg::int32, Steinberg::int32& off,
                                           Steinberg::Vst::ParamValue& v) override
    { off = 0; v = v_; return Steinberg::kResultOk; }
    Steinberg::tresult PLUGIN_API addPoint(Steinberg::int32, Steinberg::Vst::ParamValue,
                                            Steinberg::int32&) override
    { return Steinberg::kResultFalse; }
private:
    Steinberg::Vst::ParamID id_;
    Steinberg::Vst::ParamValue v_;
};

class PCList : public Steinberg::Vst::IParameterChanges
{
public:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override
    { return Steinberg::kNoInterface; }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }
    Steinberg::int32 PLUGIN_API getParameterCount() override
    { return static_cast<Steinberg::int32>(qs_.size()); }
    Steinberg::Vst::IParamValueQueue* PLUGIN_API getParameterData(Steinberg::int32 i) override
    {
        return (i < 0 || i >= static_cast<Steinberg::int32>(qs_.size()))
                   ? nullptr
                   : &qs_[static_cast<std::size_t>(i)];
    }
    Steinberg::Vst::IParamValueQueue* PLUGIN_API addParameterData(
        const Steinberg::Vst::ParamID&, Steinberg::int32&) override
    { return nullptr; }
    void add(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue v)
    { qs_.emplace_back(id, v); }
private:
    std::vector<SingleQ> qs_;
};

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
    // The MacroMapper used to clobber cfg.size on macro arrival; this test
    // pins the fixed behaviour.
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
    // applyPadSnapshotToParams sends macros BEFORE per-pad fields so the
    // per-pad fields overwrite anything the macros' first-apply changed.
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

    Steinberg::MemoryStream s1;
    REQUIRE(p.getState(&s1) == Steinberg::kResultOk);

    Membrum::Processor p2;
    preparedProcessor(p2);
    s1.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    REQUIRE(p2.setState(&s1) == Steinberg::kResultOk);

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
