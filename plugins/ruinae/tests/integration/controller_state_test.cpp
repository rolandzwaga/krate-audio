// ==============================================================================
// Integration Test: Controller State Synchronization
// ==============================================================================
// Verifies that Controller::setComponentState() synchronizes all parameters
// to match the Processor state stream.
//
// Reference: specs/045-plugin-shell/spec.md FR-012, US4
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "processor/processor.h"
#include "controller/controller.h"
#include "plugin_ids.h"

#include "public.sdk/source/common/memorystream.h"
#include "base/source/fstreamer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>
#include "vst_param_changes.h"

#include "unit/controller/view_lifecycle_rig.h"
#include "controller/view_sync_rules.h"
#include "parameters/dropdown_mappings.h"
#include "ui/arp_condition_lane.h"
#include "ui/chaos_mod_display.h"
#include <krate/dsp/core/note_value.h>
#include <krate/dsp/primitives/polyblep_oscillator.h>

using Catch::Approx;

// =============================================================================
// Helpers
// =============================================================================

static std::unique_ptr<Ruinae::Processor> makeProcessor() {
    auto p = std::make_unique<Ruinae::Processor>();
    p->initialize(nullptr);

    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = 512;
    p->setupProcessing(setup);

    return p;
}

static Ruinae::Controller* makeControllerRaw() {
    auto* ctrl = new Ruinae::Controller();
    ctrl->initialize(nullptr);
    return ctrl;
}

// Subclass to expose protected processParameterChanges for testing
class TestableProcessor : public Ruinae::Processor {
public:
    using Ruinae::Processor::processParameterChanges;
};

// Mock single-value parameter queue for feeding parameter changes
// Parameter-change mocks consolidated into tests/test_helpers/vst_param_changes.h
using StateTestParamQueue = Krate::Test::ParamValueQueue;
using StateTestParamChanges = Krate::Test::ParameterChanges;


// Mock parameter changes container


// =============================================================================
// Tests
// =============================================================================

TEST_CASE("Controller syncs default state from Processor", "[controller_state][integration]") {
    auto proc = makeProcessor();
    auto* ctrl = makeControllerRaw();

    // Save processor state
    Steinberg::MemoryStream stream;
    REQUIRE(proc->getState(&stream) == Steinberg::kResultTrue);

    // Sync controller
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    REQUIRE(ctrl->setComponentState(&stream) == Steinberg::kResultTrue);

    // Verify key default parameters are synced
    // Master Gain default = 1.0 -> normalized 0.5
    CHECK(ctrl->getParamNormalized(Ruinae::kMasterGainId) == Approx(0.5).margin(0.01));

    // Polyphony default = 8 -> normalized (8-1)/15 = 7/15
    CHECK(ctrl->getParamNormalized(Ruinae::kPolyphonyId) == Approx(7.0 / 15.0).margin(0.01));

    // Soft Limit default = true -> 1.0
    CHECK(ctrl->getParamNormalized(Ruinae::kSoftLimitId) == Approx(1.0).margin(0.01));

    // OSC A Level default = 1.0
    CHECK(ctrl->getParamNormalized(Ruinae::kOscALevelId) == Approx(1.0).margin(0.01));

    // Amp Sustain default = 0.8
    CHECK(ctrl->getParamNormalized(Ruinae::kAmpEnvSustainId) == Approx(0.8).margin(0.01));

    proc->terminate();
    ctrl->terminate();
}

TEST_CASE("Controller syncs non-default state from Processor", "[controller_state][integration]") {
    // Create a processor with test-accessible parameter changes
    auto proc = std::make_unique<TestableProcessor>();
    proc->initialize(nullptr);

    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = 512;
    proc->setupProcessing(setup);

    // Set non-default parameter values through the VST3 parameter change path.
    // All values are NORMALIZED (0.0 to 1.0) at the VST boundary.
    StateTestParamChanges paramChanges;

    // Master Gain 1.5 -> normalized 1.5/2.0 = 0.75
    paramChanges.addChange(Ruinae::kMasterGainId, 0.75);

    // Voice Mode = 1 (Mono) -> normalized 1.0
    paramChanges.addChange(Ruinae::kVoiceModeId, 1.0);

    // Polyphony = 4 -> normalized (4-1)/15 = 0.2
    paramChanges.addChange(Ruinae::kPolyphonyId, 3.0 / 15.0);

    // OSC A Level = 0.7
    paramChanges.addChange(Ruinae::kOscALevelId, 0.7);

    // Filter cutoff 1000.0 Hz -> normalized = log(1000/20)/log(1000)
    double cutoffNorm = std::log(1000.0 / 20.0) / std::log(1000.0);
    paramChanges.addChange(Ruinae::kFilterCutoffId, cutoffNorm);

    // Apply changes through the processor's parameter handling
    proc->processParameterChanges(&paramChanges);

    // Save the processor's state (in current format, always correct)
    Steinberg::MemoryStream stream;
    REQUIRE(proc->getState(&stream) == Steinberg::kResultTrue);

    // Sync controller with this state
    auto* ctrl = makeControllerRaw();
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    REQUIRE(ctrl->setComponentState(&stream) == Steinberg::kResultTrue);

    // Verify the non-default values are synced
    CHECK(ctrl->getParamNormalized(Ruinae::kMasterGainId) == Approx(0.75).margin(0.01));
    CHECK(ctrl->getParamNormalized(Ruinae::kVoiceModeId) == Approx(1.0).margin(0.01));
    CHECK(ctrl->getParamNormalized(Ruinae::kPolyphonyId) == Approx(3.0 / 15.0).margin(0.01));
    CHECK(ctrl->getParamNormalized(Ruinae::kOscALevelId) == Approx(0.7).margin(0.01));
    CHECK(ctrl->getParamNormalized(Ruinae::kFilterCutoffId) == Approx(cutoffNorm).margin(0.02));

    proc->terminate();
    ctrl->terminate();
}

TEST_CASE("Controller handles empty stream in setComponentState", "[controller_state][integration]") {
    auto* ctrl = makeControllerRaw();
    Steinberg::MemoryStream emptyStream;

    // Should not crash and return kResultTrue
    auto result = ctrl->setComponentState(&emptyStream);
    CHECK(result == Steinberg::kResultTrue);

    ctrl->terminate();
}

TEST_CASE("Controller handles null stream in setComponentState", "[controller_state][integration]") {
    auto* ctrl = makeControllerRaw();

    // Null stream should return kResultFalse
    auto result = ctrl->setComponentState(nullptr);
    CHECK(result == Steinberg::kResultFalse);

    ctrl->terminate();
}

TEST_CASE("Controller round-trip: Processor save -> Controller load", "[controller_state][integration]") {
    auto proc = makeProcessor();
    auto* ctrl = makeControllerRaw();

    // Save default processor state
    Steinberg::MemoryStream stream;
    REQUIRE(proc->getState(&stream) == Steinberg::kResultTrue);

    // Load into controller
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    REQUIRE(ctrl->setComponentState(&stream) == Steinberg::kResultTrue);

    // All parameters should have valid normalized values (0.0-1.0)
    Steinberg::int32 paramCount = ctrl->getParameterCount();
    for (Steinberg::int32 i = 0; i < paramCount; ++i) {
        Steinberg::Vst::ParameterInfo info{};
        ctrl->getParameterInfo(i, info);
        double norm = ctrl->getParamNormalized(info.id);
        INFO("Parameter " << info.id << " has normalized value " << norm);
        CHECK(norm >= 0.0);
        CHECK(norm <= 1.0);
    }

    proc->terminate();
    ctrl->terminate();
}

// =============================================================================
// Legacy states load the same values on every path (R-075, R-096, R-074)
// =============================================================================
// A state older than a field does not carry it. Whichever path loads the state
// (host: Processor::setState + Controller::setComponentState; preset browser:
// Controller::loadComponentStateWithNotify, which reaches the processor only
// through performEdit), that field must take its default, never the value the
// previously loaded preset left behind.

namespace {

std::vector<char> streamBytes(Steinberg::MemoryStream& stream) {
    return {stream.getData(), stream.getData() + stream.getSize()};
}

std::vector<char> processorStateBytes(Ruinae::Processor& proc) {
    Steinberg::MemoryStream stream;
    REQUIRE(proc.getState(&stream) == Steinberg::kResultTrue);
    return streamBytes(stream);
}

/// Byte offsets of the fields older state versions never wrote, read from a
/// current (v8) state by walking its packs.
struct LegacyStateLayout {
    Steinberg::int64 tranceGateStart = 0;  ///< first byte of the trance-gate pack
    Steinberg::int64 tranceGateEnd = 0;    ///< one past its last byte (retrigger depth, marker v3)
    Steinberg::int64 reverbTypeStart = 0;  ///< int32 reverb type (version 5+)
    Steinberg::int64 flangerStart = 0;    ///< flanger pack (version 6+), chorus follows (version 7+)
    Steinberg::int64 chorusEnd = 0;
    Steinberg::int64 total = 0;
};

LegacyStateLayout layoutOf(std::vector<char>& v8) {
    Steinberg::MemoryStream stream(v8.data(), static_cast<Steinberg::TSize>(v8.size()));
    Steinberg::IBStreamer s(&stream, kLittleEndian);
    LegacyStateLayout l;
    l.total = static_cast<Steinberg::int64>(v8.size());

    Steinberg::int32 version = 0;
    REQUIRE(s.readInt32(version));
    REQUIRE(version == 9);  // v9 writes the v8 layout (it changed the meaning of Spectral Shift)
    Ruinae::GlobalParams g; REQUIRE(loadGlobalParams(g, s));
    Ruinae::OscAParams oa; REQUIRE(loadOscAParams(oa, s));
    Ruinae::OscBParams ob; REQUIRE(loadOscBParams(ob, s));
    Ruinae::MixerParams mx; REQUIRE(loadMixerParams(mx, s));
    Ruinae::RuinaeFilterParams f; REQUIRE(loadFilterParams(f, s));
    Ruinae::RuinaeDistortionParams d; REQUIRE(loadDistortionParams(d, s));
    l.tranceGateStart = s.tell();
    Ruinae::RuinaeTranceGateParams tg; REQUIRE(loadTranceGateParams(tg, s));
    l.tranceGateEnd = s.tell();
    Ruinae::AmpEnvParams ae; REQUIRE(loadAmpEnvParams(ae, s));
    Ruinae::FilterEnvParams fe; REQUIRE(loadFilterEnvParams(fe, s));
    Ruinae::ModEnvParams me; REQUIRE(loadModEnvParams(me, s));
    Ruinae::LFO1Params l1; REQUIRE(loadLFO1Params(l1, s));
    Ruinae::LFO2Params l2; REQUIRE(loadLFO2Params(l2, s));
    Ruinae::ChaosModParams cm; REQUIRE(loadChaosModParams(cm, s));
    Ruinae::ModMatrixParams mm; REQUIRE(loadModMatrixParams(mm, s));
    Ruinae::GlobalFilterParams gf; REQUIRE(loadGlobalFilterParams(gf, s));
    Ruinae::RuinaeDelayParams dl; REQUIRE(loadDelayParams(dl, s));
    Ruinae::RuinaeReverbParams rv; REQUIRE(loadReverbParams(rv, s));
    l.reverbTypeStart = s.tell();
    Steinberg::int32 reverbType = 0; REQUIRE(s.readInt32(reverbType));
    Ruinae::MonoModeParams mono; REQUIRE(loadMonoModeParams(mono, s));
    for (int i = 0; i < 16; ++i) {  // voice routes
        Steinberg::int8 i8 = 0; float fl = 0.0f;
        REQUIRE(s.readInt8(i8)); REQUIRE(s.readInt8(i8)); REQUIRE(s.readFloat(fl));
        REQUIRE(s.readInt8(i8)); REQUIRE(s.readFloat(fl)); REQUIRE(s.readInt8(i8));
        REQUIRE(s.readInt8(i8)); REQUIRE(s.readInt8(i8));
    }
    Steinberg::int8 flag = 0;
    REQUIRE(s.readInt8(flag));  // delay enabled
    REQUIRE(s.readInt8(flag));  // reverb enabled
    Ruinae::RuinaePhaserParams ph; REQUIRE(loadPhaserParams(ph, s));
    REQUIRE(s.readInt8(flag));  // modulation type
    l.flangerStart = s.tell();
    Ruinae::RuinaeFlangerParams fl; REQUIRE(loadFlangerParams(fl, s));
    Ruinae::RuinaeChorusParams ch; REQUIRE(loadChorusParams(ch, s));
    l.chorusEnd = s.tell();
    return l;
}

/// Arp pack tail of a current state: chord lane (length + 32), inversion lane
/// (length + 32) and voicing mode are int32s written from version 4; the eight
/// lane speeds are floats written from version 8. After the arp pack, the
/// Voice LFO rate (float) and shape (int32) and the Velocity > Amp amount
/// (float), written from version 9, end the state.
constexpr Steinberg::int64 kArpLaneSpeedBytes = 8 * 4;
constexpr Steinberg::int64 kArpChordBlockBytes = (1 + 32 + 1 + 32 + 1) * 4;
constexpr Steinberg::int64 kV9TailBytes = 4 + 4 + 4;

/// A genuine older state: the current bytes minus every field `version` never
/// wrote, with the version patched. v5 also carries a marker-2 trance-gate pack
/// (no retrigger depth) so the trance-gate legacy reset is exercised too.
std::vector<char> legacyStateFrom(std::vector<char> v8, Steinberg::int32 version) {
    const LegacyStateLayout l = layoutOf(v8);
    std::vector<std::pair<Steinberg::int64, Steinberg::int64>> cut;  // [begin, end)
    const Steinberg::int64 arpEnd = l.total - kV9TailBytes;
    cut.emplace_back(arpEnd, l.total);                               // voice LFO, Velocity > Amp (v9)
    cut.emplace_back(arpEnd - kArpLaneSpeedBytes, arpEnd);           // lane speeds (v8)
    if (version < 7)
        cut.emplace_back(l.flangerStart, l.chorusEnd);               // flanger (v6), chorus (v7)
    if (version < 6) {
        cut.emplace_back(l.tranceGateEnd - 4, l.tranceGateEnd);      // trance-gate retrigger depth
        const Steinberg::int32 marker2 = 2;  // after 8 int32/float fields
        std::memcpy(v8.data() + l.tranceGateStart + 32, &marker2, sizeof(marker2));
    }
    if (version < 5)
        cut.emplace_back(l.reverbTypeStart, l.reverbTypeStart + 4);  // reverb type (v5)
    if (version < 4)
        cut.emplace_back(arpEnd - kArpLaneSpeedBytes - kArpChordBlockBytes,
                         arpEnd - kArpLaneSpeedBytes);               // chord/inversion/voicing (v4)

    std::vector<char> out;
    for (Steinberg::int64 i = 0; i < l.total; ++i) {
        bool keep = true;
        for (const auto& [b, e] : cut)
            keep = keep && (i < b || i >= e);
        if (keep)
            out.push_back(v8[static_cast<size_t>(i)]);
    }
    std::memcpy(out.data(), &version, sizeof(version));
    return out;
}

/// Forwards every performEdit of the controller to the processor, as a host does.
class EditForwardingHandler final : public Steinberg::Vst::IComponentHandler {
public:
    Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID) override { return Steinberg::kResultOk; }
    Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID id,
                                              Steinberg::Vst::ParamValue value) override {
        edits.addChange(id, value);
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID) override { return Steinberg::kResultOk; }
    Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32) override { return Steinberg::kResultOk; }
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void** obj) override {
        *obj = nullptr;
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

    Krate::Test::ParameterChanges edits;
};

class BrowserLoadController : public Ruinae::Controller {
public:
    using Ruinae::Controller::loadComponentStateWithNotify;
};

std::unique_ptr<TestableProcessor> makeTestableProcessor() {
    auto p = std::make_unique<TestableProcessor>();
    p->initialize(nullptr);
    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = 512;
    p->setupProcessing(setup);
    return p;
}

void loadIntoProcessor(Ruinae::Processor& proc, std::vector<char> bytes) {
    Steinberg::MemoryStream stream(bytes.data(), static_cast<Steinberg::TSize>(bytes.size()));
    REQUIRE(proc.setState(&stream) == Steinberg::kResultTrue);
}

void loadIntoController(Ruinae::Controller& ctrl, std::vector<char> bytes) {
    Steinberg::MemoryStream stream(bytes.data(), static_cast<Steinberg::TSize>(bytes.size()));
    REQUIRE(ctrl.setComponentState(&stream) == Steinberg::kResultTrue);
}

double registeredDefault(Ruinae::Controller& ctrl, Steinberg::Vst::ParamID id) {
    auto* p = ctrl.getParameterObject(id);
    REQUIRE(p != nullptr);
    return p->getInfo().defaultNormalizedValue;
}

/// The legacy-gated parameters checked against their registered defaults (lane
/// speeds are checked as 1.0x through the processor's map instead).
std::vector<Steinberg::Vst::ParamID> legacyGatedParamIds() {
    std::vector<Steinberg::Vst::ParamID> ids;
    for (Steinberg::Vst::ParamID id = Ruinae::kFlangerRateId; id <= Ruinae::kFlangerNoteValueId; ++id)
        ids.push_back(id);
    for (Steinberg::Vst::ParamID id = Ruinae::kChorusRateId; id <= Ruinae::kChorusNoteValueId; ++id)
        ids.push_back(id);
    ids.push_back(Ruinae::kArpChordLaneLengthId);
    ids.push_back(Ruinae::kArpInversionLaneLengthId);
    ids.push_back(Ruinae::kArpVoicingModeId);
    for (Steinberg::Vst::ParamID i = 0; i < 32; ++i) {
        ids.push_back(Ruinae::kArpChordLaneStep0Id + i);
        ids.push_back(Ruinae::kArpInversionLaneStep0Id + i);
    }
    ids.push_back(Ruinae::kTranceGateRetriggerDepthId);
    ids.push_back(Ruinae::kVoiceLfoRateId);
    ids.push_back(Ruinae::kVoiceLfoShapeId);
    return ids;
}

} // namespace

TEST_CASE("Legacy state loads identical values via setState and via the browser path",
          "[state][migration][controller]") {
    const Steinberg::int32 version = GENERATE(3, 5, 7);
    INFO("legacy state version " << version);

    // The legacy state: a fresh instance's state cut down to the older format.
    auto fresh = makeTestableProcessor();
    const std::vector<char> defaultBytes = processorStateBytes(*fresh);
    const std::vector<char> legacy = legacyStateFrom(defaultBytes, version);
    REQUIRE(legacy.size() < defaultBytes.size());

    // The previously loaded preset: non-default flanger, chorus, trance-gate
    // retrigger depth, lane speeds 2x, chord/inversion lane data and a ~21 Hz
    // square Voice LFO.
    std::vector<char> previous;
    {
        auto p = makeTestableProcessor();
        Krate::Test::ParameterChanges c;
        c.addChange(Ruinae::kVoiceLfoRateId, 0.9);
        c.addChange(Ruinae::kVoiceLfoShapeId, 3.0 / 5.0);
        for (Steinberg::Vst::ParamID id = Ruinae::kFlangerRateId; id <= Ruinae::kFlangerNoteValueId; ++id)
            c.addChange(id, 0.9);
        for (Steinberg::Vst::ParamID id = Ruinae::kChorusRateId; id <= Ruinae::kChorusNoteValueId; ++id)
            c.addChange(id, 0.9);
        c.addChange(Ruinae::kFlangerWaveformId, 0.0);
        c.addChange(Ruinae::kChorusWaveformId, 0.0);
        c.addChange(Ruinae::kModulationTypeId, 1.0);  // Chorus
        c.addChange(Ruinae::kTranceGateRetriggerDepthId, 0.7);
        for (Steinberg::Vst::ParamID id = Ruinae::kArpVelocityLaneSpeedId; id <= Ruinae::kArpInversionLaneSpeedId; ++id)
            c.addChange(id, 7.0 / 9.0);  // 2x
        c.addChange(Ruinae::kArpChordLaneLengthId, 7.0 / 31.0);
        c.addChange(Ruinae::kArpInversionLaneLengthId, 7.0 / 31.0);
        c.addChange(Ruinae::kArpVoicingModeId, 2.0 / 3.0);
        for (Steinberg::Vst::ParamID i = 0; i < 8; ++i) {
            c.addChange(Ruinae::kArpChordLaneStep0Id + i, 0.5);
            c.addChange(Ruinae::kArpInversionLaneStep0Id + i, 1.0 / 3.0);
        }
        p->processParameterChanges(&c);
        previous = processorStateBytes(*p);
        p->terminate();
    }
    REQUIRE(previous != defaultBytes);

    // (a) host path: Processor::setState + Controller::setComponentState
    auto procA = makeTestableProcessor();
    auto* ctrlA = new BrowserLoadController();
    ctrlA->initialize(nullptr);
    loadIntoProcessor(*procA, previous);
    loadIntoController(*ctrlA, previous);
    loadIntoProcessor(*procA, legacy);
    loadIntoController(*ctrlA, legacy);

    // (b) browser path: the controller loads the file and performEdits each
    // parameter; the host forwards the edits to the processor.
    // (c) the same browser load into a fresh pair, as the reference for (b).
    struct Pair {
        std::unique_ptr<TestableProcessor> proc = makeTestableProcessor();
        BrowserLoadController* ctrl = new BrowserLoadController();
        EditForwardingHandler host;
    };
    auto browserLoad = [&legacy](Pair& pair) {
        pair.ctrl->setComponentHandler(&pair.host);
        std::vector<char> bytes = legacy;
        Steinberg::MemoryStream stream(bytes.data(), static_cast<Steinberg::TSize>(bytes.size()));
        REQUIRE(pair.ctrl->loadComponentStateWithNotify(&stream));
        pair.proc->processParameterChanges(&pair.host.edits);
    };
    Pair b;
    b.ctrl->initialize(nullptr);
    loadIntoProcessor(*b.proc, previous);
    loadIntoController(*b.ctrl, previous);
    browserLoad(b);
    Pair c;
    c.ctrl->initialize(nullptr);
    browserLoad(c);
    auto* ctrlB = b.ctrl;

    for (auto* ctrl : {static_cast<Ruinae::Controller*>(ctrlA), static_cast<Ruinae::Controller*>(ctrlB)}) {
        INFO((ctrl == ctrlA ? "host path controller" : "browser path controller"));
        for (const auto id : legacyGatedParamIds()) {
            INFO("param " << id);
            CHECK(ctrl->getParamNormalized(id) == Approx(registeredDefault(*ctrl, id)).margin(1e-6));
        }
        // Lane speeds: 1.0x through the processor's own map (handleArpParamChange).
        for (Steinberg::Vst::ParamID id = Ruinae::kArpVelocityLaneSpeedId; id <= Ruinae::kArpInversionLaneSpeedId; ++id) {
            INFO("lane speed param " << id);
            const int idx = static_cast<int>(std::round(ctrl->getParamNormalized(id) * (Ruinae::kLaneSpeedCount - 1)));
            CHECK(Ruinae::kLaneSpeedValues[std::clamp(idx, 0, Ruinae::kLaneSpeedCount - 1)] == 1.0f);
        }
    }

    const std::vector<char> bytesA = processorStateBytes(*procA);
    const std::vector<char> bytesB = processorStateBytes(*b.proc);
    const std::vector<char> bytesC = processorStateBytes(*c.proc);
    REQUIRE(bytesA.size() == defaultBytes.size());
    REQUIRE(bytesB.size() == defaultBytes.size());

    // Processor lane speeds: the eight floats that end the arp pack are all 1.0x.
    for (const auto& [name, bytes] : {std::pair{"host path", &bytesA}, std::pair{"browser path", &bytesB}}) {
        INFO(name << " processor");
        for (size_t i = 0; i < 8; ++i) {
            float speed = 0.0f;
            std::memcpy(&speed, bytes->data() + bytes->size() - kV9TailBytes - kArpLaneSpeedBytes + 4 * i,
                        sizeof(speed));
            CHECK(speed == 1.0f);
        }
    }

    const auto firstDifference = [](const std::vector<char>& x, const std::vector<char>& y) {
        for (size_t i = 0; i < std::min(x.size(), y.size()); ++i)
            if (x[i] != y[i]) return i;
        return std::min(x.size(), y.size());
    };

    // The legacy file was cut from a fresh instance: every field it carries is a
    // default and every field it lacks must load as one, so the host path ends
    // in a fresh instance's state, byte for byte. The one exception is the
    // Velocity > Amp amount that ends the state: a pre-v9 state loads 0 (its
    // sound: velocity never reached the VCA), not the registered default 1.
    std::vector<char> expectedA = defaultBytes;
    const float preV9VelocityAmount = 0.0f;
    std::memcpy(expectedA.data() + expectedA.size() - sizeof(float), &preV9VelocityAmount, sizeof(float));
    for (auto* ctrl : {static_cast<Ruinae::Controller*>(ctrlA), static_cast<Ruinae::Controller*>(ctrlB)})
        CHECK(ctrl->getParamNormalized(Ruinae::kAmpEnvVelocityAmountId) == 0.0);
    INFO("first byte differing from a fresh instance (host path): "
         << firstDifference(bytesA, expectedA) << " of " << bytesA.size());
    CHECK(bytesA == expectedA);

    // The browser path does not depend on what was loaded before: byte for byte
    // what the same browser load gives a fresh pair.
    INFO("first byte differing between browser loads after a preset and fresh: "
         << firstDifference(bytesB, bytesC) << " of " << bytesB.size());
    CHECK(bytesB == bytesC);

    // Host path vs browser path, byte for byte, over every field a legacy state
    // lacks: modulation type, flanger and chorus, the trance-gate pack, and the
    // arp chord block and lane speeds. (Fields the state CARRIES go through the
    // browser path's normalized round trip, performEdit -> handle*ParamChange,
    // and some land 1 ulp off, e.g. arp free rate 4.0 -> 3.9999998, whatever the
    // state version; that is not a legacy-migration property.)
    std::vector<char> v8 = defaultBytes;
    const LegacyStateLayout layout = layoutOf(v8);
    const std::pair<Steinberg::int64, Steinberg::int64> legacyRegions[] = {
        {layout.flangerStart - 1, layout.chorusEnd},
        {layout.tranceGateStart, layout.tranceGateEnd},
        {layout.total - kArpLaneSpeedBytes - kArpChordBlockBytes, layout.total},
    };
    for (const auto& [begin, end] : legacyRegions) {
        INFO("bytes [" << begin << ", " << end << ")");
        const auto from = bytesA.begin() + static_cast<std::ptrdiff_t>(begin);
        const auto to = bytesA.begin() + static_cast<std::ptrdiff_t>(end);
        CHECK(std::equal(from, to, bytesB.begin() + static_cast<std::ptrdiff_t>(begin)));
    }

    procA->terminate();
    b.proc->terminate();
    c.proc->terminate();
    fresh->terminate();
    for (auto* ctrl : {static_cast<Ruinae::Controller*>(ctrlA), static_cast<Ruinae::Controller*>(b.ctrl),
                       static_cast<Ruinae::Controller*>(c.ctrl)}) {
        ctrl->setComponentHandler(nullptr);
        ctrl->terminate();
        ctrl->release();
    }
}

// =============================================================================
// T4.5 (R-108): live edit, state load and editor open apply ONE view-sync rule
// =============================================================================
// setParamNormalized (live edit / automation), syncAllViews (setComponentState)
// and verifyView (a view built on editor open) each used to carry their own copy
// of the PW-knob dimming, the condition-index scale and the 120 BPM display tempo.
// Every path must show what the shared rule in controller/view_sync_rules.h says.

namespace {

using RuinaeViewLifecycleTest::ParamList;
using RuinaeViewLifecycleTest::ViewLifecycleRig;

// Processor state after the given normalized parameter changes.
std::vector<char> processorStateWith(const ParamList& params) {
    auto proc = std::make_unique<TestableProcessor>();
    REQUIRE(proc->initialize(nullptr) == Steinberg::kResultOk);
    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = 512;
    proc->setupProcessing(setup);
    StateTestParamChanges changes;
    for (const auto& [id, v] : params) changes.addChange(id, v);
    proc->processParameterChanges(&changes);
    Steinberg::MemoryStream s;
    REQUIRE(proc->getState(&s) == Steinberg::kResultTrue);
    std::vector<char> bytes(s.getData(), s.getData() + s.getSize());
    proc->terminate();
    return bytes;
}

struct ViewReadings {
    double liveEdit = -1.0;    // setParamNormalized with the editor open
    double stateLoad = -1.0;   // setComponentState with the editor open (syncAllViews)
    double editorOpen = -1.0;  // parameters set first, then the editor opens (verifyView)
};

template <typename Read>
ViewReadings readViewThreeWays(const ParamList& editorParams, const ParamList& edits, Read read) {
    ViewReadings out;
    {
        ViewLifecycleRig rig(editorParams);
        for (const auto& [id, v] : edits) rig.ctl.setParamNormalized(id, v);
        out.liveEdit = read(rig);
    }
    {
        ViewLifecycleRig rig(editorParams);
        REQUIRE(rig.loadState(processorStateWith(edits)) == Steinberg::kResultTrue);
        out.stateLoad = read(rig);
    }
    {
        ParamList all = editorParams;
        all.insert(all.end(), edits.begin(), edits.end());
        ViewLifecycleRig rig(all);
        out.editorOpen = read(rig);
    }
    return out;
}

}  // namespace

TEST_CASE("Live edit and state load give the same view state", "[controller][view-sync]") {
    using namespace RuinaeViewLifecycleTest;

    SECTION("OSC A PW knob alpha for the Pulse waveform") {
        const double pulse = static_cast<double>(Krate::DSP::OscWaveform::Pulse) /
                             static_cast<double>(Ruinae::kOscWaveformCount - 1);
        const double saw = static_cast<double>(Krate::DSP::OscWaveform::Sawtooth) /
                           static_cast<double>(Ruinae::kOscWaveformCount - 1);
        REQUIRE(Ruinae::oscPwKnobAlpha(pulse) == 1.0f);
        REQUIRE(Ruinae::oscPwKnobAlpha(saw) < 1.0f);

        auto readAlpha = [](ViewLifecycleRig& rig) {
            auto* knob = rig.pointer("oscAPWKnob_");
            REQUIRE(knob != nullptr);
            return static_cast<double>(knob->getAlphaValue());
        };
        const ParamList tab{{Ruinae::kMainTabTag, kTabSound}};
        {
            // The default (Sawtooth) knob is dimmed, so the Pulse edit has something to change.
            ViewLifecycleRig rig(tab);
            CHECK(readAlpha(rig) == Approx(Ruinae::oscPwKnobAlpha(saw)));
        }
        const auto r = readViewThreeWays(tab, {{Ruinae::kOscAWaveformId, pulse}}, readAlpha);
        CHECK(r.liveEdit == Approx(Ruinae::oscPwKnobAlpha(pulse)));
        CHECK(r.stateLoad == Approx(r.liveEdit));
        CHECK(r.editorOpen == Approx(r.liveEdit));
    }

    SECTION("Condition lane step value") {
        constexpr int kMaxCondition = Krate::Plugins::ArpConditionLane::kConditionCount - 1;
        constexpr int kCondition = 11;
        static_assert(kCondition < kMaxCondition);
        const double norm = static_cast<double>(kCondition) / kMaxCondition;
        REQUIRE(Ruinae::conditionIndexFromNormalized(norm) == kCondition);
        REQUIRE(Ruinae::conditionIndexFromNormalized(1.0) == kMaxCondition);

        auto readCondition = [](ViewLifecycleRig& rig) {
            auto* lane = dynamic_cast<Krate::Plugins::ArpConditionLane*>(rig.pointer("conditionLane_"));
            REQUIRE(lane != nullptr);
            return static_cast<double>(lane->getStepCondition(0));
        };
        const auto r = readViewThreeWays({{Ruinae::kMainTabTag, kTabSeq}},
                                         {{Ruinae::kArpConditionLaneStep0Id, norm}}, readCondition);
        CHECK(r.liveEdit == kCondition);
        CHECK(r.stateLoad == r.liveEdit);
        CHECK(r.editorOpen == r.liveEdit);
    }

    SECTION("Chaos display speed when tempo-synced") {
        constexpr int kNoteIndex = 13;
        static_assert(kNoteIndex != Krate::DSP::kNoteValueDefaultIndex);
        const double nvNorm = static_cast<double>(kNoteIndex) / (Krate::DSP::kNoteValueDropdownCount - 1);
        const auto mapping = Krate::DSP::getNoteValueFromDropdown(kNoteIndex);
        const float expected = (Krate::Plugins::LfoWaveformDisplay::kDefaultBpm / 60.0f) /
                               Krate::DSP::getBeatsForNote(mapping.note, mapping.modifier);
        REQUIRE(expected > 0.05f);
        REQUIRE(expected < 20.0f);  // inside ChaosModDisplay::setSpeed's clamp
        REQUIRE(Ruinae::displayBeatsPerSecond() == Krate::Plugins::LfoWaveformDisplay::kDefaultBpm / 60.0f);
        REQUIRE(Ruinae::syncedDisplayRateHz(nvNorm) == Approx(expected).margin(1e-6));

        auto readSpeed = [](ViewLifecycleRig& rig) {
            auto* display = dynamic_cast<Krate::Plugins::ChaosModDisplay*>(rig.pointer("chaosModDisplay_"));
            REQUIRE(display != nullptr);
            return static_cast<double>(display->getSpeed());
        };
        constexpr int kChaosSourceIndex = 2;  // ModSourceViewMode: LFO1, LFO2, Chaos, ...
        const auto r = readViewThreeWays(
            {{Ruinae::kMainTabTag, kTabMod}, {Ruinae::kModSourceViewModeTag, modSourceNorm(kChaosSourceIndex)}},
            {{Ruinae::kChaosModSyncId, 1.0}, {Ruinae::kChaosModNoteValueId, nvNorm}}, readSpeed);
        CHECK(r.liveEdit == Approx(expected).margin(1e-6));
        CHECK(r.stateLoad == Approx(r.liveEdit).margin(1e-6));
        CHECK(r.editorOpen == Approx(r.liveEdit).margin(1e-6));
    }
}
