// Contract: Processor v4 API changes for Membrum Phase 4
// Documents the changes to the Processor class for multi-bus output,
// per-pad parameter dispatch, and state v4.

#pragma once

namespace Membrum {

// ============================================================
// Processor::initialize() changes:
// ============================================================
//
// BEFORE (Phase 3):
//   addEventInput(STR16("Event In"));
//   addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);
//
// AFTER (Phase 4):
//   addEventInput(STR16("Event In"));
//   addAudioOutput(STR16("Main Out"), SpeakerArr::kStereo);  // bus 0, kMain
//   for (int i = 1; i < kMaxOutputBuses; ++i) {
//       // Aux 1 through Aux 15
//       addAudioOutput(auxName, SpeakerArr::kStereo, BusTypes::kAux, 0);
//   }

// ============================================================
// NEW: activateBus override
// ============================================================
//
// tresult PLUGIN_API activateBus(MediaType type, BusDirection dir,
//                                 int32 index, TBool state) override;
// Tracks bus activation in busActive_[kMaxOutputBuses] array.

// ============================================================
// Processor fields changes:
// ============================================================
//
// REMOVED:
//   All individual atomic<float> fields for per-pad params (material_, size_, etc.)
//   These are replaced by PadConfig[32] in VoicePool.
//
// ADDED:
//   std::array<bool, kMaxOutputBuses> busActive_ = {true, false, ...};
//   int selectedPadIndex_ = 0;
//
// KEPT (global, not per-pad):
//   std::atomic<int> maxPolyphony_;
//   std::atomic<int> voiceStealingPolicy_;
//   std::atomic<float> exciterFMRatio_;        // global secondary exciter params
//   std::atomic<float> exciterFeedbackAmount_; // (not per-pad per FR-011)
//   std::atomic<float> exciterNoiseBurstDuration_;
//   std::atomic<float> exciterFrictionPressure_;

// ============================================================
// processParameterChanges() changes:
// ============================================================
//
// The giant switch statement is restructured:
// 1. Check if paramId is in per-pad range [kPadBaseId, kPadBaseId + 32*64)
//    -> compute padIndex and offset, update VoicePool::padConfig[padIndex]
// 2. Check if paramId is a global param (100-252, 260)
//    -> handle as before (some become no-ops since they proxy to per-pad)
// 3. Check if paramId is a global-only param (maxPolyphony, stealingPolicy, exciter secondaries)
//    -> handle as before

// ============================================================
// process() changes:
// ============================================================
//
// BEFORE: voicePool_.processBlock(outL, outR, numSamples);
// AFTER:  voicePool_.processBlock(outL, outR, auxL, auxR, busActive_, numOutputBuses, numSamples);
//
// Where auxL/auxR arrays are extracted from data.outputs[1..N].

// ============================================================
// getState() / setState() changes:
// ============================================================
//
// getState: writes v4 format (version=4, global settings, 32 pad configs, selectedPadIndex)
// setState: reads v4 format; chains v1->v2->v3->v4 migration for older blobs

} // namespace Membrum
