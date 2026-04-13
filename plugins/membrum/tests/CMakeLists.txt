# ==============================================================================
# Membrum Plugin Tests
# ==============================================================================

add_executable(membrum_tests
    unit/test_main.cpp

    # VST3 validation and parameter tests
    unit/vst/membrum_vst_tests.cpp
    unit/vst/test_membrum_parameters_v2.cpp
    unit/vst/test_state_roundtrip_v2.cpp

    # Phase 3: VST3 parameter + state tests (scaffolded Phase 3.0)
    unit/vst/test_phase3_params.cpp
    unit/vst/test_state_roundtrip_v3.cpp
    unit/vst/test_state_migration_v2_to_v3.cpp
    unit/vst/test_state_migration_v1_to_v3.cpp
    unit/vst/test_state_corruption_clamp.cpp

    # Processor tests
    unit/processor/membrum_processor_tests.cpp

    # Phase 2: architecture refactor tests
    unit/architecture/test_exciter_bank.cpp
    unit/architecture/test_body_bank.cpp

    # Phase 2: exciter tests
    unit/exciters/test_impulse_exciter.cpp
    unit/exciters/test_mallet_exciter.cpp
    unit/exciters/test_noise_burst_exciter.cpp
    unit/exciters/test_friction_exciter.cpp
    unit/exciters/test_fm_impulse_exciter.cpp
    unit/exciters/test_feedback_exciter.cpp
    unit/exciters/test_velocity_mapping.cpp

    # Phase 2: body tests
    unit/bodies/test_membrane_body.cpp
    unit/bodies/test_plate_body.cpp
    unit/bodies/test_shell_body.cpp
    unit/bodies/test_string_body.cpp
    unit/bodies/test_bell_body.cpp
    unit/bodies/test_noise_body.cpp

    # Phase 2: tone shaper tests
    unit/tone_shaper/test_tone_shaper.cpp
    unit/tone_shaper/test_pitch_envelope_808.cpp
    unit/tone_shaper/test_tone_shaper_bypass.cpp

    # Phase 2: unnatural zone tests
    unit/unnatural/test_mode_stretch.cpp
    unit/unnatural/test_decay_skew.cpp
    unit/unnatural/test_mode_inject.cpp
    unit/unnatural/test_nonlinear_coupling.cpp
    unit/unnatural/test_material_morph.cpp

    # Phase 2: top-level cross-cutting tests
    unit/test_exciter_body_matrix.cpp
    unit/test_stability_guard.cpp
    unit/test_allocation_matrix.cpp

    # Phase 2: approval (Phase 1 regression golden)
    approval/test_phase1_regression.cpp

    # Phase 2: CPU benchmarks (tagged [.perf], skipped by default)
    perf/test_benchmark_144.cpp
    perf/test_noise_body_cpu.cpp

    # Phase 3: voice pool unit tests (scaffolded Phase 3.0)
    unit/voice_pool/test_voice_pool_scaffold.cpp
    unit/voice_pool/test_voice_pool_allocate.cpp
    unit/voice_pool/test_voice_pool_stealing_policies.cpp
    unit/voice_pool/test_voice_pool_allocation_free.cpp
    unit/voice_pool/test_steal_click_free.cpp
    unit/voice_pool/test_fast_release_denormals.cpp
    unit/voice_pool/test_fast_release_double_steal.cpp
    unit/voice_pool/test_choke_group.cpp
    unit/voice_pool/test_choke_click_free.cpp
    unit/voice_pool/test_choke_allocation_free.cpp
    unit/voice_pool/test_polyphony_allocation_matrix.cpp
    unit/voice_pool/test_phase2_regression_maxpoly1.cpp
    unit/voice_pool/test_poly_change_live.cpp

    # Phase 4: PadConfig, per-pad parameters, state v4, multi-bus, presets
    unit/vst/test_pad_config.cpp
    unit/vst/test_state_v4.cpp
    unit/vst/test_state_migration_v3_to_v4.cpp
    unit/vst/test_pad_parameters.cpp
    unit/voice_pool/test_per_pad_dispatch.cpp
    unit/voice_pool/test_multi_bus_output.cpp
    unit/vst/test_bus_activation.cpp
    unit/processor/test_default_kit.cpp
    unit/preset/test_kit_preset.cpp
    unit/preset/test_pad_preset.cpp

    # Phase 5: cross-pad coupling tests
    unit/dsp/test_pad_category.cpp
    unit/dsp/test_coupling_matrix.cpp
    unit/processor/test_coupling_integration.cpp
    unit/processor/test_coupling_state.cpp
    unit/processor/test_coupling_energy.cpp
    unit/processor/test_coupling_fuzz.cpp
    unit/processor/test_coupling_phase9_verification.cpp

    # Phase 3: CPU benchmarks / 16-voice stress (tagged [.perf], scaffolded)
    perf/test_polyphony_benchmark.cpp
    perf/test_polyphony_stress_16.cpp

    # Phase 6: macros, session-scoped params, publishers, custom editor, lifecycle
    unit/processor/test_macro_mapper.cpp
    unit/processor/test_state_v6_migration.cpp
    unit/processor/test_pad_glow_publisher.cpp
    unit/processor/test_matrix_activity_publisher.cpp
    unit/controller/test_phase6_parameters.cpp
    unit/controller/test_ui_mode_session_scope.cpp
    unit/controller/test_editor_size_session_scope.cpp
    unit/controller/test_param_reachability_in_editor.cpp
    unit/controller/test_voice_management_ui.cpp
    unit/controller/test_output_routing_ui.cpp
    unit/ui/test_pad_grid_view.cpp
    unit/ui/test_pitch_envelope_display.cpp
    unit/ui/test_coupling_matrix_view.cpp
    unit/preset/test_kit_uimode_roundtrip.cpp
    integration/test_editor_asan_lifecycle.cpp

    # Processor/Controller implementation (needed by plugin shell tests)
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/processor/processor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/processor/macro_mapper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/controller/controller.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/pad_grid_view.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/kit_meters_view.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/coupling_matrix_view.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/pitch_envelope_display.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/voice_pool/voice_pool.cpp
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
        vstgui_support
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

# Phase 3.2 (T3.2.2): test_fast_release_denormals.cpp classifies denormals via
# IEEE-754 exponent bits (memcpy + mask), but tasks.md §NaN Detection Pattern
# mandates that any test reasoning about non-finite or denormal floats be
# compiled with -fno-fast-math so downstream optimisers cannot elide the
# checks. This is a no-op on MSVC (which does not enable -ffast-math by
# default) but required for Clang/GCC.
if(NOT MSVC)
    set_source_files_properties(
        unit/voice_pool/test_fast_release_denormals.cpp
        unit/processor/test_coupling_energy.cpp
        PROPERTIES
            COMPILE_OPTIONS "-fno-fast-math"
    )
endif()

# CTest Integration
catch_discover_tests(membrum_tests REPORTER console)
