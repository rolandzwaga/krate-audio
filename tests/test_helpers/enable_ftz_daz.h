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

// Platform detection: x86/x64 covers MSVC (_M_X64/_M_IX86), Clang/GCC (__x86_64__/__i386__),
// and the legacy __SSE__ macro (GCC/Clang only — MSVC does NOT define __SSE__ for x64
// builds even when SSE is available, which is why the older `#if defined(__SSE__)` guard
// silently no-op'd on Windows and caused denormal-driven 10× CPU spikes in modal DSP tests).
#if defined(_M_X64) || defined(__x86_64__) || defined(_M_IX86) || defined(__i386__) || defined(__SSE__)
#define KRATE_TEST_FTZ_DAZ_X86 1
#include <xmmintrin.h> // _MM_SET_FLUSH_ZERO_MODE
#include <pmmintrin.h> // _MM_SET_DENORMALS_ZERO_MODE
#endif

inline void enableFTZDAZ() {
#ifdef KRATE_TEST_FTZ_DAZ_X86
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#endif
}
