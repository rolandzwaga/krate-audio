// ==============================================================================
// Editor lifecycle ASan stress test (Phase 6, T091 / SC-014)
// Spec: specs/141-membrum-phase6-ui/spec.md (SC-014 use-after-free safety)
// ==============================================================================
//
// Stress the Controller's initialize/setComponentState/terminate lifecycle
// 100 times, along with continuous parameter automation, to catch
// use-after-free, double-free, or cached-pointer bugs under AddressSanitizer.
//
// Why this test doesn't call createView():
//   The full VST3Editor lifecycle requires a plugin bundle on disk (for the
//   editor.uidesc resource) and a platform window. Neither is available in
//   the test harness. Host-integration validation (Reaper, auval, pluginval
//   L5) covers the full attached-window path; this test focuses on the
//   Controller surface that ASan can catch.
//
// In Release, the test compiles and runs normally. The real SC-014 value
// comes from running the [editor_lifecycle_asan] tag under an ASan build.
// ==============================================================================

#include "controller/controller.h"
#include "processor/processor.h"
#include "plugin_ids.h"
#include "dsp/pad_config.h"

#include "public.sdk/source/common/memorystream.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

using namespace Membrum;

// ==============================================================================
// T091 / SC-014: 100 init/setComponentState/terminate cycles with continuous
// parameter automation must not leak, double-free, or trip ASan.
// ==============================================================================
TEST_CASE("Editor lifecycle: 100 controller init/terminate cycles survive ASan",
          "[editor_lifecycle_asan]")
{
    // Produce a valid v6 state blob once, shared across the 100 cycles.
    Steinberg::MemoryStream stateBlob;
    {
        Processor p;
        REQUIRE(p.initialize(nullptr) == Steinberg::kResultOk);
        REQUIRE(p.getState(&stateBlob) == Steinberg::kResultOk);
        p.terminate();
    }

    for (int cycle = 0; cycle < 100; ++cycle)
    {
        Controller ctl;
        REQUIRE(ctl.initialize(nullptr) == Steinberg::kResultOk);

        // Parameter traffic that hits every Phase 6 pad macro and the
        // session-scoped globals. This is the path host automation would
        // take while the editor is open.
        const double macroValue = (cycle % 2 == 0) ? 0.75 : 0.25;
        for (int pad = 0; pad < kNumPads; ++pad)
        {
            const auto macroT = static_cast<Steinberg::Vst::ParamID>(
                padParamId(pad, kPadMacroTightness));
            const auto macroB = static_cast<Steinberg::Vst::ParamID>(
                padParamId(pad, kPadMacroBrightness));
            const auto macroC = static_cast<Steinberg::Vst::ParamID>(
                padParamId(pad, kPadMacroComplexity));
            ctl.setParamNormalized(macroT, macroValue);
            ctl.setParamNormalized(macroB, 1.0 - macroValue);
            ctl.setParamNormalized(macroC, macroValue * 0.5);
        }
        ctl.setParamNormalized(kUiModeId,     (cycle & 1) ? 1.0 : 0.0);
        ctl.setParamNormalized(kEditorSizeId, (cycle & 2) ? 1.0 : 0.0);
        ctl.setParamNormalized(kSelectedPadId,
            static_cast<double>(cycle % kNumPads) /
            static_cast<double>(kNumPads - 1));

        // Push fresh state every 10 cycles -- mimics a DAW project reload
        // while the editor would be open. This exercises the controller's
        // state-ingest path that clears and re-applies per-pad parameters.
        if ((cycle % 10) == 0)
        {
            stateBlob.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
            REQUIRE(ctl.setComponentState(&stateBlob) == Steinberg::kResultOk);
        }

        // Preset load path: same kit preset provider the UI exercises.
        if ((cycle % 7) == 0)
        {
            Steinberg::IBStream* kitStream = ctl.kitPresetStateProvider();
            if (kitStream != nullptr)
            {
                kitStream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
                ctl.kitPresetLoadProvider(kitStream);
                kitStream->release();
            }
        }

        REQUIRE(ctl.terminate() == Steinberg::kResultOk);
    }
}
