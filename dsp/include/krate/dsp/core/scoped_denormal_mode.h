#pragma once

// ==============================================================================
// Scoped Denormal Mode (FTZ/DAZ)
// ==============================================================================
// RAII guard that enables flush-to-zero / denormals-are-zero for the duration of
// a scope and restores the caller's MXCSR bits on exit.
//
// Why scoped, and why not in setupProcessing():
//   MXCSR is *per-thread* CPU state. Hosts call setupProcessing() while the
//   plugin is in setup state -- typically the main/UI thread -- whereas
//   process() runs on a dedicated audio thread, and some hosts rotate plugins
//   across a render thread pool. Setting FTZ/DAZ in setupProcessing() therefore
//   configures a thread that never runs any DSP: the flags silently fail to
//   apply where they matter. Constructing this guard at the top of process()
//   is the portable fix -- it is two register writes per block, far below the
//   cost of a single denormal trap (hundreds of cycles per operation).
//
// Restoring on exit matters because the host owns the FP environment. Leaving
// FTZ/DAZ set on a shared render thread would silently change the arithmetic of
// the next plugin in the chain, which is not ours to decide.
//
// Long exponential tails (ADSR release, residual overlap-add, sympathetic
// resonance, modal decay) are the usual denormal sources in this codebase.
//
// Non-x86 targets (e.g. AArch64) compile this to an empty, zero-cost object:
// ARM NEON flushes denormals by default in the common FPCR configuration.
// ==============================================================================

#if defined(__SSE2__) || defined(_M_X64) || defined(__x86_64__) \
    || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#define KRATE_HAS_SSE_DENORMAL_CONTROL 1
#include <xmmintrin.h> // _MM_SET_FLUSH_ZERO_MODE
#include <pmmintrin.h> // _MM_SET_DENORMALS_ZERO_MODE
#else
#define KRATE_HAS_SSE_DENORMAL_CONTROL 0
#endif

namespace Krate::DSP {

/// @brief RAII guard enabling FTZ/DAZ for the current scope on the current thread.
///
/// Construct at the top of the audio callback:
/// @code
///   tresult PLUGIN_API Processor::process(ProcessData& data) {
///       const Krate::DSP::ScopedDenormalMode denormalGuard;
///       ...
///   }
/// @endcode
///
/// @note Real-time safe: no allocations, no locks, no syscalls.
/// @note Non-copyable and non-movable -- the guard must not outlive its scope.
class ScopedDenormalMode {
public:
    ScopedDenormalMode() noexcept {
#if KRATE_HAS_SSE_DENORMAL_CONTROL
        savedFlushZero_ = _MM_GET_FLUSH_ZERO_MODE();
        savedDenormalsZero_ = _MM_GET_DENORMALS_ZERO_MODE();
        _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
        _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#endif
    }

    ~ScopedDenormalMode() noexcept {
#if KRATE_HAS_SSE_DENORMAL_CONTROL
        _MM_SET_FLUSH_ZERO_MODE(savedFlushZero_);
        _MM_SET_DENORMALS_ZERO_MODE(savedDenormalsZero_);
#endif
    }

    ScopedDenormalMode(const ScopedDenormalMode&) = delete;
    ScopedDenormalMode& operator=(const ScopedDenormalMode&) = delete;
    ScopedDenormalMode(ScopedDenormalMode&&) = delete;
    ScopedDenormalMode& operator=(ScopedDenormalMode&&) = delete;

private:
#if KRATE_HAS_SSE_DENORMAL_CONTROL
    unsigned int savedFlushZero_{0};
    unsigned int savedDenormalsZero_{0};
#endif
};

} // namespace Krate::DSP
