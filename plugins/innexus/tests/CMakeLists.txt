# ==============================================================================
# Innexus Plugin Tests
# ==============================================================================

add_executable(innexus_tests
    unit/test_main.cpp

    # --- Unit tests (isolated DSP components) ---

    # VST3 validation and parameter tests
    unit/vst/innexus_vst_tests.cpp
    unit/vst/musical_control_vst_tests.cpp
    unit/vst/harmonic_memory_vst_tests.cpp
    unit/vst/test_state_v6.cpp

    # DSP component unit tests
    unit/processor/pre_processing_pipeline_tests.cpp
    unit/processor/sample_analyzer_tests.cpp
    unit/processor/live_analysis_pipeline_tests.cpp
    unit/processor/test_harmonic_modulator.cpp
    unit/processor/test_harmonic_blender.cpp

    # --- Integration tests (real Processor, full pipeline) ---

    # Core processor integration (MIDI, state, parameter application)
    integration/innexus_processor_tests.cpp

    # Residual pipeline integration (M2)
    integration/residual_integration_tests.cpp

    # Sidechain / live analysis integration (M3)
    integration/sidechain_integration_tests.cpp

    # Musical control integration (M4)
    integration/musical_control_tests.cpp

    # Harmonic memory integration (M5)
    integration/harmonic_memory_tests.cpp

    # Creative extensions integration (M6)
    integration/test_cross_synthesis.cpp
    integration/test_evolution_engine.cpp
    integration/test_stereo_spread_integration.cpp
    integration/test_m6_pipeline_integration.cpp

    # Reset trap regression tests (per-block setter safety)
    integration/reset_trap_tests.cpp

    # Enable/disable while playing + degraded host conditions
    integration/enable_disable_tests.cpp

    # Processor/Controller implementation (needed by plugin shell tests)
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/processor/processor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/controller/controller.cpp

    # Sample analyzer implementation
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/dsp/sample_analyzer.cpp

    # Live analysis pipeline implementation
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/dsp/live_analysis_pipeline.cpp

    # SDK hosting helpers (HostMessage, HostAttributeList) for IMessage tests
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/hostclasses.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp

    # Stub for GetPluginFactory (provided by entry.cpp in the real plugin, not in tests)
    vstgui_test_stubs.cpp
)

target_link_libraries(innexus_tests
    PRIVATE
        KrateDSP
        KratePluginsShared
        Catch2::Catch2
        test_helpers
        vstgui_support
        sdk
)

target_include_directories(innexus_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/tests
        ${CMAKE_SOURCE_DIR}/tools
        ${CMAKE_SOURCE_DIR}/extern/dr_libs
        ${vst3sdk_SOURCE_DIR}
        ${vst3sdk_SOURCE_DIR}/vstgui4
)

target_compile_features(innexus_tests PRIVATE cxx_std_20)

# CTest Integration
catch_discover_tests(innexus_tests REPORTER console)
