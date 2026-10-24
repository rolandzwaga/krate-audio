# ==============================================================================
# Profundum Plugin Tests
# ==============================================================================

# This list is ENUMERATED, not globbed - an unregistered TU silently drops out of
# the build and its cases never run (Catch2 reports "No tests ran", exit code 0).
add_executable(profundum_tests
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

    # Phase 11
    unit/param_table_test.cpp
    unit/state_v2_test.cpp
    unit/midi_mapping_test.cpp
    unit/phase11_alloc_test.cpp
    integration/param_wiring_test.cpp
    integration/state_restore_render_test.cpp
    integration/copy_compare_test.cpp
    integration/tail_test.cpp
    integration/param_path_audibility_test.cpp
    integration/mono_sum_params_test.cpp
    integration/determinism_test.cpp
    integration/band_level_metric_test.cpp

    # Phase 12
    integration/main_page_render_set_test.cpp   # [.listen] FR-006 render set (T010; P-1 ruling, spec FR-001)
    unit/spectrum_producer_test.cpp
    unit/state_v3_test.cpp
    unit/controller/editor_layout_test.cpp
    unit/controller/morph_component_test.cpp
    unit/controller/macro_ui_test.cpp
    unit/controller/spread_curve_test.cpp
    unit/controller/editor_sync_test.cpp
    unit/controller/custom_view_laws_test.cpp
    unit/controller/ui_views_test.cpp
    integration/spectrum_frame_test.cpp
    integration/main_page_sanity_test.cpp
    integration/main_page_audibility_test.cpp   # SC-006 (a) rows (T039, ruled 2026-10-24)

    # SECOND compilation of every plugin .cpp (needed by the plugin-shell tests).
    # A new plugin .cpp must appear here AND in plugins/profundum/CMakeLists.txt.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/processor/processor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/processor/goertzel_bank_simd.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/controller/controller.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/harmonic_bar_view.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/spread_curve_view.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/offset_ring_knob.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/panel_sub_controller.cpp

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
target_link_libraries(profundum_tests
    PRIVATE
        KrateDSP
        hwy  # T040: goertzel_bank_simd.cpp
        KratePluginsShared
        Catch2::Catch2
        test_helpers
        vstgui_support
        sdk
)

target_include_directories(profundum_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/tests
        ${vst3sdk_SOURCE_DIR}
        ${vst3sdk_SOURCE_DIR}/vstgui4
        # read-only Phase 9 test helpers for SC-018 / SC-019 (plan 6.4; FR-070 forbids edits, not includes)
        ${CMAKE_SOURCE_DIR}/dsp/tests/unit/systems
)

target_compile_features(profundum_tests PRIVATE cxx_std_20)

# Tests locate resources/editor.uidesc through PROFUNDUM_RESOURCES_DIR.
target_compile_definitions(profundum_tests
    PRIVATE
        PROFUNDUM_RESOURCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../resources"
)

# Disable -ffast-math for IEEE 754 compliant NaN/Inf handling.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set_source_files_properties(
        unit/param_denorm_test.cpp         # normalized NaN
        unit/state_roundtrip_test.cpp      # NaN / +Inf per float field
        unit/lifecycle_test.cpp            # NaN sample rate; bit-pattern finiteness
        unit/midi_event_test.cpp           # bit-pattern finiteness over renders
        integration/processor_audio_test.cpp
        integration/param_flow_test.cpp
        # Phase 11 (build non-finite values or compare bits)
        unit/param_table_test.cpp
        unit/state_v2_test.cpp
        unit/midi_mapping_test.cpp
        unit/phase11_alloc_test.cpp
        integration/param_wiring_test.cpp
        integration/state_restore_render_test.cpp
        integration/copy_compare_test.cpp
        integration/tail_test.cpp
        integration/param_path_audibility_test.cpp
        integration/mono_sum_params_test.cpp
        integration/determinism_test.cpp
        integration/band_level_metric_test.cpp
        # Phase 12
        integration/main_page_render_set_test.cpp  # bit-pattern finiteness over renders
        unit/spectrum_producer_test.cpp            # NaN f0 / NaN sample; bit-pattern normal check
        unit/state_v3_test.cpp
        integration/spectrum_frame_test.cpp
        integration/main_page_sanity_test.cpp
        integration/main_page_audibility_test.cpp
        PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
    )
    # integration/processor_cpu_test.cpp is DELIBERATELY ABSENT: the timed code must
    # run in the shipping FP mode (plugins/vorago/tests/CMakeLists.txt:159-160).
endif()

# CTest Integration
catch_discover_tests(profundum_tests REPORTER console)

# Precompiled Catch2 + STL when KRATE_TEST_PCH is ON (cmake/KrateTestPch.cmake); after the per-source flag blocks above (P-9).
krate_test_pch(profundum_tests)
