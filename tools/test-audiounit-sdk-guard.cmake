# Self-test for krate_check_audiounit_sdk_standard (cmake/KratePlugin.cmake).
# Usage: cmake -P tools/test-audiounit-sdk-guard.cmake        (prints AUDIOUNIT SDK GUARD: N cases OK | FAILED)
#
# Each case writes a fixture AudioUnitSDK.xcodeproj/project.pbxproj and runs the guard in a child `cmake -P`
# (FATAL_ERROR ends a script, so the verdict is the child's exit code). The c++23 line is the one AudioUnitSDK 1.4.0
# ships, the pin that broke every *_AU target twice (PR #293, 89f7b1c3).
cmake_minimum_required(VERSION 3.20)

if(DEFINED KRATE_GUARD_SDK_DIR)
    include("${CMAKE_CURRENT_LIST_DIR}/../cmake/KratePlugin.cmake")
    krate_check_audiounit_sdk_standard("${KRATE_GUARD_SDK_DIR}" 20)
    return()
endif()

set(_root "${CMAKE_CURRENT_LIST_DIR}/../build/audiounit-sdk-guard-test")
file(REMOVE_RECURSE "${_root}")
# name | pbxproj lines, `^` between lines, each written with its ';' ("" = no project file) | expected exit: 0 pass,
# 1 fail. (No ';' in the table: it is CMake's list separator.)
set(_cases
    "c++20 (1.3.0)|CLANG_CXX_LANGUAGE_STANDARD = \"c++20\"|0"
    "c++17 (1.1.0)|CLANG_CXX_LANGUAGE_STANDARD = \"c++17\"|0"
    "gnu++2a|CLANG_CXX_LANGUAGE_STANDARD = \"gnu++2a\"|0"
    "c++23 (1.4.0)|CLANG_CXX_LANGUAGE_STANDARD = \"c++23\"|1"
    "gnu++2b|CLANG_CXX_LANGUAGE_STANDARD = \"gnu++2b\"|1"
    "mixed 20 + 23|CLANG_CXX_LANGUAGE_STANDARD = \"c++20\"^CLANG_CXX_LANGUAGE_STANDARD = \"c++23\"|1"
    "no standard line|ALWAYS_SEARCH_USER_PATHS = NO|1"
    "no project file||1"
)
set(_failed 0)
set(_n 0)
foreach(_case IN LISTS _cases)
    string(REPLACE "|" ";" _parts "${_case}")
    list(GET _parts 0 _name)
    list(GET _parts 1 _body)
    list(GET _parts 2 _want)
    math(EXPR _n "${_n} + 1")
    set(_dir "${_root}/case${_n}")
    file(MAKE_DIRECTORY "${_dir}")
    if(NOT _body STREQUAL "")
        string(REPLACE "^" "\;\n" _body "${_body}")
        file(WRITE "${_dir}/AudioUnitSDK.xcodeproj/project.pbxproj" "${_body}\;\n")
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -DKRATE_GUARD_SDK_DIR=${_dir} -P "${CMAKE_CURRENT_LIST_FILE}"
        RESULT_VARIABLE _rc OUTPUT_QUIET ERROR_QUIET)
    if(_rc EQUAL 0)
        set(_got 0)
    else()
        set(_got 1)
    endif()
    if(NOT _got EQUAL _want)
        math(EXPR _failed "${_failed} + 1")
        message("AUDIOUNIT SDK GUARD self-test FAIL: ${_name}: want exit ${_want}, got ${_rc}")
    endif()
endforeach()
file(REMOVE_RECURSE "${_root}")
if(_failed)
    message(FATAL_ERROR "AUDIOUNIT SDK GUARD: ${_failed} of ${_n} cases FAILED")
endif()
message("AUDIOUNIT SDK GUARD: ${_n} cases OK")
