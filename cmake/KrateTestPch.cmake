# ==============================================================================
# KrateTestPch.cmake - precompiled header for the test executables
# ==============================================================================
# The CI Windows build is 70 % test translation units (1093 of 1571 in the CI
# target closure, measured 2026-10-09), each re-parsing Catch2 and the standard
# library. Nothing between runs can be cached on the GitHub Windows runner, but
# a PCH needs no cache: measured on dsp_systems_tests (98 TUs, clean, MSVC
# 2022, capped to four compile processes like the 4-vCPU runner) 115 s without
# and 87 s with.
#
# Catch2 and the STL only. No krate header goes in, so no TU's per-source
# fast-math setting or test-hook definition can leak into another. Sources that
# carry their own COMPILE_FLAGS / COMPILE_OPTIONS (the -fno-fast-math blocks)
# are excluded: a PCH compiled with different options is rejected by MSVC
# (C2855) and silently unused by GCC, so those files keep the plain path.
#
# Usage: krate_test_pch(<test target>) AFTER every set_source_files_properties()
# call for that target's sources. A no-op unless KRATE_TEST_PCH is ON; the CI
# Windows configure turns it on.

option(KRATE_TEST_PCH "Precompile Catch2 + the standard library for every test executable" OFF)

function(krate_test_pch target)
    if(NOT KRATE_TEST_PCH)
        return()
    endif()
    target_precompile_headers(${target} PRIVATE
        <catch2/catch_all.hpp>
        <algorithm> <array> <chrono> <cmath> <cstddef> <cstdint> <cstring>
        <iomanip> <limits> <memory> <numeric> <span> <sstream> <string> <vector>)
    get_target_property(_sources ${target} SOURCES)
    foreach(_source IN LISTS _sources)
        get_source_file_property(_flags "${_source}" COMPILE_FLAGS)
        get_source_file_property(_options "${_source}" COMPILE_OPTIONS)
        if((_flags AND NOT _flags STREQUAL "NOTFOUND") OR (_options AND NOT _options STREQUAL "NOTFOUND"))
            set_source_files_properties("${_source}" PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
        endif()
    endforeach()
endfunction()
