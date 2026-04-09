// ==============================================================================
// FTZ/DAZ (Flush-To-Zero / Denormals-Are-Zero) Test Helper
// ==============================================================================
// On x86 Linux and macOS, FTZ/DAZ is NOT enabled by default (unlike MSVC on
// Windows). Without it, reverb feedback networks and other DSP with decaying
// signals produce denormals, causing 100x+ CPU slowdown in tests.
//
// In production, the DAW sets FTZ/DAZ before calling process(). Tests must do
// it themselves. Call enableFTZDAZ() at the top of main() in every test binary.
//
// ARM (Apple Silicon, etc.) flushes denormals by default -- no action needed.
// ==============================================================================

#pragma once

#if defined(__SSE__)
#include <xmmintrin.h> // _MM_SET_FLUSH_ZERO_MODE
#include <pmmintrin.h> // _MM_SET_DENORMALS_ZERO_MODE
#endif

inline void enableFTZDAZ() {
#if defined(__SSE__)
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#endif
}
