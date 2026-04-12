#pragma once

// ==============================================================================
// Coupling Integration Contracts -- Phase 5
// ==============================================================================
// Contract: specs/140-membrum-phase5-coupling/contracts/coupling_integration.h
//
// Defines the integration points between the coupling subsystem and existing
// Membrum components (Processor, VoicePool, DrumVoice).
// ==============================================================================

// --- Processor extensions (processor.h) ---
//
// New members:
//   Krate::DSP::SympatheticResonance  couplingEngine_;
//   Krate::DSP::DelayLine             couplingDelay_;
//   CouplingMatrix                    couplingMatrix_;
//   std::array<PadCategory, kNumPads> padCategories_{};
//   std::atomic<float> globalCoupling_{0.0f};
//   std::atomic<float> snareBuzz_{0.0f};
//   std::atomic<float> tomResonance_{0.0f};
//   std::atomic<float> couplingDelayMs_{1.0f};
//   float energyEnvelope_ = 0.0f;
//
// Signal chain in process():
//   1. processParameterChanges(...)   // handle Phase 5 param IDs
//   2. processEvents(...)
//   3. voicePool_.processBlock(outL, outR, ...)
//   4. if (!couplingEngine_.isBypassed()):
//        delaySamples = couplingDelayMs_ * sampleRate / 1000.0f
//        for each sample s in [0, numSamples):
//          mono = (outL[s] + outR[s]) * 0.5f
//          delayed = couplingDelay_.readLinear(delaySamples)
//          couplingDelay_.write(mono)
//          coupling = couplingEngine_.process(delayed)
//          coupling = applyEnergyLimiter(coupling)
//          outL[s] += coupling
//          outR[s] += coupling

// --- VoicePool extensions (voice_pool.h) ---
//
// New member:
//   Krate::DSP::SympatheticResonance* couplingEngine_ = nullptr;
//
// New method:
//   void setCouplingEngine(Krate::DSP::SympatheticResonance* engine) noexcept;
//
// Modified methods:
//   noteOn(): after applyPadConfigToSlot(), extract first 4 partials from
//     the voice's body ModalResonatorBank and call:
//       couplingEngine_->noteOn(voiceId, partials, velocity / 127.0f);
//     where voiceId is the slot index, partials.frequencies are from
//     bodyBank_.getSharedBank().getModeFrequency(0..3), and velocity is
//     normalized MIDI velocity (0.0-1.0) for coupling excitation scaling (FR-041).
//
//   noteOff(): call couplingEngine_->noteOff(voiceId) before releasing.
//
// The per-pad coupling amount is NOT applied here (it's applied in the
// CouplingMatrix gain formula at the Processor level).

// --- DrumVoice extensions (drum_voice.h) ---
//
// New method:
//   /// Extract the first N partial frequencies from the body's modal bank.
//   /// Returns a SympatheticPartialInfo with up to kSympatheticPartialCount freqs.
//   [[nodiscard]] Krate::DSP::SympatheticPartialInfo
//   getPartialInfo() const noexcept;

// --- ModalResonatorBank extensions (modal_resonator_bank.h) ---
//
// New methods:
//   [[nodiscard]] float getModeFrequency(int k) const noexcept;
//   [[nodiscard]] int getNumModes() const noexcept;

// --- Parameter handling in processParameterChanges ---
//
// New cases:
//   kGlobalCouplingId (270): globalCoupling_.store(value)
//   kSnareBuzzId (271): snareBuzz_.store(value) + recompute matrix
//   kTomResonanceId (272): tomResonance_.store(value) + recompute matrix
//   kCouplingDelayId (273): couplingDelayMs_.store(denormalize(value))
//   kPadCouplingAmount (offset 36): setPadConfigField + recompute matrix

// --- Energy limiter (inline in Processor) ---
//
// One-pole envelope follower on |coupling output|:
//   float applyEnergyLimiter(float sample) noexcept {
//       constexpr float kThreshold = 0.1f;  // -20 dBFS
//       constexpr float kAttackCoeff = 0.001f;  // fast attack
//       constexpr float kReleaseCoeff = 0.9999f;  // slow release
//       float absVal = std::abs(sample);
//       float coeff = absVal > energyEnvelope_ ? kAttackCoeff : kReleaseCoeff;
//       energyEnvelope_ += (1.0f - coeff) * (absVal - energyEnvelope_);
//       if (energyEnvelope_ > kThreshold) {
//           return sample * (kThreshold / energyEnvelope_);
//       }
//       return sample;
//   }
