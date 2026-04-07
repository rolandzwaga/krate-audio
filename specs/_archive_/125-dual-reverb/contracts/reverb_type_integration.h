// ==============================================================================
// API Contract: Reverb Type Integration (Ruinae Plugin)
// ==============================================================================
// This file defines the integration API for dual-reverb support in Ruinae.
//
// Changes to existing files:
//   - plugins/ruinae/src/plugin_ids.h          (new parameter ID, state version bump)
//   - plugins/ruinae/src/parameters/reverb_params.h (new field + handlers)
//   - plugins/ruinae/src/engine/ruinae_effects_chain.h (dual reverb + crossfade)
//   - plugins/ruinae/src/processor/processor.cpp (state serialization)
//   - plugins/ruinae/src/controller/controller.cpp (parameter registration)
// ==============================================================================

// --- plugin_ids.h additions ---

// State version bump: 4 -> 5
// constexpr Steinberg::int32 kCurrentStateVersion = 5;

// New parameter in Reverb range (1700-1799):
// kReverbTypeId = 1709,  // 0=Plate, 1=Hall (StringListParameter, 2 steps)

// --- reverb_params.h additions ---

// New field in RuinaeReverbParams:
//   std::atomic<int32_t> reverbType{0};  // 0=Plate, 1=Hall

// New case in handleReverbParamChange:
//   case kReverbTypeId:
//       params.reverbType.store(
//           static_cast<int32_t>(std::round(value)),
//           std::memory_order_relaxed);
//       break;

// New registration in registerReverbParams:
//   auto* reverbTypeParam = new StringListParameter(
//       STR16("Reverb Type"), kReverbTypeId);
//   reverbTypeParam->appendString(STR16("Plate"));
//   reverbTypeParam->appendString(STR16("Hall"));
//   parameters.addParameter(reverbTypeParam);

// Extended save/load to include reverbType after existing reverb params.
// Backward compat: version < 5 defaults reverbType to 0 (Plate).

// --- ruinae_effects_chain.h additions ---

// New methods:
//   void setReverbType(int type) noexcept;
//     - If type == activeReverbType_ and not crossfading, no-op
//     - If crossfading, fast-track (complete current, start new)
//     - Otherwise, start 30ms equal-power crossfade:
//         reverbCrossfadeIncrement_ = crossfadeIncrement(30.0f, sampleRate_)
//     - If freeze param is currently active, call setParams on the incoming reverb
//       BEFORE the crossfade begins, so it enters freeze from the start (FR-029)

//   void setReverbTypeDirect(int type) noexcept;
//     - Sets activeReverbType_ = type immediately, NO crossfade
//     - Clears any in-progress crossfade: reverbCrossfading_ = false, alpha = 0
//     - Used ONLY during state load (setState in processor.cpp) to restore saved type
//     - Must NOT be called during normal audio playback

// Modified processInternal():
//   Reverb slot changes from:
//     if (reverbEnabled_) reverb_.processBlock(left, right, numSamples);
//   To:
//     if (reverbEnabled_) processReverbSlot(left, right, numSamples);
//
//   processReverbSlot() handles:
//     - Normal: process only active reverb
//     - Crossfading (30ms equal-power via equalPowerGains()): process both, blend
//     - On complete: reset outgoing reverb, swap activeReverbType_

// New members:
//   FDNReverb fdnReverb_;
//   int activeReverbType_ = 0;
//   int incomingReverbType_ = 0;
//   bool reverbCrossfading_ = false;
//   float reverbCrossfadeAlpha_ = 0.0f;
//   float reverbCrossfadeIncrement_ = 0.0f;
