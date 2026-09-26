# ==============================================================================
# Vorago Plugin Tests
# ==============================================================================

# This list is ENUMERATED, not globbed - an unregistered TU silently drops out of
# the build and its cases never run (Catch2 reports "No tests ran", exit code 0).
add_executable(vorago_tests
    unit/test_main.cpp
    unit/processor_bus_test.cpp
    unit/param_denorm_test.cpp
    unit/state_roundtrip_test.cpp
    unit/midi_event_test.cpp
    unit/lifecycle_test.cpp
    unit/controller/editor_lifecycle_test.cpp
    integration/processor_audio_test.cpp
    integration/param_flow_test.cpp
    integration/processor_cpu_test.cpp
    # Phase 12 (specs/vorago-phase12-parameters)
    unit/state_v2_test.cpp
    unit/param_table_test.cpp
    integration/param_surface_test.cpp
    integration/sustain_test.cpp
    integration/channel_pressure_test.cpp
    integration/seed_test.cpp
    integration/automation_rt_test.cpp
    integration/continuity_test.cpp
    integration/soak_test.cpp
    # Phase 12 parameter packs: one contract TU per pack
    unit/params/global_ext_params_test.cpp
    unit/params/cloud_params_test.cpp
    unit/params/noise_params_test.cpp
    unit/params/resonance_params_test.cpp
    unit/params/ecology_params_test.cpp
    unit/params/sub_params_test.cpp
    unit/params/smear_params_test.cpp
    unit/params/events_params_test.cpp
    unit/params/ecosystem_params_test.cpp
    unit/params/body_params_test.cpp
    unit/params/space_params_test.cpp
    unit/params/envelope_params_test.cpp
    unit/params/bloom_params_test.cpp
    unit/params/ghost_params_test.cpp
    unit/params/life_params_test.cpp

    # SECOND compilation of every plugin .cpp (needed by the plugin-shell tests).
    # A new plugin .cpp must appear here AND in plugins/vorago/CMakeLists.txt.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/processor/processor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/controller/controller.cpp

    # VST3 SDK sources
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp

    # SDK hosting helpers (HostApplication, HostMessage, HostAttributeList)
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/hostclasses.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp

    # Stubs for GetPluginFactory/moduleHandle + SDK module init (Linux linker)
    vstgui_test_stubs.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/moduleinit.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/pluginfactory.cpp
)

# `sdk` AFTER `vstgui_support` - see the comment in vstgui_test_stubs.cpp.
target_link_libraries(vorago_tests
    PRIVATE
        KrateDSP
        KratePluginsShared
        Catch2::Catch2
        test_helpers
        vstgui_support
        sdk
)

target_include_directories(vorago_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/tests
        ${vst3sdk_SOURCE_DIR}
        ${vst3sdk_SOURCE_DIR}/vstgui4
)

target_compile_features(vorago_tests PRIVATE cxx_std_20)

# Tests locate resources/editor.uidesc through VORAGO_RESOURCES_DIR.
# processor_cpu_test.cpp reuses the Phase 10 budget (kReferenceNs) through
# `#include VORAGO_PERF_BUDGET_HEADER`, so the whole dsp/tests/unit/systems
# directory is NOT added to the include path.
target_compile_definitions(vorago_tests
    PRIVATE
        VORAGO_RESOURCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../resources"
        VORAGO_PERF_BUDGET_HEADER="${CMAKE_SOURCE_DIR}/dsp/tests/unit/systems/vorago_perf_budget.h"
)

# Disable -ffast-math for IEEE 754 compliant NaN/Inf handling.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set_source_files_properties(
        unit/state_roundtrip_test.cpp      # SC-010 corrupt-stream arm injects +Inf
        unit/lifecycle_test.cpp            # bit-pattern finiteness over renders
        integration/processor_audio_test.cpp
        integration/param_flow_test.cpp
        unit/state_v2_test.cpp             # SC-008 (5) NaN bit pattern per float field
        unit/param_table_test.cpp          # SC-010 normalized NaN/+-Inf on every ID
        integration/automation_rt_test.cpp # bit-pattern finiteness over renders
        integration/continuity_test.cpp    # bit-pattern finiteness over renders
        integration/soak_test.cpp          # bit-pattern finiteness over renders
        # Phase 12 pack TUs: each injects NaN into its pack's loader
        unit/params/global_ext_params_test.cpp
        unit/params/cloud_params_test.cpp
        unit/params/noise_params_test.cpp
        unit/params/resonance_params_test.cpp
        unit/params/ecology_params_test.cpp
        unit/params/sub_params_test.cpp
        unit/params/smear_params_test.cpp
        unit/params/events_params_test.cpp
        unit/params/ecosystem_params_test.cpp
        unit/params/body_params_test.cpp
        unit/params/space_params_test.cpp
        unit/params/envelope_params_test.cpp
        unit/params/bloom_params_test.cpp
        unit/params/ghost_params_test.cpp
        unit/params/life_params_test.cpp
        PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
    )
    # integration/processor_cpu_test.cpp is DELIBERATELY ABSENT: the flag would
    # move the timed code away from what the Phase 10 budget measured.
endif()

# No /wd4459 here yet: FR-006 adds it to vorago_tests only if the first MSVC
# build of this target actually emits C4459 (decided by T026 from the build log).

# CTest Integration
catch_discover_tests(vorago_tests REPORTER console)
