// ==============================================================================
// Global allocation-operator replacements for AllocationDetector
// ==============================================================================
// Include this header from EXACTLY ONE translation unit per test binary. It
// defines the global operator new / delete replacements that let
// TestHelpers::AllocationDetector observe heap allocations, and it is the only
// place in the repo allowed to define them.
//
//     // in one test TU of the binary:
//     #include <allocation_operator_overrides.h>
//
// Why this exists rather than each test rolling its own: replacing these
// operators is only well-defined if the WHOLE matched set is replaced and the
// replacements are visible program-wide. Three separate hand-rolled copies had
// drifted into two different broken shapes, both of which corrupt the heap and
// surface as an intermittent, test-order-dependent SIGSEGV on Linux CI that
// never reproduces in isolation and never appears on Windows:
//
//   1. Replacing new AND delete but compiled under -fvisibility=hidden. Hidden
//      symbols do not interpose libstdc++, so a std::string allocated inside
//      libstdc++ but destroyed in executable code was freed by the TU's free()
//      while libstdc++ had allocated it.
//   2. Replacing ONLY new / new[] and leaving operator delete at the library
//      default. That looks safe on glibc -- the default delete does call free
//      -- but [new.delete] requires a replaced operator new to be paired with a
//      replaced operator delete; the pair must agree on which allocator owns
//      the block. AddressSanitizer reports it as
//      "alloc-dealloc-mismatch (malloc vs operator delete)". Measured on
//      membrum_tests: 694,513 such errors, the first during Catch2's static
//      test registration -- before any test body runs, so everything after it
//      inherits a poisoned allocator.
//
// This header does it properly: the entire matched set (throwing, nothrow,
// array and sized forms) on malloc/free so new and delete always agree, with
// default visibility so exactly one definition serves the whole process,
// libstdc++ included. One allocator, one matched set, no mismatch left to hit.
//
// tools/lint-allocation-operator-overrides.js enforces that no other file
// defines these operators.
//
// noinline is load-bearing, for valgrind: memcheck intercepts the OUTLINE
// operator new/delete symbols process-wide (including ours), but GCC inlines
// a same-TU replacement operator delete into its callers, leaving a bare
// free() at the call site. Valgrind then sees "allocated by (intercepted)
// operator new, freed by free()" and reports a spurious
// "Mismatched free() / delete" -- 78 of them across membrum_tests, enough to
// redden the valgrind-nightly lane. Natively both paths are plain
// malloc/free, so this is purely a bookkeeping artifact. Keeping every form
// noinline means call sites always go through the interceptable symbol, and
// valgrind's alloc/free buckets stay consistent.
// ==============================================================================

#pragma once

#include <allocation_detector.h>

#include <cstdlib>
#include <new>

#if defined(_MSC_VER)
#  define KRATE_ALLOC_REPLACEMENT __declspec(noinline)
#else
#  define KRATE_ALLOC_REPLACEMENT __attribute__((visibility("default"), noinline))
#endif

KRATE_ALLOC_REPLACEMENT void* operator new(std::size_t size)
{
    TestHelpers::AllocationDetector::instance().recordAllocation();
    void* p = std::malloc(size ? size : 1);
    if (!p) throw std::bad_alloc();
    return p;
}

KRATE_ALLOC_REPLACEMENT void* operator new[](std::size_t size)
{
    TestHelpers::AllocationDetector::instance().recordAllocation();
    void* p = std::malloc(size ? size : 1);
    if (!p) throw std::bad_alloc();
    return p;
}

KRATE_ALLOC_REPLACEMENT void* operator new(
    std::size_t size, const std::nothrow_t&) noexcept
{
    TestHelpers::AllocationDetector::instance().recordAllocation();
    return std::malloc(size ? size : 1);
}

KRATE_ALLOC_REPLACEMENT void* operator new[](
    std::size_t size, const std::nothrow_t&) noexcept
{
    TestHelpers::AllocationDetector::instance().recordAllocation();
    return std::malloc(size ? size : 1);
}

KRATE_ALLOC_REPLACEMENT void operator delete(void* p) noexcept
{
    std::free(p);
}

KRATE_ALLOC_REPLACEMENT void operator delete[](void* p) noexcept
{
    std::free(p);
}

KRATE_ALLOC_REPLACEMENT void operator delete(void* p, std::size_t) noexcept
{
    std::free(p);
}

KRATE_ALLOC_REPLACEMENT void operator delete[](void* p, std::size_t) noexcept
{
    std::free(p);
}

KRATE_ALLOC_REPLACEMENT void operator delete(
    void* p, const std::nothrow_t&) noexcept
{
    std::free(p);
}

KRATE_ALLOC_REPLACEMENT void operator delete[](
    void* p, const std::nothrow_t&) noexcept
{
    std::free(p);
}
