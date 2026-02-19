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

    # Harmonizer Parameter Tests (067-ruinae-harmonizer)
    unit/harmonizer_params_test.cpp

    # Plugin Shell Tests (045-plugin-shell)
    unit/processor_bus_test.cpp
    unit/param_denorm_test.cpp
    unit/state_roundtrip_test.cpp
    unit/state_migration_test.cpp
    unit/controller_params_test.cpp
    unit/controller_display_test.cpp

    # Macro/Rungler Parameter Tests (057-macros-rungler)
    unit/macro_params_test.cpp
    unit/rungler_params_test.cpp

    # Settings Parameter Tests (058-settings-drawer)
    unit/settings_params_test.cpp

    # Mod Matrix Tests (049-mod-matrix-grid)
    unit/mod_matrix_params_test.cpp
    integration/mod_matrix_grid_test.cpp
    integration/mod_ring_indicator_test.cpp
    integration/processor_audio_test.cpp
    integration/midi_events_test.cpp
    integration/tempo_sync_test.cpp
    integration/param_flow_test.cpp
    integration/trance_gate_param_flow_test.cpp
    integration/harmonizer_param_flow_test.cpp
    integration/controller_state_test.cpp
    integration/mod_source_pipeline_test.cpp

    # Processor/Controller implementation (needed by plugin shell tests)
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/processor/processor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/controller/controller.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp

    # Stub for GetPluginFactory (provided by entry.cpp in the real plugin, not in tests)
    vstgui_test_stubs.cpp
)

target_link_libraries(ruinae_tests
    PRIVATE
        KrateDSP
        KratePluginsShared
        Catch2::Catch2
        test_helpers
        vstgui_support
        sdk
)

target_include_directories(ruinae_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_SOURCE_DIR}/tests
        ${vst3sdk_SOURCE_DIR}
        ${vst3sdk_SOURCE_DIR}/vstgui4
)

target_compile_features(ruinae_tests PRIVATE cxx_std_20)

# Disable -ffast-math for IEEE 754 compliant NaN handling
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set_source_files_properties(
        unit/ruinae_engine_test.cpp
        unit/ruinae_engine_integration_test.cpp
        unit/ruinae_voice_test.cpp
        unit/ruinae_effects_chain_test.cpp
        integration/processor_audio_test.cpp
        integration/midi_events_test.cpp
        integration/tempo_sync_test.cpp
        integration/param_flow_test.cpp
        integration/trance_gate_param_flow_test.cpp
        integration/harmonizer_param_flow_test.cpp
        integration/controller_state_test.cpp
        integration/mod_source_pipeline_test.cpp
        unit/controller_params_test.cpp
        unit/controller_display_test.cpp
        PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
    )
endif()

# CTest Integration
catch_discover_tests(ruinae_tests REPORTER console)
