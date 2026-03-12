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

    # Spectral decay envelope unit tests
    unit/processor/test_spectral_decay_envelope.cpp

    # Harmonic Physics unit tests (Spec A)
    unit/processor/test_harmonic_physics.cpp

    # ADSR Envelope Detection unit tests (Spec 124)
    unit/processor/test_envelope_detector.cpp
    unit/processor/test_memory_slot_adsr.cpp

    # State v7 tests (Spec A)
    unit/vst/test_state_v7.cpp

    # State v8 tests (Spec B: Analysis Feedback Loop)
    unit/vst/test_state_v8.cpp

    # State v9 tests (Spec 124: ADSR Envelope Detection)
    unit/vst/test_state_v9.cpp

    # ADSR VST parameter registration tests (Spec 124: T026)
    unit/vst/test_adsr_vst_params.cpp

    # State v10 tests (Modulator Tempo Sync)
    unit/vst/test_state_v10.cpp

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

    # Harmonic Physics integration tests (Spec A)
    integration/test_harmonic_physics_integration.cpp

    # Analysis Feedback Loop integration tests (Spec B)
    integration/test_analysis_feedback.cpp

    # Modulator Tempo Sync integration tests
    integration/test_modulator_sync.cpp

    # ADSR Envelope Detection integration tests (Spec 124)
    integration/test_adsr_envelope.cpp
    integration/test_adsr_phase8_verification.cpp

    # Reset trap regression tests (per-block setter safety)
    integration/reset_trap_tests.cpp

    # Sidechain signal end behavior (graceful decay)
    integration/test_sidechain_signal_end.cpp

    # Sidechain WAV quality diagnostics (real audio files)
    integration/test_sidechain_wav_quality.cpp

    # Enable/disable while playing + degraded host conditions
    integration/enable_disable_tests.cpp

    # End-to-end: WAV file → SampleAnalyzer → Processor → pitched audio
    integration/sample_load_e2e_tests.cpp

    # Processor/Controller implementation (needed by plugin shell tests)
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/processor/processor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/processor/processor_params.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/processor/processor_state.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/processor/processor_midi.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/processor/processor_messages.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/controller/controller.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/controller/controller_presets.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp

    # Sample analyzer implementation
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/dsp/sample_analyzer.cpp

    # Live analysis pipeline implementation
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/dsp/live_analysis_pipeline.cpp

    # SDK hosting helpers (HostMessage, HostAttributeList) for IMessage tests
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/hostclasses.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp

    # UI controller tests (M7)
    unit/controller/test_harmonic_display_view.cpp
    unit/controller/test_confidence_indicator_view.cpp
    unit/controller/test_memory_slot_status_view.cpp
    unit/controller/test_evolution_position_view.cpp
    unit/controller/test_modulator_activity_view.cpp
    unit/controller/test_modulator_sub_controller.cpp
    unit/controller/test_controller_ui.cpp
    unit/controller/test_sample_drop.cpp
    unit/controller/test_adsr_display_wiring.cpp

    # UI custom view implementations (needed by controller tests)
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/controller/views/harmonic_display_view.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/controller/views/confidence_indicator_view.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/controller/views/memory_slot_status_view.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/controller/views/evolution_position_view.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/controller/views/modulator_activity_view.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/controller/modulator_sub_controller.cpp

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

# Disable -ffast-math for files requiring IEEE 754 compliant NaN/Inf handling.
# The VST3 SDK enables -ffast-math globally (see SMTG_PlatformToolset.cmake),
# which breaks std::isnan / std::isinf. Only needed for GCC/Clang; MSVC is unaffected.
if(NOT MSVC)
    set_source_files_properties(
        integration/test_adsr_envelope.cpp
        integration/test_adsr_phase8_verification.cpp
        integration/musical_control_tests.cpp
        integration/enable_disable_tests.cpp
        integration/reset_trap_tests.cpp
        integration/sidechain_integration_tests.cpp
        integration/test_sidechain_wav_quality.cpp
        PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
    )
endif()

# CTest Integration
catch_discover_tests(innexus_tests REPORTER console)
