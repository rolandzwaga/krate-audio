# ==============================================================================
# KrateDSP Test Configuration
# ==============================================================================
# Constitution Principle VIII: Testing Discipline
# - DSP algorithms MUST be pure functions testable without VST infrastructure
# - Unit tests MUST cover all DSP algorithms
# - Tests MUST run in CI/CD pipeline
# ==============================================================================

# ==============================================================================
# DSP Unit Tests
# ==============================================================================
# Constitution Principle VIII: DSP algorithms must be independently testable

add_executable(dsp_tests
    # Root test file
    unit/test_dsp_utils.cpp

    # Layer 0: Core
    unit/core/db_utils_test.cpp
    unit/core/math_constants_test.cpp
    unit/core/window_functions_test.cpp
    unit/core/random_test.cpp
    unit/core/note_value_test.cpp
    unit/core/note_value_mapping_test.cpp
    unit/core/block_context_test.cpp
    unit/core/fast_math_test.cpp
    unit/core/interpolation_test.cpp
    unit/core/stereo_utils_test.cpp
    unit/core/pitch_utils_test.cpp
    unit/core/grain_envelope_test.cpp
    unit/core/crossfade_utils_test.cpp
    unit/core/sigmoid_test.cpp
    unit/core/chebyshev_test.cpp
    unit/core/test_wavefold_math.cpp
    unit/core/pattern_freeze_types_test.cpp
    unit/core/test_euclidean_pattern.cpp
    unit/core/filter_design_test.cpp
    unit/core/filter_tables_test.cpp
    unit/core/midi_utils_tests.cpp
    unit/core/modulation_types_test.cpp
    unit/core/modulation_curves_test.cpp
    unit/core/polyblep_test.cpp
    unit/core/phase_utils_test.cpp
    unit/core/wavetable_data_test.cpp

    # Layer 1: Primitives
    unit/primitives/delay_line_test.cpp
    unit/primitives/crossfading_delay_line_test.cpp
    unit/primitives/lfo_test.cpp
    unit/primitives/biquad_test.cpp
    unit/primitives/smoother_test.cpp
    unit/primitives/dc_blocker_test.cpp
    unit/primitives/oversampler_test.cpp
    unit/primitives/fft_test.cpp
    unit/primitives/spectral_buffer_test.cpp
    unit/primitives/stft_test.cpp
    unit/primitives/bit_crusher_test.cpp
    unit/primitives/bit_crusher_symmetric_quantization_test.cpp
    unit/primitives/bit_crusher_rng_bias_test.cpp
    unit/primitives/sample_rate_reducer_test.cpp
    unit/primitives/reverse_buffer_test.cpp
    unit/primitives/grain_pool_test.cpp
    unit/primitives/waveshaper_test.cpp
    unit/primitives/hard_clip_adaa_test.cpp
    unit/primitives/tanh_adaa_test.cpp
    unit/primitives/wavefolder_test.cpp
    unit/primitives/chebyshev_shaper_test.cpp
    unit/primitives/pitch_detector_test.cpp
    unit/primitives/test_rolling_capture_buffer.cpp
    unit/primitives/test_slice_pool.cpp
    unit/primitives/one_pole_test.cpp
    unit/primitives/svf_test.cpp
    unit/primitives/sample_rate_converter_test.cpp
    unit/primitives/allpass_1pole_test.cpp
    unit/primitives/comb_filter_test.cpp
    unit/primitives/ladder_filter_test.cpp
    unit/primitives/two_pole_lp_test.cpp
    unit/primitives/hilbert_transform_test.cpp
    unit/primitives/sequencer_core_tests.cpp
    unit/primitives/chaos_waveshaper_test.cpp
    unit/primitives/hard_clip_polyblamp_test.cpp
    unit/primitives/stochastic_shaper_test.cpp
    unit/primitives/ring_saturation_test.cpp
    unit/primitives/bitwise_mangler_test.cpp
    unit/primitives/sweep_position_buffer_test.cpp
    unit/primitives/spectrum_fifo_test.cpp
    unit/primitives/polyblep_oscillator_test.cpp
    unit/primitives/wavetable_generator_test.cpp
    unit/primitives/wavetable_oscillator_test.cpp
    unit/primitives/minblep_table_test.cpp
    unit/primitives/pink_noise_filter_test.cpp
    unit/primitives/noise_oscillator_test.cpp

    # Layer 2: Processors
    unit/processors/multimode_filter_test.cpp
    unit/processors/saturation_processor_test.cpp
    unit/processors/envelope_follower_test.cpp
    unit/processors/dynamics_processor_test.cpp
    unit/processors/ducking_processor_test.cpp
    unit/processors/noise_generator_test.cpp
    unit/processors/midside_processor_test.cpp
    unit/processors/diffusion_network_test.cpp
    unit/processors/pitch_shift_processor_test.cpp
    unit/processors/reverse_feedback_processor_test.cpp
    unit/processors/grain_scheduler_test.cpp
    unit/processors/grain_processor_test.cpp
    unit/processors/tube_stage_test.cpp
    unit/processors/diode_clipper_test.cpp
    unit/processors/wavefolder_processor_test.cpp
    unit/processors/tape_saturator_test.cpp
    unit/processors/fuzz_processor_test.cpp
    unit/processors/bitcrusher_processor_test.cpp
    unit/processors/test_pattern_scheduler.cpp
    unit/processors/crossover_filter_test.cpp
    unit/processors/formant_filter_test.cpp
    unit/processors/envelope_filter_test.cpp
    unit/processors/phaser_test.cpp
    unit/processors/spectral_morph_filter_test.cpp
    unit/processors/spectral_gate_test.cpp
    unit/processors/spectral_tilt_test.cpp
    unit/processors/resonator_bank_test.cpp
    unit/processors/karplus_strong_test.cpp
    unit/processors/waveguide_resonator_test.cpp
    unit/processors/modal_resonator_test.cpp
    unit/processors/stochastic_filter_test.cpp
    unit/processors/self_oscillating_filter_tests.cpp
    unit/processors/sample_hold_filter_test.cpp
    unit/processors/sidechain_filter_test.cpp
    unit/processors/transient_filter_test.cpp
    unit/processors/pitch_tracking_filter_test.cpp
    unit/processors/note_selective_filter_test.cpp
    unit/processors/audio_rate_filter_fm_test.cpp
    unit/processors/frequency_shifter_test.cpp
    unit/processors/multistage_env_filter_tests.cpp
    unit/processors/spectral_distortion_test.cpp
    unit/processors/formant_distortion_test.cpp
    unit/processors/temporal_distortion_test.cpp
    unit/processors/allpass_saturator_tests.cpp
    unit/processors/feedback_distortion_test.cpp
    unit/processors/aliasing_effect_test.cpp
    unit/processors/granular_distortion_test.cpp
    unit/processors/fractal_distortion_test.cpp
    unit/processors/random_source_test.cpp
    unit/processors/chaos_mod_source_test.cpp
    unit/processors/transient_detector_test.cpp
    unit/processors/sample_hold_source_test.cpp
    unit/processors/pitch_follower_source_test.cpp
    unit/processors/sync_oscillator_test.cpp
    unit/processors/sub_oscillator_test.cpp
    unit/processors/fm_operator_test.cpp
    unit/processors/phase_distortion_oscillator_test.cpp

    # Layer 3: Systems
    unit/systems/delay_engine_test.cpp
    unit/systems/feedback_network_test.cpp
    unit/systems/feedback_network_dc_blocking_test.cpp
    unit/systems/modulation_matrix_test.cpp
    unit/systems/character_processor_test.cpp
    unit/systems/character_processor_digital_ramping_test.cpp
    unit/systems/character_processor_bbd_stereo_test.cpp
    unit/systems/stereo_field_test.cpp
    unit/systems/tap_manager_test.cpp
    unit/systems/flexible_feedback_network_test.cpp
    unit/systems/granular_engine_test.cpp
    unit/systems/amp_channel_test.cpp
    unit/systems/tape_machine_test.cpp
    unit/systems/fuzz_pedal_test.cpp
    unit/systems/distortion_rack_tests.cpp
    unit/systems/filter_feedback_matrix_tests.cpp
    unit/systems/filter_step_sequencer_tests.cpp
    unit/systems/vowel_sequencer_tests.cpp
    unit/systems/granular_filter_test.cpp
    unit/systems/timevar_comb_bank_tests.cpp
    unit/systems/modulation_engine_test.cpp
    unit/systems/modulation_engine_perf_test.cpp
    unit/systems/unison_engine_test.cpp
    unit/systems/fm_voice_test.cpp

    # Layer 4: Effects
    unit/effects/tape_delay_test.cpp
    unit/effects/tape_delay_params_diagnostic_test.cpp
    unit/effects/bbd_delay_test.cpp
    unit/effects/digital_delay_test.cpp
    unit/effects/digital_width_processing_test.cpp
    unit/effects/digital_delay_age0_ramping_test.cpp
    unit/effects/ping_pong_delay_test.cpp
    unit/effects/multi_tap_delay_test.cpp
    unit/effects/multi_tap_morph_test.cpp
    unit/effects/shimmer_delay_test.cpp
    unit/effects/reverse_delay_test.cpp
    unit/effects/freeze_mode_test.cpp
    unit/effects/ducking_delay_test.cpp
    unit/effects/spectral_delay_test.cpp
    unit/effects/granular_delay_test.cpp
    unit/effects/granular_delay_tempo_sync_test.cpp
    unit/effects/granular_delay_dry_signal_test.cpp
    unit/effects/test_pattern_freeze_mode.cpp

    # Test Helpers tests
    ${CMAKE_SOURCE_DIR}/tests/test_helpers/spectral_analysis_test.cpp
    unit/test_helpers/statistical_utils_tests.cpp
    unit/test_helpers/artifact_detection_tests.cpp
    unit/test_helpers/signal_metrics_tests.cpp
    unit/test_helpers/parameter_sweep_tests.cpp
    unit/test_helpers/lpc_detection_tests.cpp
    unit/test_helpers/spectral_anomaly_tests.cpp
    unit/test_helpers/golden_reference_tests.cpp
    unit/test_helpers/artifact_detection_integration_tests.cpp
)

target_link_libraries(dsp_tests
    PRIVATE
        KrateDSP
        Catch2::Catch2
        test_helpers
)

target_compile_features(dsp_tests PRIVATE cxx_std_20)

# ==============================================================================
# Disable -ffast-math for files that require IEEE 754 compliant NaN handling
# ==============================================================================
# The VST3 SDK enables -ffast-math globally (see SMTG_PlatformToolset.cmake)
# which causes isnan() and __builtin_isnan() to be optimized out.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set_source_files_properties(
        unit/test_dsp_utils.cpp
        unit/core/db_utils_test.cpp
        unit/core/math_constants_test.cpp
        unit/core/window_functions_test.cpp
        unit/core/note_value_test.cpp
        unit/core/note_value_mapping_test.cpp
        unit/core/block_context_test.cpp
        unit/core/fast_math_test.cpp
        unit/core/interpolation_test.cpp
        unit/core/pitch_utils_test.cpp
        unit/core/grain_envelope_test.cpp
        unit/core/crossfade_utils_test.cpp
        unit/core/sigmoid_test.cpp
        unit/core/chebyshev_test.cpp
        unit/core/test_wavefold_math.cpp
        unit/core/pattern_freeze_types_test.cpp
        unit/core/test_euclidean_pattern.cpp
        unit/core/filter_design_test.cpp
        unit/core/filter_tables_test.cpp
        unit/core/midi_utils_tests.cpp
        unit/core/modulation_types_test.cpp
        unit/core/modulation_curves_test.cpp
        unit/core/polyblep_test.cpp
        unit/core/phase_utils_test.cpp
        unit/core/wavetable_data_test.cpp
        unit/primitives/smoother_test.cpp
        unit/primitives/dc_blocker_test.cpp
        unit/primitives/oversampler_test.cpp
        unit/primitives/delay_line_test.cpp
        unit/primitives/lfo_test.cpp
        unit/primitives/biquad_test.cpp
        unit/primitives/fft_test.cpp
        unit/primitives/spectral_buffer_test.cpp
        unit/primitives/stft_test.cpp
        unit/primitives/bit_crusher_test.cpp
        unit/primitives/bit_crusher_symmetric_quantization_test.cpp
        unit/primitives/bit_crusher_rng_bias_test.cpp
        unit/primitives/sample_rate_reducer_test.cpp
        unit/primitives/reverse_buffer_test.cpp
        unit/primitives/grain_pool_test.cpp
        unit/primitives/waveshaper_test.cpp
        unit/primitives/hard_clip_adaa_test.cpp
        unit/primitives/tanh_adaa_test.cpp
        unit/primitives/wavefolder_test.cpp
        unit/primitives/chebyshev_shaper_test.cpp
        unit/primitives/pitch_detector_test.cpp
        unit/primitives/test_rolling_capture_buffer.cpp
        unit/primitives/test_slice_pool.cpp
        unit/primitives/one_pole_test.cpp
        unit/primitives/svf_test.cpp
        unit/primitives/sample_rate_converter_test.cpp
        unit/primitives/allpass_1pole_test.cpp
        unit/primitives/comb_filter_test.cpp
        unit/primitives/ladder_filter_test.cpp
        unit/primitives/two_pole_lp_test.cpp
        unit/primitives/hilbert_transform_test.cpp
        unit/primitives/sequencer_core_tests.cpp
        unit/primitives/chaos_waveshaper_test.cpp
        unit/primitives/hard_clip_polyblamp_test.cpp
        unit/primitives/stochastic_shaper_test.cpp
        unit/primitives/ring_saturation_test.cpp
        unit/primitives/bitwise_mangler_test.cpp
        unit/primitives/sweep_position_buffer_test.cpp
        unit/primitives/spectrum_fifo_test.cpp
        unit/primitives/polyblep_oscillator_test.cpp
        unit/primitives/wavetable_generator_test.cpp
        unit/primitives/wavetable_oscillator_test.cpp
        unit/primitives/minblep_table_test.cpp
        unit/primitives/pink_noise_filter_test.cpp
        unit/primitives/noise_oscillator_test.cpp
        unit/processors/multimode_filter_test.cpp
        unit/processors/saturation_processor_test.cpp
        unit/processors/envelope_follower_test.cpp
        unit/processors/dynamics_processor_test.cpp
        unit/processors/ducking_processor_test.cpp
        unit/processors/noise_generator_test.cpp
        unit/processors/midside_processor_test.cpp
        unit/processors/diffusion_network_test.cpp
        unit/processors/pitch_shift_processor_test.cpp
        unit/processors/reverse_feedback_processor_test.cpp
        unit/processors/grain_scheduler_test.cpp
        unit/processors/grain_processor_test.cpp
        unit/processors/tube_stage_test.cpp
        unit/processors/diode_clipper_test.cpp
        unit/processors/wavefolder_processor_test.cpp
        unit/processors/tape_saturator_test.cpp
        unit/processors/fuzz_processor_test.cpp
        unit/processors/bitcrusher_processor_test.cpp
        unit/processors/test_pattern_scheduler.cpp
        unit/processors/crossover_filter_test.cpp
        unit/processors/formant_filter_test.cpp
        unit/processors/envelope_filter_test.cpp
        unit/processors/phaser_test.cpp
        unit/processors/spectral_morph_filter_test.cpp
        unit/processors/spectral_gate_test.cpp
        unit/systems/delay_engine_test.cpp
        unit/systems/feedback_network_test.cpp
        unit/systems/modulation_matrix_test.cpp
        unit/systems/character_processor_test.cpp
        unit/systems/stereo_field_test.cpp
        unit/systems/tap_manager_test.cpp
        unit/systems/flexible_feedback_network_test.cpp
        unit/systems/granular_engine_test.cpp
        unit/systems/feedback_network_dc_blocking_test.cpp
        unit/systems/character_processor_digital_ramping_test.cpp
        unit/systems/character_processor_bbd_stereo_test.cpp
        unit/systems/amp_channel_test.cpp
        unit/systems/tape_machine_test.cpp
        unit/systems/fuzz_pedal_test.cpp
        unit/systems/distortion_rack_tests.cpp
        unit/systems/filter_feedback_matrix_tests.cpp
        unit/systems/filter_step_sequencer_tests.cpp
        unit/systems/vowel_sequencer_tests.cpp
        unit/systems/granular_filter_test.cpp
        unit/systems/timevar_comb_bank_tests.cpp
        unit/systems/modulation_engine_test.cpp
        unit/systems/modulation_engine_perf_test.cpp
        unit/systems/unison_engine_test.cpp
        unit/systems/fm_voice_test.cpp
        unit/effects/tape_delay_test.cpp
        unit/effects/tape_delay_params_diagnostic_test.cpp
        unit/effects/bbd_delay_test.cpp
        unit/effects/digital_delay_test.cpp
        unit/effects/digital_width_processing_test.cpp
        unit/effects/digital_delay_age0_ramping_test.cpp
        unit/effects/ping_pong_delay_test.cpp
        unit/effects/multi_tap_delay_test.cpp
        unit/effects/multi_tap_morph_test.cpp
        unit/effects/shimmer_delay_test.cpp
        unit/effects/reverse_delay_test.cpp
        unit/effects/freeze_mode_test.cpp
        unit/effects/ducking_delay_test.cpp
        unit/effects/spectral_delay_test.cpp
        unit/effects/granular_delay_test.cpp
        unit/effects/granular_delay_tempo_sync_test.cpp
        unit/effects/granular_delay_dry_signal_test.cpp
        unit/effects/test_pattern_freeze_mode.cpp
        ${CMAKE_SOURCE_DIR}/tests/test_helpers/spectral_analysis_test.cpp
        unit/test_helpers/statistical_utils_tests.cpp
        unit/test_helpers/artifact_detection_tests.cpp
        unit/test_helpers/signal_metrics_tests.cpp
        unit/test_helpers/parameter_sweep_tests.cpp
        unit/test_helpers/lpc_detection_tests.cpp
        unit/test_helpers/spectral_anomaly_tests.cpp
        unit/test_helpers/golden_reference_tests.cpp
        unit/test_helpers/artifact_detection_integration_tests.cpp
        unit/processors/spectral_gate_test.cpp
        unit/processors/spectral_tilt_test.cpp
        unit/processors/resonator_bank_test.cpp
        unit/processors/karplus_strong_test.cpp
        unit/processors/waveguide_resonator_test.cpp
        unit/processors/modal_resonator_test.cpp
        unit/processors/stochastic_filter_test.cpp
        unit/processors/self_oscillating_filter_tests.cpp
        unit/processors/sample_hold_filter_test.cpp
        unit/processors/sidechain_filter_test.cpp
        unit/processors/transient_filter_test.cpp
        unit/processors/pitch_tracking_filter_test.cpp
        unit/processors/note_selective_filter_test.cpp
        unit/processors/audio_rate_filter_fm_test.cpp
        unit/processors/frequency_shifter_test.cpp
        unit/processors/multistage_env_filter_tests.cpp
        unit/processors/spectral_distortion_test.cpp
        unit/processors/formant_distortion_test.cpp
        unit/processors/temporal_distortion_test.cpp
        unit/processors/allpass_saturator_tests.cpp
        unit/processors/feedback_distortion_test.cpp
        unit/processors/aliasing_effect_test.cpp
        unit/processors/granular_distortion_test.cpp
        unit/processors/fractal_distortion_test.cpp
        unit/processors/random_source_test.cpp
        unit/processors/chaos_mod_source_test.cpp
        unit/processors/transient_detector_test.cpp
        unit/processors/sample_hold_source_test.cpp
        unit/processors/pitch_follower_source_test.cpp
        unit/processors/sync_oscillator_test.cpp
        unit/processors/sub_oscillator_test.cpp
        unit/processors/fm_operator_test.cpp
        unit/processors/phase_distortion_oscillator_test.cpp
        PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
    )
endif()

# ==============================================================================
# CTest Integration
# ==============================================================================
catch_discover_tests(dsp_tests REPORTER console)
