// ==============================================================================
// CONTRACT: MinBlepTable Extension - MinBLAMP Support
// ==============================================================================
// Extension to the existing MinBlepTable to add minBLAMP (band-limited ramp)
// correction capability for derivative discontinuities (e.g., reverse sync).
//
// This contract documents the NEW methods/fields to add to the existing
// MinBlepTable class in primitives/minblep_table.h. Existing API is unchanged.
//
// Reference: specs/018-oscillator-sync/spec.md (FR-027a)
// ==============================================================================

// ============================================================================
// NEW: MinBlepTable additions
// ============================================================================

// In MinBlepTable class, add AFTER existing table_ member:
//
//   std::vector<float> blampTable_;   // Precomputed minBLAMP table
//
// In MinBlepTable::prepare(), add AFTER step 5 (table storage):
//
//   // Step 6: Compute minBLAMP table by integrating minBLEP residual
//   blampTable_.resize(tableSize);
//   for (size_t sub = 0; sub < oversamplingFactor_; ++sub) {
//       float runningSum = 0.0f;
//       for (size_t idx = 0; idx < length_; ++idx) {
//           size_t tableIdx = idx * oversamplingFactor_ + sub;
//           float blepResidual = table_[tableIdx] - 1.0f;
//           runningSum += blepResidual;
//           blampTable_[tableIdx] = runningSum;
//       }
//   }

// NEW method: sampleBlamp()
// Same interface as sample() but reads from blampTable_.
//
//   [[nodiscard]] float sampleBlamp(float subsampleOffset, size_t index) const noexcept;

// ============================================================================
// NEW: Residual::addBlamp()
// ============================================================================

// In MinBlepTable::Residual, add:
//
//   void addBlamp(float subsampleOffset, float amplitude) noexcept {
//       if (detail::isNaN(amplitude) || detail::isInf(amplitude)) return;
//       if (table_ == nullptr || buffer_.empty()) return;
//
//       const size_t len = buffer_.size();
//       for (size_t i = 0; i < len; ++i) {
//           float tableVal = table_->sampleBlamp(subsampleOffset, i);
//           float correction = amplitude * tableVal;
//           buffer_[(readIdx_ + i) % len] += correction;
//       }
//   }
