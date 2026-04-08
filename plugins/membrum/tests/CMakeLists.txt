# ==============================================================================
# Membrum Plugin Tests
# ==============================================================================

add_executable(membrum_tests
    unit/test_main.cpp

    # VST3 validation and parameter tests
    unit/vst/membrum_vst_tests.cpp

    # Processor tests
    unit/processor/membrum_processor_tests.cpp

    # Processor/Controller implementation (needed by plugin shell tests)
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/processor/processor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/controller/controller.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp

    # SDK hosting helpers (HostMessage, HostAttributeList) for IMessage tests
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/hostclasses.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp

    # Stub for GetPluginFactory
    vstgui_test_stubs.cpp
)

target_link_libraries(membrum_tests
    PRIVATE
        KrateDSP
        KratePluginsShared
        Catch2::Catch2
        test_helpers
        sdk
)

target_include_directories(membrum_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/tests
        ${vst3sdk_SOURCE_DIR}
)

target_compile_features(membrum_tests PRIVATE cxx_std_20)

# Compiler warnings matching the plugin target
if(MSVC)
    target_compile_options(membrum_tests PRIVATE
        /W4
        /permissive-
        /Zc:__cplusplus
        /wd4100
        /wd4458
    )
else()
    target_compile_options(membrum_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter
    )
endif()

# T047: NaN/Inf detection in membrum_processor_tests.cpp uses bit manipulation
# (isNaNBits/isInfBits via memcpy + integer comparison), NOT std::isnan/std::isfinite.
# Therefore, -fno-fast-math is NOT needed for this test file.

# CTest Integration
catch_discover_tests(membrum_tests REPORTER console)
