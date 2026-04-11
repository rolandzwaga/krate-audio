# ==============================================================================
# Membrum Plugin Tests
# ==============================================================================

add_executable(membrum_tests
    unit/test_main.cpp

    # VST3 validation and parameter tests
    unit/vst/membrum_vst_tests.cpp
    unit/vst/test_membrum_parameters_v2.cpp
    unit/vst/test_state_roundtrip_v2.cpp

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
