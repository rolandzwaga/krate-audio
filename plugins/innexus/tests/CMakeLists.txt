# ==============================================================================
# Innexus Plugin Tests
# ==============================================================================

add_executable(innexus_tests
    unit/test_main.cpp

    # VST3 validation and parameter tests
    unit/vst/innexus_vst_tests.cpp

    # Pre-processing pipeline tests (Phase 2)
    unit/processor/pre_processing_pipeline_tests.cpp

    # Sample analyzer tests (Phase 10)
    unit/processor/sample_analyzer_tests.cpp

    # MIDI integration and state persistence tests (Phase 11)
    unit/processor/innexus_processor_tests.cpp

    # Residual integration tests (M2 Phases 10-11)
    unit/processor/residual_integration_tests.cpp

    # Processor/Controller implementation (needed by plugin shell tests)
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/processor/processor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/controller/controller.cpp

    # Sample analyzer implementation
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/dsp/sample_analyzer.cpp

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
