# ==============================================================================
# Gradus Plugin Tests
# ==============================================================================

add_executable(gradus_tests
    unit/test_main.cpp

    # Ring geometry unit tests (no VSTGUI dependency)
    unit/ui/ring_geometry_tests.cpp

    # Pin flag strip (v1.6 Step Pinning UI) unit tests
    unit/ui/pin_flag_strip_tests.cpp

    # Markov matrix editor unit tests
    unit/ui/markov_matrix_editor_tests.cpp

    # Speed curve data model unit tests
    unit/ui/speed_curve_data_tests.cpp

    # VST3 validation and parameter tests
    unit/vst/gradus_vst_tests.cpp

    # Processor/Controller implementation (needed by plugin shell tests)
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/processor/processor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/controller/controller.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/controller/controller_arp.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/controller/controller_verify_view.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/controller/controller_view_sync.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/controller/controller_presets.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp

    # SDK hosting helpers (HostMessage, HostAttributeList) for IMessage tests
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/hostclasses.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp

    # Stub for GetPluginFactory
    vstgui_test_stubs.cpp
)

target_link_libraries(gradus_tests
    PRIVATE
        KrateDSP
        KratePluginsShared
        Catch2::Catch2
        test_helpers
        vstgui_support
        sdk
)

target_include_directories(gradus_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/tests
        ${vst3sdk_SOURCE_DIR}
        ${vst3sdk_SOURCE_DIR}/vstgui4
)

target_compile_features(gradus_tests PRIVATE cxx_std_20)

# Compiler warnings matching the plugin target
if(MSVC)
    target_compile_options(gradus_tests PRIVATE
        /W4
        /permissive-
        /Zc:__cplusplus
        /wd4100
        /wd4458
    )
else()
    target_compile_options(gradus_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter
    )
endif()

# CTest Integration
catch_discover_tests(gradus_tests REPORTER console)
