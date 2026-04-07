// Contract: ModalResonatorBank API extension for mallet choke (FR-035)
// Location: dsp/include/krate/dsp/processors/modal_resonator_bank.h
// This documents the NEW overload to be added alongside the existing API.

// Existing (unchanged):
//   float processSample(float excitation) noexcept;
//   void processBlock(const float* input, float* output, int numSamples) noexcept;

// NEW overload:

/// Process a single sample with optional decay scaling (mallet choke).
/// @param excitation Input excitation signal
/// @param decayScale Decay acceleration factor:
///   - 1.0f = normal operation (no choke)
///   - >1.0f = accelerated decay (choke), applied as R_eff = pow(R, decayScale)
///   - Preserves relative damping between modes (material character retained)
/// @return Resonator output sample
/// @note The pow() per mode is only computed when decayScale != 1.0f
float processSample(float excitation, float decayScale) noexcept;

// The existing processSample(float) delegates:
//   float processSample(float excitation) noexcept {
//       return processSample(excitation, 1.0f);
//   }

// NEW block overload (optional, for consistency):
void processBlock(const float* input, float* output, int numSamples,
                  float decayScale) noexcept;
