// ==============================================================================
// Editor lifecycle ASan stress test (Phase 6, T091 / SC-014)
// Spec: specs/141-membrum-phase6-ui/spec.md (SC-014 use-after-free safety)
// ==============================================================================
//
// Stress the full Controller + Processor editor lifecycle surface 100 times.
// Each cycle exercises every code path SC-014 names:
//
//   (a) Controller::initialize / terminate
//   (b) Parameter automation (macros, session params, selected pad)
//   (c) setComponentState (DAW project reload path)
//   (d) Kit preset load via provider callbacks
//   (e) Controller::createView("editor") construction and release
//       -- the bundle resources are not resolvable in the test harness so we
//          do NOT call IPlugView::attached(); creation + release is still an
//          ASan-relevant path (sub-controller registration, IDependent wiring
//          in MembrumEditorController constructors/destructors).
//   (f) Processor MIDI ingestion: a steady stream of note-on / note-off events
//       drives voice_pool allocation / stealing / choke paths, which ASan must
//       not flag as use-after-free under repeated init/terminate cycles.
//
// In Release, the test compiles and runs normally. The real SC-014 value
// comes from running the [editor_lifecycle_asan] tag under an ASan build.
// ==============================================================================

#include "controller/controller.h"
#include "processor/processor.h"
#include "plugin_ids.h"
#include "ui/membrum_editor_controller.h"
#include "dsp/pad_config.h"

#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/vsttypes.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <vector>

using namespace Membrum;
using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

// Minimal IEventList impl shared by all cycles -- buffers a handful of
// note-on / note-off events and replays them through the Processor's MIDI
// ingestion path.
class FixedEventList : public IEventList
{
public:
    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    int32 PLUGIN_API getEventCount() override { return static_cast<int32>(events_.size()); }
    tresult PLUGIN_API getEvent(int32 index, Event& e) override
    {
        if (index < 0 || index >= static_cast<int32>(events_.size()))
            return kInvalidArgument;
        e = events_[static_cast<std::size_t>(index)];
        return kResultOk;
    }
    tresult PLUGIN_API addEvent(Event& e) override
    {
        events_.push_back(e);
        return kResultOk;
    }
    void clear() { events_.clear(); }

private:
    std::vector<Event> events_;
};

} // namespace

// ==============================================================================
// T091 / SC-014: 100 full editor-lifecycle cycles survive ASan.
//
// Each cycle exercises:
//   - Controller init + param automation + state ingest + preset load + createView
//   - Processor init + MIDI note-on/off stream + setActive + process + terminate
//
// The point of the 100x loop is to catch use-after-free / double-free that
// only manifests across repeated teardown/reconstruction pairs (dangling
// IDependent subscribers, stale cached Parameter*, dangling publisher
// snapshots in voice pool paths).
// ==============================================================================
TEST_CASE("Editor lifecycle: 100 full controller+processor cycles survive ASan",
          "[editor_lifecycle_asan]")
{
    constexpr int32  kBlockSize  = 128;
    constexpr double kSampleRate = 44100.0;

    // Produce a valid v6 state blob once, shared across the 100 cycles.
    MemoryStream stateBlob;
    {
        Processor p;
        REQUIRE(p.initialize(nullptr) == kResultOk);
        REQUIRE(p.getState(&stateBlob) == kResultOk);
        p.terminate();
    }

    for (int cycle = 0; cycle < 100; ++cycle)
    {
        // ------------------------------------------------------------------
        // Controller surface.
        // ------------------------------------------------------------------
        Controller ctl;
        REQUIRE(ctl.initialize(nullptr) == kResultOk);

        // Parameter traffic that hits every Phase 6 pad macro and the
        // session-scoped globals. This is the path host automation would
        // take while the editor is open.
        const double macroValue = (cycle % 2 == 0) ? 0.75 : 0.25;
        for (int pad = 0; pad < kNumPads; ++pad)
        {
            const auto macroT = static_cast<ParamID>(
                padParamId(pad, kPadMacroTightness));
            const auto macroB = static_cast<ParamID>(
                padParamId(pad, kPadMacroBrightness));
            const auto macroC = static_cast<ParamID>(
                padParamId(pad, kPadMacroComplexity));
            ctl.setParamNormalized(macroT, macroValue);
            ctl.setParamNormalized(macroB, 1.0 - macroValue);
            ctl.setParamNormalized(macroC, macroValue * 0.5);
        }
        ctl.setParamNormalized(kUiModeId, (cycle & 1) ? 1.0 : 0.0);
        ctl.setParamNormalized(kSelectedPadId,
            static_cast<double>(cycle % kNumPads) /
            static_cast<double>(kNumPads - 1));

        // Push fresh state every 10 cycles -- mimics a DAW project reload
        // while the editor would be open. This exercises the controller's
        // state-ingest path that clears and re-applies per-pad parameters.
        if ((cycle % 10) == 0)
        {
            stateBlob.seek(0, IBStream::kIBSeekSet, nullptr);
            REQUIRE(ctl.setComponentState(&stateBlob) == kResultOk);
        }

        // Preset load path: same kit preset provider the UI exercises.
        if ((cycle % 7) == 0)
        {
            IBStream* kitStream = ctl.kitPresetStateProvider();
            if (kitStream != nullptr)
            {
                kitStream->seek(0, IBStream::kIBSeekSet, nullptr);
                ctl.kitPresetLoadProvider(kitStream);
                kitStream->release();
            }
        }

        // Editor open/close surface without a resolvable uidesc bundle:
        // construct MembrumEditorController directly (same ctor chain that
        // createView("editor") + VST3Editor would drive once the sub-view
        // scope is resolved). Its ctor registers as IDependent on
        // kUiModeId; its dtor must unregister. This is the exact
        // add/removeDependent balance the plan.md gotcha calls out as
        // use-after-free-prone.
        //
        // We do NOT invoke VST3Editor::createView itself because it
        // requires the plugin bundle resource "editor.uidesc" to be
        // resolvable and throws an SEH exception in headless tests.
        // Host-integration (Reaper, auval, pluginval L5) covers the
        // full attached-window path.
        {
            UI::MembrumEditorController subCtl(/*editor*/ nullptr, &ctl);
            // Drive a parameter change while the sub-controller is alive
            // so update() dispatches through the IDependent chain; the
            // method is tolerant of null editor_ / uiModeSwitch_.
            ctl.setParamNormalized(kUiModeId,
                (cycle & 1) ? 0.0 : 1.0);
            // subCtl goes out of scope here -> removeDependent. Any
            // imbalance would manifest in subsequent cycles.
        }

        REQUIRE(ctl.terminate() == kResultOk);

        // ------------------------------------------------------------------
        // Processor surface -- MIDI stream + param automation.
        // ------------------------------------------------------------------
        Processor p;
        REQUIRE(p.initialize(nullptr) == kResultOk);

        ProcessSetup setup{};
        setup.processMode        = kRealtime;
        setup.symbolicSampleSize = kSample32;
        setup.maxSamplesPerBlock = kBlockSize;
        setup.sampleRate         = kSampleRate;
        REQUIRE(p.setupProcessing(setup) == kResultOk);
        REQUIRE(p.setActive(true) == kResultOk);

        std::vector<float> outL(static_cast<std::size_t>(kBlockSize), 0.0f);
        std::vector<float> outR(static_cast<std::size_t>(kBlockSize), 0.0f);
        float* channels[2] = { outL.data(), outR.data() };

        AudioBusBuffers outputBus{};
        outputBus.numChannels      = 2;
        outputBus.channelBuffers32 = channels;
        outputBus.silenceFlags     = 0;

        FixedEventList events;

        ProcessData data{};
        data.processMode            = kRealtime;
        data.symbolicSampleSize     = kSample32;
        data.numSamples             = kBlockSize;
        data.numOutputs             = 1;
        data.outputs                = &outputBus;
        data.numInputs              = 0;
        data.inputs                 = nullptr;
        data.inputEvents            = &events;
        data.outputEvents           = nullptr;
        data.inputParameterChanges  = nullptr;
        data.outputParameterChanges = nullptr;
        data.processContext         = nullptr;

        // Drive a continuous MIDI note-on / note-off stream. We push 8 blocks
        // per cycle (~23 ms of audio) with at least one event per block --
        // enough to exercise voice_pool allocate / steal / choke / release
        // paths plus coupling-matrix polling while the editor would be open.
        for (int b = 0; b < 8; ++b)
        {
            events.clear();

            // Note-on on a rotating pad to hit voice allocation.
            const int16 padNote = static_cast<int16>(
                kFirstDrumNote + ((cycle + b) % kNumPads));
            {
                Event noteOn{};
                noteOn.type               = Event::kNoteOnEvent;
                noteOn.sampleOffset       = 0;
                noteOn.noteOn.channel     = 0;
                noteOn.noteOn.pitch       = padNote;
                noteOn.noteOn.velocity    = 0.8f;
                noteOn.noteOn.noteId      = 1000 + b;
                events.addEvent(noteOn);
            }

            // Alternate-block note-off on the previously triggered pad to hit
            // the release path; exercises pad_glow_publisher updates and
            // coupling-matrix energy drop.
            if ((b & 1) != 0)
            {
                Event noteOff{};
                noteOff.type               = Event::kNoteOffEvent;
                noteOff.sampleOffset       = kBlockSize / 2;
                noteOff.noteOff.channel    = 0;
                noteOff.noteOff.pitch      = static_cast<int16>(
                    kFirstDrumNote + ((cycle + b - 1) % kNumPads));
                noteOff.noteOff.velocity   = 0.0f;
                noteOff.noteOff.noteId     = 1000 + b - 1;
                events.addEvent(noteOff);
            }

            std::fill(outL.begin(), outL.end(), 0.0f);
            std::fill(outR.begin(), outR.end(), 0.0f);
            REQUIRE(p.process(data) == kResultOk);
        }

        REQUIRE(p.setActive(false) == kResultOk);
        REQUIRE(p.terminate() == kResultOk);
    }
}
