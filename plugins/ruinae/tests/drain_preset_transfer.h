#pragma once

// ==============================================================================
// Test Helper: Drain Preset Transfer
// ==============================================================================
// Since setState() is deferred via RTTransferT, tests that call setState() and
// then immediately inspect state (via getState()) need a minimal process() call
// in between to apply the snapshot on the "audio thread."
// ==============================================================================

#include "processor/processor.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <vector>

/// Run a minimal process() call with 0 samples to drain any pending
/// RTTransferT preset snapshot. Call this after setState() in tests
/// before inspecting state via getState().
inline void drainPresetTransfer(Ruinae::Processor* proc) {
    Steinberg::Vst::ProcessData data{};
    data.numSamples = 0;
    data.numInputs = 0;
    data.numOutputs = 0;
    data.inputParameterChanges = nullptr;
    data.outputParameterChanges = nullptr;
    data.inputEvents = nullptr;
    data.outputEvents = nullptr;
    data.processContext = nullptr;
    proc->process(data);
}
