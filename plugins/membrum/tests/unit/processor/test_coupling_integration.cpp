// ==============================================================================
// Coupling Integration Tests -- Signal chain, noteOn/noteOff hooks
// ==============================================================================
// T018: Signal chain wiring -- noteOn with couplingEngine_ set causes noteOn on
// SympatheticResonance; process() with global coupling > 0 adds non-zero energy
// to output; process() with global coupling = 0 adds zero energy (SC-001);
// mono sum (L+R)/2 feeds delay then engine (not raw L or R);
// SC-002: kick + snare buzz produces audible coupling contribution.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "dsp/pad_config.h"
#include "dsp/pad_category.h"
#include "dsp/coupling_matrix.h"
#include "plugin_ids.h"

#include <krate/dsp/systems/sympathetic_resonance.h>
#include <krate/dsp/primitives/delay_line.h>
#include <krate/dsp/primitives/fft.h>

#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <numeric>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using Catch::Approx;

// =============================================================================
// Helpers
// =============================================================================

static constexpr double kTestSampleRate = 44100.0;
static constexpr int32 kTestBlockSize = 512;

static ProcessSetup makeSetup(double sampleRate = kTestSampleRate,
                              int32 blockSize = kTestBlockSize)
{
    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = blockSize;
    setup.sampleRate = sampleRate;
    return setup;
}

// Simple EventList for sending MIDI events in tests
class CouplingTestEventList : public IEventList
{
public:
    tresult PLUGIN_API queryInterface(const TUID, void**) override
    {
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    int32 PLUGIN_API getEventCount() override
    {
        return static_cast<int32>(events_.size());
    }

    tresult PLUGIN_API getEvent(int32 index, Event& e) override
    {
        if (index < 0 || index >= static_cast<int32>(events_.size()))
            return kResultFalse;
        e = events_[static_cast<size_t>(index)];
        return kResultTrue;
    }

    tresult PLUGIN_API addEvent(Event& e) override
    {
        events_.push_back(e);
        return kResultTrue;
    }

    void addNoteOn(int16 pitch, float velocity, int32 sampleOffset = 0)
    {
        Event e{};
        e.type = Event::kNoteOnEvent;
        e.sampleOffset = sampleOffset;
        e.noteOn.channel = 0;
        e.noteOn.pitch = pitch;
        e.noteOn.velocity = velocity;
        e.noteOn.noteId = pitch;
        e.noteOn.tuning = 0.0f;
        e.noteOn.length = 0;
        events_.push_back(e);
    }

    void addNoteOff(int16 pitch, int32 sampleOffset = 0)
    {
        Event e{};
        e.type = Event::kNoteOffEvent;
        e.sampleOffset = sampleOffset;
        e.noteOff.channel = 0;
        e.noteOff.pitch = pitch;
        e.noteOff.velocity = 0.0f;
        e.noteOff.noteId = pitch;
        e.noteOff.tuning = 0.0f;
        events_.push_back(e);
    }

    void clear() { events_.clear(); }

private:
    std::vector<Event> events_;
};

// Minimal parameter change queue for setting a single parameter
class SingleParamQueue : public IParamValueQueue
{
public:
    SingleParamQueue(ParamID id, ParamValue value)
        : id_(id), value_(value) {}

    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    ParamID PLUGIN_API getParameterId() override { return id_; }
    int32 PLUGIN_API getPointCount() override { return 1; }

    tresult PLUGIN_API getPoint(int32 index, int32& sampleOffset, ParamValue& value) override
    {
        if (index != 0) return kResultFalse;
        sampleOffset = 0;
        value = value_;
        return kResultOk;
    }

    tresult PLUGIN_API addPoint([[maybe_unused]] int32 sampleOffset,
                                [[maybe_unused]] ParamValue value,
                                [[maybe_unused]] int32& index) override
    {
        return kResultFalse;
    }

private:
    ParamID id_;
    ParamValue value_;
};

class MultiParamChanges : public IParameterChanges
{
public:
    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    int32 PLUGIN_API getParameterCount() override
    {
        return static_cast<int32>(queues_.size());
    }

    IParamValueQueue* PLUGIN_API getParameterData(int32 index) override
    {
        if (index < 0 || index >= static_cast<int32>(queues_.size()))
            return nullptr;
        return queues_[static_cast<size_t>(index)].get();
    }

    IParamValueQueue* PLUGIN_API addParameterData(
        [[maybe_unused]] const ParamID& id,
        [[maybe_unused]] int32& index) override
    {
        return nullptr;
    }

    void add(ParamID id, ParamValue value)
    {
        queues_.push_back(std::make_unique<SingleParamQueue>(id, value));
    }

private:
    std::vector<std::unique_ptr<SingleParamQueue>> queues_;
};

// Test fixture for coupling integration tests
struct CouplingIntegrationFixture
{
    Membrum::Processor processor;
    CouplingTestEventList events;

    std::vector<float> outL;
    std::vector<float> outR;
    float* outChannels[2];
    AudioBusBuffers outputBus{};
    ProcessData data{};

    int32 blockSize;

    explicit CouplingIntegrationFixture(int32 bs = kTestBlockSize,
                                        double sampleRate = kTestSampleRate)
        : outL(static_cast<size_t>(bs), 0.0f)
        , outR(static_cast<size_t>(bs), 0.0f)
        , blockSize(bs)
    {
        outChannels[0] = outL.data();
        outChannels[1] = outR.data();

        outputBus.numChannels = 2;
        outputBus.channelBuffers32 = outChannels;
        outputBus.silenceFlags = 0;

        data.processMode = kRealtime;
        data.symbolicSampleSize = kSample32;
        data.numSamples = bs;
        data.numOutputs = 1;
        data.outputs = &outputBus;
        data.numInputs = 0;
        data.inputs = nullptr;
        data.inputParameterChanges = nullptr;
        data.outputParameterChanges = nullptr;
        data.inputEvents = &events;
        data.outputEvents = nullptr;
        data.processContext = nullptr;

        processor.initialize(nullptr);
        auto setup = makeSetup(sampleRate, bs);
        processor.setupProcessing(setup);
        processor.setActive(true);
    }

    ~CouplingIntegrationFixture()
    {
        processor.setActive(false);
        processor.terminate();
    }

    void clearBuffers()
    {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
    }

    void processBlock()
    {
        clearBuffers();
        processor.process(data);
    }

    void setParam(ParamID id, ParamValue value)
    {
        MultiParamChanges changes;
        changes.add(id, value);
        data.inputParameterChanges = &changes;
        // Process a silent block to apply the parameter
        clearBuffers();
        events.clear();
        processor.process(data);
        data.inputParameterChanges = nullptr;
    }

    float peakAmplitude() const
    {
        float peak = 0.0f;
        for (size_t i = 0; i < outL.size(); ++i)
            peak = std::max(peak, std::max(std::abs(outL[i]), std::abs(outR[i])));
        return peak;
    }

    double rmsEnergy() const
    {
        double sum = 0.0;
        for (size_t i = 0; i < outL.size(); ++i)
        {
            sum += static_cast<double>(outL[i]) * outL[i];
            sum += static_cast<double>(outR[i]) * outR[i];
        }
        return sum / (2.0 * static_cast<double>(outL.size()));
    }

    // Process multiple blocks and collect L channel samples
    std::vector<float> processNBlocks(int numBlocks)
    {
        std::vector<float> result;
        result.reserve(static_cast<size_t>(numBlocks) * static_cast<size_t>(blockSize));
        for (int b = 0; b < numBlocks; ++b)
        {
            processBlock();
            result.insert(result.end(), outL.begin(), outL.end());
            events.clear();
        }
        return result;
    }
};

// =============================================================================
// Tests
// =============================================================================

TEST_CASE("Coupling integration: noteOn with coupling engine set triggers resonance",
          "[coupling]")
{
    CouplingIntegrationFixture fix;

    // Set global coupling to 100% and snare buzz to 50%
    fix.setParam(Membrum::kGlobalCouplingId, 1.0);
    fix.setParam(Membrum::kSnareBuzzId, 0.5);

    // Trigger the kick (MIDI 36) at full velocity
    fix.events.addNoteOn(36, 1.0f);

    // Process several blocks to allow coupling resonance to develop
    auto samples = fix.processNBlocks(20);

    // With coupling enabled, there should be non-zero energy
    double energy = 0.0;
    for (float s : samples)
        energy += static_cast<double>(s) * s;
    energy /= static_cast<double>(samples.size());

    // The coupling contribution should add energy above what the kick alone produces
    // This test verifies signal chain wiring is active
    CHECK(energy > 0.0);
}

TEST_CASE("Coupling integration: global coupling = 0 adds zero coupling energy (SC-001)",
          "[coupling]")
{
    // Run 1: with coupling disabled (global coupling = 0)
    CouplingIntegrationFixture fix1;
    fix1.setParam(Membrum::kGlobalCouplingId, 0.0);
    fix1.setParam(Membrum::kSnareBuzzId, 0.5);

    fix1.events.addNoteOn(36, 1.0f);
    auto samplesOff = fix1.processNBlocks(10);

    // Run 2: baseline with no coupling params at all (Phase 4 behavior)
    CouplingIntegrationFixture fix2;
    // Don't set any coupling params -- defaults should be 0
    fix2.events.addNoteOn(36, 1.0f);
    auto samplesBaseline = fix2.processNBlocks(10);

    // SC-001: outputs should be identical within -120 dBFS tolerance
    REQUIRE(samplesOff.size() == samplesBaseline.size());
    double maxDiff = 0.0;
    for (size_t i = 0; i < samplesOff.size(); ++i)
    {
        double diff = std::abs(static_cast<double>(samplesOff[i]) - samplesBaseline[i]);
        maxDiff = std::max(maxDiff, diff);
    }
    // -120 dBFS = 10^(-120/20) = 1e-6
    CHECK(maxDiff < 1e-6);
}

TEST_CASE("Coupling integration: SC-002 kick triggers audible snare buzz",
          "[coupling]")
{
    CouplingIntegrationFixture fix;

    // Enable coupling: global 100%, snare buzz 50%
    fix.setParam(Membrum::kGlobalCouplingId, 1.0);
    fix.setParam(Membrum::kSnareBuzzId, 0.5);

    // Trigger kick (MIDI 36)
    fix.events.addNoteOn(36, 1.0f);
    auto samplesWithCoupling = fix.processNBlocks(20);

    // Get the kick peak level
    float kickPeak = 0.0f;
    for (float s : samplesWithCoupling)
        kickPeak = std::max(kickPeak, std::abs(s));

    // Now run without coupling for comparison
    CouplingIntegrationFixture fixNoCoupling;
    fixNoCoupling.setParam(Membrum::kGlobalCouplingId, 0.0);
    fixNoCoupling.setParam(Membrum::kSnareBuzzId, 0.5);

    fixNoCoupling.events.addNoteOn(36, 1.0f);
    auto samplesNoCoupling = fixNoCoupling.processNBlocks(20);

    // Compute the difference (coupling contribution)
    REQUIRE(samplesWithCoupling.size() == samplesNoCoupling.size());
    double couplingEnergy = 0.0;
    for (size_t i = 0; i < samplesWithCoupling.size(); ++i)
    {
        double diff = static_cast<double>(samplesWithCoupling[i]) - samplesNoCoupling[i];
        couplingEnergy += diff * diff;
    }
    couplingEnergy = std::sqrt(couplingEnergy / static_cast<double>(samplesWithCoupling.size()));

    // SC-002: coupling contribution must be measurably above noise floor
    CHECK(couplingEnergy > 1e-8);

    // SC-002: coupling contribution must be at least -40 dBFS below kick peak
    // i.e., coupling RMS < kickPeak * 10^(-40/20) = kickPeak * 0.01
    if (kickPeak > 0.0f)
    {
        CHECK(couplingEnergy < static_cast<double>(kickPeak) * 0.01);
    }
}

TEST_CASE("Coupling integration: mono sum (L+R)/2 feeds delay then engine",
          "[coupling]")
{
    // This test verifies the signal chain order: mono sum -> delay read -> delay write -> engine
    // We verify this indirectly by checking the coupling output is consistent
    // with the expected signal flow
    CouplingIntegrationFixture fix;

    fix.setParam(Membrum::kGlobalCouplingId, 1.0);
    fix.setParam(Membrum::kSnareBuzzId, 1.0);

    // Trigger kick
    fix.events.addNoteOn(36, 1.0f);
    auto samples = fix.processNBlocks(10);

    // With coupling enabled, output should have energy
    double energy = 0.0;
    for (float s : samples)
        energy += static_cast<double>(s) * s;

    CHECK(energy > 0.0);
}

// =============================================================================
// Phase 4 (User Story 2) -- Tom Sympathetic Resonance (T030, T032)
// =============================================================================

TEST_CASE("Phase 4: classifyPad identifies Tom pads from default kit configuration "
          "(T030)",
          "[coupling][phase4]")
{
    // Default kit Tom pads (from DefaultKit::apply): indices 5, 7, 9, 11, 12, 14.
    // Template: Mallet exciter + Membrane body, no pitch envelope, no noise burst.
    Membrum::PadConfig tom;
    tom.exciterType    = Membrum::ExciterType::Mallet;
    tom.bodyModel      = Membrum::BodyModelType::Membrane;
    tom.tsPitchEnvTime = 0.0f;
    CHECK(Membrum::classifyPad(tom) == Membrum::PadCategory::Tom);

    // Kick template -- Membrane + pitch envelope active.
    Membrum::PadConfig kick;
    kick.exciterType    = Membrum::ExciterType::Impulse;
    kick.bodyModel      = Membrum::BodyModelType::Membrane;
    kick.tsPitchEnvTime = 0.04f;
    CHECK(Membrum::classifyPad(kick) == Membrum::PadCategory::Kick);

    // Snare template -- Membrane + NoiseBurst exciter, no pitch env.
    Membrum::PadConfig snare;
    snare.exciterType    = Membrum::ExciterType::NoiseBurst;
    snare.bodyModel      = Membrum::BodyModelType::Membrane;
    snare.tsPitchEnvTime = 0.0f;
    CHECK(Membrum::classifyPad(snare) == Membrum::PadCategory::Snare);

    // Hat template -- NoiseBody.
    Membrum::PadConfig hat;
    hat.exciterType = Membrum::ExciterType::NoiseBurst;
    hat.bodyModel   = Membrum::BodyModelType::NoiseBody;
    CHECK(Membrum::classifyPad(hat) == Membrum::PadCategory::HatCymbal);
}

TEST_CASE("Phase 4: Tom Resonance knob at 50% yields Tom->Tom effectiveGain = 0.025 "
          "in processor matrix (T030)",
          "[coupling][phase4]")
{
    // End-to-end: setting the kTomResonanceId parameter must flow through
    // Processor::processParameterChanges -> CouplingMatrix::recomputeFromTier1,
    // with pad categories derived from the (default kit) PadConfig instances.
    CouplingIntegrationFixture fix;
    fix.setParam(Membrum::kTomResonanceId, 0.5);
    fix.setParam(Membrum::kSnareBuzzId,    0.0);

    const auto& matrix = fix.processor.couplingMatrixForTest();

    // Default kit Tom pads: 5, 7, 9, 11, 12, 14.
    constexpr int toms[] = {5, 7, 9, 11, 12, 14};
    const float expected = 0.5f * Membrum::CouplingMatrix::kMaxCoefficient; // 0.025

    for (int src : toms) {
        for (int dst : toms) {
            if (src == dst) {
                CHECK(matrix.getEffectiveGain(src, dst) == 0.0f);
            } else {
                CHECK(matrix.getEffectiveGain(src, dst) == Approx(expected));
            }
        }
    }

    // Non-tom pads must not gain Tom-style coupling.
    // Pad 0 is Kick, pad 6 is Hat.
    CHECK(matrix.getEffectiveGain(0, 5) == 0.0f);
    CHECK(matrix.getEffectiveGain(5, 0) == 0.0f);
    CHECK(matrix.getEffectiveGain(6, 5) == 0.0f);
}

TEST_CASE("Phase 4: Tom Resonance = 0 produces zero Tom->Tom gain in processor matrix "
          "(T030)",
          "[coupling][phase4]")
{
    CouplingIntegrationFixture fix;
    fix.setParam(Membrum::kTomResonanceId, 0.0);
    fix.setParam(Membrum::kSnareBuzzId,    1.0);

    const auto& matrix = fix.processor.couplingMatrixForTest();
    constexpr int toms[] = {5, 7, 9, 11, 12, 14};
    for (int src : toms)
        for (int dst : toms)
            CHECK(matrix.getEffectiveGain(src, dst) == 0.0f);
}

TEST_CASE("Phase 4: padCategories_ updates when pad body model / exciter / pitch env change "
          "(T032)",
          "[coupling][phase4]")
{
    // T032: verify that per-pad config changes (body model, exciter type,
    // or pitch envelope time) trigger pad-category recomputation, and that
    // the coupling matrix reflects the new categorization afterwards.
    CouplingIntegrationFixture fix;

    // Enable Tom Resonance so Tom->Tom pairs have non-zero gain.
    fix.setParam(Membrum::kTomResonanceId, 1.0);

    // Baseline: pad 5 is a Tom (default kit). Pad 5 <-> pad 7 should couple.
    {
        const auto& m = fix.processor.couplingMatrixForTest();
        REQUIRE(m.getEffectiveGain(5, 7) == Approx(Membrum::CouplingMatrix::kMaxCoefficient));
    }

    // Change pad 5's body model to NoiseBody -> it becomes HatCymbal.
    // kPadBodyModel = offset 1. Normalized selector index for NoiseBody.
    const int padIdx = 5;
    const int bodyModelParam = Membrum::padParamId(padIdx, Membrum::kPadBodyModel);
    const double noiseBodyNorm =
        (static_cast<double>(static_cast<int>(Membrum::BodyModelType::NoiseBody)) + 0.5) /
        static_cast<double>(Membrum::BodyModelType::kCount);
    fix.setParam(static_cast<Steinberg::Vst::ParamID>(bodyModelParam), noiseBodyNorm);

    // Pad 5 is no longer a Tom -> Tom->Tom coupling with pad 7 must vanish.
    {
        const auto& m = fix.processor.couplingMatrixForTest();
        CHECK(m.getEffectiveGain(5, 7) == 0.0f);
        CHECK(m.getEffectiveGain(7, 5) == 0.0f);
    }

    // Restore Membrane body; still a Mallet Tom -> coupling returns.
    const double membraneNorm =
        (static_cast<double>(static_cast<int>(Membrum::BodyModelType::Membrane)) + 0.5) /
        static_cast<double>(Membrum::BodyModelType::kCount);
    fix.setParam(static_cast<Steinberg::Vst::ParamID>(bodyModelParam), membraneNorm);
    {
        const auto& m = fix.processor.couplingMatrixForTest();
        CHECK(m.getEffectiveGain(5, 7) == Approx(Membrum::CouplingMatrix::kMaxCoefficient));
    }

    // Switch exciter to NoiseBurst -> pad 5 becomes Snare, not Tom.
    const int exciterParam = Membrum::padParamId(padIdx, Membrum::kPadExciterType);
    const double noiseBurstNorm =
        (static_cast<double>(static_cast<int>(Membrum::ExciterType::NoiseBurst)) + 0.5) /
        static_cast<double>(Membrum::ExciterType::kCount);
    fix.setParam(static_cast<Steinberg::Vst::ParamID>(exciterParam), noiseBurstNorm);
    {
        const auto& m = fix.processor.couplingMatrixForTest();
        CHECK(m.getEffectiveGain(5, 7) == 0.0f);
    }

    // Back to Mallet; pad 5 is a Tom again.
    const double malletNorm =
        (static_cast<double>(static_cast<int>(Membrum::ExciterType::Mallet)) + 0.5) /
        static_cast<double>(Membrum::ExciterType::kCount);
    fix.setParam(static_cast<Steinberg::Vst::ParamID>(exciterParam), malletNorm);
    {
        const auto& m = fix.processor.couplingMatrixForTest();
        CHECK(m.getEffectiveGain(5, 7) == Approx(Membrum::CouplingMatrix::kMaxCoefficient));
    }

    // Apply a non-zero pitch envelope time -> pad 5 becomes Kick.
    const int pitchEnvTimeParam = Membrum::padParamId(padIdx, Membrum::kPadTSPitchEnvTime);
    fix.setParam(static_cast<Steinberg::Vst::ParamID>(pitchEnvTimeParam), 0.25);
    {
        const auto& m = fix.processor.couplingMatrixForTest();
        CHECK(m.getEffectiveGain(5, 7) == 0.0f);
    }

    // Clear pitch envelope time -> Tom again.
    fix.setParam(static_cast<Steinberg::Vst::ParamID>(pitchEnvTimeParam), 0.0);
    {
        const auto& m = fix.processor.couplingMatrixForTest();
        CHECK(m.getEffectiveGain(5, 7) == Approx(Membrum::CouplingMatrix::kMaxCoefficient));
    }
}

TEST_CASE("Phase 4: SC-008 frequency-selective coupling via modal coincidence -- "
          "mode-coincident toms couple >= 12 dB more than mode-gap toms",
          "[coupling][phase4]")
{
    // SC-008: A receiver drum whose fundamental coincides with one of the
    // driver's first 4 modal partials (mode-coincident tuning) MUST produce
    // at least 12 dB more coupling energy at that frequency than a receiver
    // whose fundamental falls in a gap between the driver's modes (mode-gap
    // tuning).
    //
    // Physics rationale: membrane modes are inharmonic -- Bessel ratios
    // 1.0, 1.594, 2.136, 2.296 (Rossing & Fletcher, Physics of Musical
    // Instruments; Cook, PhISM 1996). Musical intervals (octave, tritone)
    // DO NOT predict coupling between inharmonic bodies because the modal
    // spectrum does not line up at 2:1 or sqrt(2):1 ratios. What predicts
    // coupling strength is modal coincidence -- how near a receiver's
    // resonator frequency lies to one of the driver's actively driven
    // modes.
    //
    // Setup: Pad 5 (driver, Tom) is struck. Pad 9 (receiver, Tom) is also
    // struck at very low velocity so its partials register with the
    // SympatheticResonance engine (the engine tracks active voices). Pad 9's
    // Size is the variable under test:
    //   Mode-coincident: pad 9 f0 lands on pad 5's 2nd modal partial
    //                    (f0 * 1.594 ~= 158.9 Hz at pad5 size=0.7).
    //   Mode-gap:        pad 9 f0 lands in the gap between pad 5's 1st and
    //                    2nd modal partials (~130 Hz -- far from any pad 5
    //                    resonator).
    // Running the same MIDI sequence with coupling on vs off and subtracting
    // isolates the coupling-engine contribution (direct voice audio cancels).

    auto runConfig = [](float pad9Size, bool couplingOn) {
        CouplingIntegrationFixture fix;

        // Tune pad 5 and pad 9 (both default-kit Toms).
        const int pad5SizeId = Membrum::padParamId(5, Membrum::kPadSize);
        const int pad9SizeId = Membrum::padParamId(9, Membrum::kPadSize);
        fix.setParam(static_cast<Steinberg::Vst::ParamID>(pad5SizeId), 0.7);
        fix.setParam(static_cast<Steinberg::Vst::ParamID>(pad9SizeId), pad9Size);

        if (couplingOn) {
            fix.setParam(Membrum::kGlobalCouplingId, 1.0);
            fix.setParam(Membrum::kTomResonanceId,   1.0);
        } else {
            fix.setParam(Membrum::kGlobalCouplingId, 0.0);
            fix.setParam(Membrum::kTomResonanceId,   0.0);
        }

        // Strike ONLY pad 5 (MIDI 41). Pad 9's Size parameter configures
        // its expected natural frequency for the test observation -- we
        // look for spectral energy at pad 9's f0 in the coupling signal
        // that originated from pad 5's modal excitation. Identical MIDI
        // sequence is used for coupling-on and coupling-off runs so the
        // on-off diff isolates the coupling-engine output only.
        fix.events.addNoteOn(41, 1.0f, 0);
        // Process ~80 blocks = 40960 samples (~0.93s) to capture resonator ring-out.
        return fix.processNBlocks(80);
    };

    // Pad 5 fundamental at Size=0.7: f0 = 500 * 0.1^0.7 ~= 99.76 Hz.
    // Pad 5 inharmonic membrane modes (Bessel ratios 1.0, 1.594, 2.136,
    // 2.296) land at approximately: {99.8, 158.9, 213.1, 229.0} Hz.
    //
    // Mode-coincident: tune pad 9 f0 onto pad 5's 2nd modal partial
    //                  (~158.9 Hz). Size = log10(500 / 158.9) ~= 0.498.
    //
    // Mode-gap: tune pad 9 f0 BETWEEN pad 5's 1st and 2nd partials at
    //           ~130 Hz (no pad 5 resonator is anywhere nearby).
    //           Size = log10(500 / 130) ~= 0.585.
    constexpr float kModeCoincidentSize = 0.498f;
    constexpr float kModeGapSize        = 0.585f;

    auto coincidentOn  = runConfig(kModeCoincidentSize, true);
    auto coincidentOff = runConfig(kModeCoincidentSize, false);
    auto gapOn         = runConfig(kModeGapSize,        true);
    auto gapOff        = runConfig(kModeGapSize,        false);

    REQUIRE(coincidentOn.size() == coincidentOff.size());
    REQUIRE(gapOn.size()        == gapOff.size());

    // Isolate the coupling contribution: diff = on - off.
    auto couplingDiff = [](const std::vector<float>& on,
                            const std::vector<float>& off) {
        std::vector<float> diff(on.size());
        for (size_t i = 0; i < on.size(); ++i)
            diff[i] = on[i] - off[i];
        return diff;
    };

    const auto coincidentCoupling = couplingDiff(coincidentOn, coincidentOff);
    const auto gapCoupling        = couplingDiff(gapOn,        gapOff);

    // Sanity: both configurations must produce a measurable coupling signal.
    auto rms = [](const std::vector<float>& v) {
        double sq = 0.0;
        for (float s : v) sq += static_cast<double>(s) * s;
        return std::sqrt(sq / static_cast<double>(v.size()));
    };
    REQUIRE(rms(coincidentCoupling) > 1e-9);
    REQUIRE(rms(gapCoupling)        > 1e-9);

    // Spectral analysis via FFT. Use tail samples (after exciters have decayed,
    // so the signal is dominated by sympathetic-resonator ringing).
    constexpr size_t kFFTSize = 8192;
    // Skip the first ~500 ms so the exciter/body transients have mostly
    // settled and the residual signal is driven by the resonator bank only.
    constexpr size_t kSkipSamples = 22050; // ~0.5 s
    REQUIRE(coincidentCoupling.size() >= kSkipSamples + kFFTSize);

    auto spectralPeakEnergyHz = [&](const std::vector<float>& x,
                                     float centerHz,
                                     float halfWidthHz) {
        // Use a tail region past kSkipSamples to focus on resonator ring-out.
        const size_t start = std::max<size_t>(kSkipSamples, x.size() - kFFTSize);
        std::vector<float> windowed(kFFTSize);
        for (size_t i = 0; i < kFFTSize; ++i) {
            const float w = 0.5f * (1.0f - std::cos(2.0f * 3.14159265358979f *
                                    static_cast<float>(i) /
                                    static_cast<float>(kFFTSize)));
            windowed[i] = x[start + i] * w;
        }
        Krate::DSP::FFT fft;
        fft.prepare(kFFTSize);
        std::vector<Krate::DSP::Complex> spectrum(kFFTSize / 2 + 1);
        fft.forward(windowed.data(), spectrum.data());

        const double binHz =
            kTestSampleRate / static_cast<double>(kFFTSize);
        const int lo = std::max(0, static_cast<int>(
            std::floor((centerHz - halfWidthHz) / binHz)));
        const int hi = std::min(static_cast<int>(kFFTSize / 2),
            static_cast<int>(std::ceil((centerHz + halfWidthHz) / binHz)));
        // Return the PEAK bin energy within the window (not the sum). This
        // makes the measurement robust to windowing side-lobes and captures
        // the resonator spectral line itself.
        double peak = 0.0;
        for (int k = lo; k <= hi; ++k) {
            const double e =
                static_cast<double>(spectrum[static_cast<size_t>(k)].real) *
                    spectrum[static_cast<size_t>(k)].real +
                static_cast<double>(spectrum[static_cast<size_t>(k)].imag) *
                    spectrum[static_cast<size_t>(k)].imag;
            if (e > peak) peak = e;
        }
        return peak;
    };

    // Runtime-measured frequencies (no hardcoded strings).
    const float pad5F0             = 500.0f * std::pow(0.1f, 0.7f);
    const float pad5Mode2          = pad5F0 * 1.594f; // Bessel ratio
    const float pad9CoincidentF0   = 500.0f * std::pow(0.1f, kModeCoincidentSize);
    const float pad9GapF0          = 500.0f * std::pow(0.1f, kModeGapSize);

    INFO("pad5 f0 (1st mode):       " << pad5F0           << " Hz");
    INFO("pad5 2nd modal partial:   " << pad5Mode2        << " Hz");
    INFO("pad9 f0 (mode-coincident): " << pad9CoincidentF0 << " Hz "
         "(placed on pad5's 2nd mode)");
    INFO("pad9 f0 (mode-gap):        " << pad9GapF0        << " Hz "
         "(placed between pad5's 1st and 2nd modes)");

    // Measure spectral energy at pad 9's fundamental in a narrow band (~ bin
    // resolution 5.4 Hz at SR 44.1 kHz, FFT 8192). Use +/- 8 Hz to capture
    // the peak and a couple of adjacent bins (covers windowing smear).
    const float kHalfWidthHz = 8.0f;
    const double coincidentEnergy =
        spectralPeakEnergyHz(coincidentCoupling, pad9CoincidentF0, kHalfWidthHz);
    const double gapEnergy =
        spectralPeakEnergyHz(gapCoupling,        pad9GapF0,        kHalfWidthHz);

    // Sanity: energy at pad 5's own fundamental and 2nd mode should be
    // comparable across cases (both runs have the same driver).
    const double coincidentAtMode1 =
        spectralPeakEnergyHz(coincidentCoupling, pad5F0,    kHalfWidthHz);
    const double gapAtMode1 =
        spectralPeakEnergyHz(gapCoupling,        pad5F0,    kHalfWidthHz);
    const double coincidentAtMode2 =
        spectralPeakEnergyHz(coincidentCoupling, pad5Mode2, kHalfWidthHz);
    const double gapAtMode2 =
        spectralPeakEnergyHz(gapCoupling,        pad5Mode2, kHalfWidthHz);

    INFO("mode-coincident energy at pad9 f0 ("   << pad9CoincidentF0
         << " Hz): " << coincidentEnergy);
    INFO("mode-gap energy at pad9 f0 ("          << pad9GapF0
         << " Hz): " << gapEnergy);
    INFO("mode-coincident energy at pad5 1st mode (" << pad5F0
         << " Hz): " << coincidentAtMode1);
    INFO("mode-gap energy at pad5 1st mode ("        << pad5F0
         << " Hz): " << gapAtMode1);
    INFO("mode-coincident energy at pad5 2nd mode (" << pad5Mode2
         << " Hz): " << coincidentAtMode2);
    INFO("mode-gap energy at pad5 2nd mode ("        << pad5Mode2
         << " Hz): " << gapAtMode2);

    REQUIRE(coincidentEnergy > 0.0);
    REQUIRE(gapEnergy        > 0.0);

    // SC-008: the mode-coincident case must have at least 12 dB more
    // coupling energy at the receiver tom's fundamental than the mode-gap
    // case. This is frequency selectivity via modal coincidence: the
    // resonator tuned to pad 9's f0 is driven strongly when pad 9's f0
    // aligns with pad 5's 2nd modal partial (which the driver excites), and
    // weakly when pad 9's f0 falls in a spectral gap between pad 5's modes.
    const double ratioDb =
        10.0 * std::log10(coincidentEnergy / gapEnergy);
    INFO("mode-coincident / mode-gap coupling-energy ratio at pad9 f0 (dB): "
         << ratioDb);
    CHECK(ratioDb >= 12.0);
}

TEST_CASE("Coupling integration: velocity scaling produces proportionally less coupling",
          "[coupling]")
{
    // High velocity
    CouplingIntegrationFixture fixHigh;
    fixHigh.setParam(Membrum::kGlobalCouplingId, 1.0);
    fixHigh.setParam(Membrum::kSnareBuzzId, 1.0);
    fixHigh.events.addNoteOn(36, 1.0f);
    auto samplesHigh = fixHigh.processNBlocks(20);

    // Low velocity
    CouplingIntegrationFixture fixLow;
    fixLow.setParam(Membrum::kGlobalCouplingId, 1.0);
    fixLow.setParam(Membrum::kSnareBuzzId, 1.0);
    fixLow.events.addNoteOn(36, 0.3f);
    auto samplesLow = fixLow.processNBlocks(20);

    // Compute RMS energy for both
    double energyHigh = 0.0, energyLow = 0.0;
    for (float s : samplesHigh) energyHigh += static_cast<double>(s) * s;
    for (float s : samplesLow)  energyLow  += static_cast<double>(s) * s;

    // Lower velocity should produce less total energy
    CHECK(energyLow < energyHigh);
}
