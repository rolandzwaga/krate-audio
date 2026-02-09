# ==============================================================================
# Ruinae Plugin Tests
# ==============================================================================
# Engine tests (moved from dsp/tests/) and plugin-specific tests.
# ==============================================================================

# ==============================================================================
# Engine Tests (RuinaeEngine, RuinaeVoice, RuinaeEffectsChain)
# ==============================================================================
add_executable(ruinae_tests
    unit/test_main.cpp
    unit/ruinae_engine_test.cpp
    unit/ruinae_engine_integration_test.cpp
    unit/ruinae_voice_test.cpp
    unit/ruinae_effects_chain_test.cpp
)

target_link_libraries(ruinae_tests
    PRIVATE
        KrateDSP
        KratePluginsShared
        Catch2::Catch2
        test_helpers
        sdk
        vstgui_support
)

target_include_directories(ruinae_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_SOURCE_DIR}/tests
        ${vst3sdk_SOURCE_DIR}
)

target_compile_features(ruinae_tests PRIVATE cxx_std_20)

# Disable -ffast-math for IEEE 754 compliant NaN handling
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set_source_files_properties(
        unit/ruinae_engine_test.cpp
        unit/ruinae_engine_integration_test.cpp
        unit/ruinae_voice_test.cpp
        unit/ruinae_effects_chain_test.cpp
        PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
    )
endif()

# CTest Integration
catch_discover_tests(ruinae_tests REPORTER console)
