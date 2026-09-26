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

# Split into one executable per DSP layer so a leaf edit relinks only its
# layer's tests, not the whole ~250-TU monolith. The source list below is the
# same set that used to build dsp_tests, partitioned by layer (union unchanged).
# NOTE: sources are listed explicitly (no GLOB) — a new test file MUST be added
#       to the right target here or it silently drops from the build.

# Layer 0 (core) + root dsp_utils + test-helper self-tests
add_executable(dsp_core_tests
    # Shared Catch2 main + FTZ/DAZ setup (see dsp_test_main.cpp)
    dsp_test_main.cpp
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
    unit/core/scoped_denormal_mode_test.cpp
    unit/core/fast_math_test.cpp
    unit/core/interpolation_test.cpp
    unit/core/stereo_utils_test.cpp
    unit/core/pitch_utils_test.cpp
    unit/core/scale_harmonizer_test.cpp
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
    unit/core/spectral_simd_test.cpp
    unit/core/curve_table_test.cpp
    unit/core/transport_sync_test.cpp
    unit/core/chord_generator_test.cpp
    unit/core/hungarian_algorithm_test.cpp
    unit/core/xorshift32_test.cpp

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
    unit/test_helpers/uidesc_reachability_tests.cpp
)

target_link_libraries(dsp_core_tests
    PRIVATE
        KrateDSP
        Catch2::Catch2
        test_helpers
)

target_compile_features(dsp_core_tests PRIVATE cxx_std_20)

# Layer 1 (primitives)
add_executable(dsp_primitives_tests
    # Shared Catch2 main + FTZ/DAZ setup (see dsp_test_main.cpp)
    dsp_test_main.cpp
    # Layer 1: Primitives
    unit/primitives/envelope_utils_test.cpp
    unit/primitives/adsr_envelope_test.cpp
    unit/primitives/delay_line_test.cpp
    unit/primitives/crossfading_delay_line_test.cpp
    unit/primitives/lfo_test.cpp
    unit/primitives/biquad_test.cpp
    unit/primitives/smoother_test.cpp
    unit/primitives/dc_blocker_test.cpp
    unit/primitives/oversampler_test.cpp
    unit/primitives/fft_test.cpp
    unit/primitives/spectral_buffer_test.cpp
    unit/primitives/spectral_utils_tests.cpp
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
    unit/primitives/pitch_tracker_test.cpp
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
    unit/primitives/spectral_transient_detector_test.cpp
    unit/primitives/held_note_buffer_test.cpp
    unit/primitives/arp_lane_test.cpp
)

target_link_libraries(dsp_primitives_tests
    PRIVATE
        KrateDSP
        Catch2::Catch2
        test_helpers
)

target_compile_features(dsp_primitives_tests PRIVATE cxx_std_20)

# Layer 2 (processors)
add_executable(dsp_processors_tests
    # Shared Catch2 main + FTZ/DAZ setup (see dsp_test_main.cpp)
    dsp_test_main.cpp
    # Layer 2: Processors
    unit/processors/markov_matrices_test.cpp
    unit/processors/multimode_filter_test.cpp
    unit/processors/saturation_processor_test.cpp
    unit/processors/envelope_follower_test.cpp
    unit/processors/dynamics_processor_test.cpp
    unit/processors/true_peak_limiter_test.cpp
    unit/processors/ducking_processor_test.cpp
    unit/processors/noise_generator_test.cpp
    unit/processors/midside_processor_test.cpp
    unit/processors/diffusion_network_test.cpp
    unit/processors/pitch_shift_processor_test.cpp
    unit/processors/phase_locking_test.cpp
    unit/processors/phase_reset_test.cpp
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
    unit/processors/chorus_test.cpp
    unit/processors/flanger_test.cpp
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
    unit/processors/additive_oscillator_test.cpp
    unit/processors/chaos_oscillator_test.cpp
    unit/processors/formant_oscillator_test.cpp
    unit/processors/particle_oscillator_test.cpp
    unit/processors/rungler_test.cpp
    unit/processors/spectral_freeze_oscillator_test.cpp
    unit/processors/multi_stage_envelope_test.cpp
    unit/processors/mono_handler_test.cpp
    unit/processors/note_processor_test.cpp
    unit/processors/trance_gate_test.cpp
    # arpeggiator_core_test.cpp split into spec-area siblings (D1) sharing
    # arpeggiator_core_test_helpers.h
    unit/processors/arpeggiator_core_core_test.cpp
    unit/processors/arpeggiator_core_lanes_test.cpp
    unit/processors/arpeggiator_core_baseline_test.cpp
    unit/processors/arpeggiator_core_perstepmods_test.cpp
    unit/processors/arpeggiator_core_ratcheting_test.cpp
    unit/processors/arpeggiator_core_euclidean_test.cpp
    unit/processors/arpeggiator_core_conditional_test.cpp
    unit/processors/arpeggiator_core_spicehuman_test.cpp
    unit/processors/arpeggiator_core_dawloop_test.cpp
    unit/processors/arpeggiator_core_scalechord_test.cpp
    unit/processors/arpeggiator_core_curve_race_test.cpp
    unit/processors/midi_note_delay_test.cpp
    unit/processors/ring_modulator_test.cpp
    unit/processors/harmonic_types_tests.cpp
    unit/processors/yin_pitch_detector_tests.cpp
    unit/processors/dual_stft_tests.cpp
    unit/processors/partial_tracker_tests.cpp
    unit/processors/harmonic_oscillator_bank_tests.cpp
    unit/processors/residual_types_tests.cpp
    unit/processors/residual_analyzer_tests.cpp
    unit/processors/residual_synthesizer_tests.cpp
    unit/processors/spectral_coring_estimator_tests.cpp
    unit/processors/subharmonic_validator_tests.cpp
    unit/processors/harmonic_frame_utils_tests.cpp
    unit/processors/harmonic_snapshot_tests.cpp
    unit/processors/test_harmonic_oscillator_bank_stereo.cpp
    unit/processors/multi_pitch_detector_tests.cpp
    unit/processors/multi_source_sieve_tests.cpp
    unit/processors/partial_tracker_polyphonic_tests.cpp
    unit/processors/test_modal_resonator_bank.cpp
    unit/processors/test_modal_bank_stretch.cpp
    unit/processors/impact_exciter_test.cpp
    unit/processors/waveguide_string_test.cpp
    unit/processors/bow_exciter_test.cpp
    unit/processors/bow_exciter_oversampling_test.cpp
    unit/processors/modal_resonator_bank_bow_test.cpp
    unit/processors/waveguide_string_dc_blocker_test.cpp
    unit/processors/bow_waveguide_coupling_test.cpp
    unit/processors/body_resonance_tests.cpp
    unit/processors/test_modal_bank_frequency.cpp
    unit/processors/brownian_drift_test.cpp
    unit/processors/breathing_modulator_test.cpp
    unit/processors/tidal_modulator_test.cpp
    unit/processors/orbit_modulator_test.cpp
    unit/processors/spline_trajectory_test.cpp
    unit/processors/growth_envelope_test.cpp
    unit/processors/life_modulators_perf_test.cpp
    unit/processors/spectral_state_test.cpp
    unit/processors/entropy_processor_test.cpp
    # Seraphis Phase 4 (specs/seraphis-phase4-continuous-body): shared Layer 2 amendments
    unit/processors/waveguide_string_retune_test.cpp
    unit/processors/diffusion_network_zeromod_test.cpp
    # Vorago Phase 1 (specs/vorago-phase1-events-modulation)
    unit/processors/perlin_noise_source_test.cpp
    unit/processors/chaos_mod_source_aizawa_test.cpp
    unit/processors/slow_event_scheduler_test.cpp
    unit/processors/vorago_p1_perf_test.cpp
    unit/processors/vorago_p1_longrun_test.cpp
    unit/processors/vorago_p1_harness.cpp
    # Vorago Phase 4 (specs/vorago-phase4-spectral-smear): SpectralSmear.
    # This list is ENUMERATED, not globbed - an unregistered TU silently drops
    # out of the build and its cases never run.
    #   spectral_smear_test.cpp           SC-002, SC-003, SC-007, SC-008, SC-009,
    #                                     SC-011, SC-012 (c), SC-017, SC-018 (a)
    #                                     + geometry / clamps / boundaries /
    #                                       pole-bounds / output-clamp cases
    #   spectral_smear_spectral_test.cpp  SC-001, SC-004, SC-005, SC-006, SC-010,
    #                                     SC-012 (a)(b), SpectralSmear_TimeConstantLaw,
    #                                     SpectralSmear_DcNyquistSmear, the flux
    #                                     helper sanity case  -- the [long] set
    #   spectral_smear_perf_test.cpp      SC-013 (a)-(d), [.perf] only
    #   spectral_smear_nonfinite_test.cpp SC-016, SC-018 (b), FR-009's non-finite
    #                                     setter arm -- the ONE -fno-fast-math TU
    unit/processors/spectral_smear_test.cpp
    unit/processors/spectral_smear_spectral_test.cpp
    unit/processors/spectral_smear_perf_test.cpp
    unit/processors/spectral_smear_nonfinite_test.cpp
)

target_link_libraries(dsp_processors_tests
    PRIVATE
        KrateDSP
        Catch2::Catch2
        test_helpers
)

target_compile_features(dsp_processors_tests PRIVATE cxx_std_20)

# Layer 3 (systems)
add_executable(dsp_systems_tests
    # Shared Catch2 main + FTZ/DAZ setup (see dsp_test_main.cpp)
    dsp_test_main.cpp
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
    unit/systems/vector_mixer_tests.cpp
    unit/systems/voice_allocator_test.cpp
    unit/systems/synth_voice_test.cpp
    unit/systems/poly_synth_engine_test.cpp
    unit/systems/selectable_oscillator_test.cpp
    unit/systems/voice_mod_router_test.cpp
    unit/systems/ext_modulation_test.cpp
    unit/systems/harmonizer_engine_test.cpp
    unit/systems/harmonic_model_builder_tests.cpp
    unit/systems/sympathetic_resonance_test.cpp
    unit/systems/harmonic_cloud_test.cpp
    unit/systems/harmonic_cloud_spectral_test.cpp
    unit/systems/harmonic_cloud_perf_test.cpp
    unit/systems/spectral_morph_engine_test.cpp
    unit/systems/spectral_morph_render_test.cpp
    unit/systems/spectral_morph_perf_test.cpp
    # Seraphis Phase 4 (specs/seraphis-phase4-continuous-body): ContinuousBody
    unit/systems/continuous_body_test.cpp
    unit/systems/continuous_body_spectral_test.cpp
    unit/systems/continuous_body_perf_test.cpp
    # Seraphis Phase 5 (specs/seraphis-phase5-atmosphere): AtmosphereEngine
    unit/systems/atmosphere_engine_test.cpp
    unit/systems/atmosphere_engine_spectral_test.cpp
    unit/systems/atmosphere_engine_perf_test.cpp
    unit/systems/atmosphere_engine_nonfinite_test.cpp
    # Seraphis Phase 7 (specs/seraphis-phase7-voice-engine): SeraphisVoice,
    # SeraphisEngine, SeraphisMacroMatrix. This list is ENUMERATED, not globbed -
    # an unregistered TU silently drops out of the build and its cases never run.
    unit/systems/seraphis_voice_test.cpp
    unit/systems/seraphis_engine_test.cpp
    unit/systems/seraphis_macro_test.cpp
    unit/systems/seraphis_nonfinite_test.cpp
    unit/systems/seraphis_perf_test.cpp
    # Seraphis Phase 9 (specs/seraphis-phase9-parameters): the broadcast POD,
    # applyVoiceParams / applySpectralStates, the macro base overrides, the new
    # SeraphisVoice forwarders and ContinuousBody accessors.
    unit/systems/seraphis_param_broadcast_test.cpp
    # Seraphis Phase 11 (specs/seraphis-phase11-ui): the SeraphisEngine
    # per-partial fan-outs and their SeraphisVoice pass-throughs (T003).
    unit/systems/seraphis_partial_fanout_test.cpp
    # Seraphis Phase 11 (specs/seraphis-phase11-ui): SC-012's acceptance arm and
    # SC-013's four audibility arms for the SpectralState authoring mutators
    # (T005). Layer 3 because every arm needs SpectralMorphEngine/HarmonicCloud;
    # the Layer 2 preservation table stays in
    # unit/processors/spectral_state_test.cpp. Deliberately NOT in the
    # -fno-fast-math block below: its non-finite arguments are built from bit
    # patterns and the mutators' guards are detail::isNaN/isInf, so the file must
    # build in the FP mode the header actually ships in.
    #
    # T027 audit: these are the ONLY two dsp/ TUs Phase 11 adds, both here in
    # dsp_systems_tests, each exactly once, and neither in the -fno-fast-math
    # block below. unit/processors/spectral_state_test.cpp (T002's Layer 2
    # preservation table) was already registered to dsp_processors_tests and stays
    # there. SC-030 needs NO new TU - it is a re-run of the existing
    # spectral_morph_* cases in this same executable.
    unit/systems/spectral_state_authoring_test.cpp
    # Vorago Phase 2 (specs/vorago-phase2-noise-organism): NoiseOrganism.
    # This list is ENUMERATED, not globbed - an unregistered TU silently drops
    # out of the build and its cases never run.
    #   noise_organism_test.cpp          SC-003, SC-005(a), SC-006, SC-007, SC-010,
    #                                    SC-013, SC-014, SC-016, SC-017, SC-018, SC-021
    #   noise_organism_spectral_test.cpp SC-001, SC-002, SC-005(b), SC-008, SC-009,
    #                                    SC-019, SC-020   (the [long] set)
    #   noise_organism_perf_test.cpp     SC-004(a)-(e) + the T002 stage probe   [.perf]
    #   noise_organism_nonfinite_test.cpp SC-015 only
    unit/systems/noise_organism_test.cpp
    unit/systems/noise_organism_spectral_test.cpp
    unit/systems/noise_organism_perf_test.cpp
    unit/systems/noise_organism_nonfinite_test.cpp
    # Vorago Phase 3 (specs/vorago-phase3-resonance-drift): ResonanceDriftNetwork.
    # This list is ENUMERATED, not globbed - an unregistered TU silently drops
    # out of the build and its cases never run.
    #   resonance_drift_network_test.cpp          SC-005, SC-006, SC-007, SC-008,
    #                                             SC-010, SC-011, SC-014, SC-017,
    #                                             SC-018, SC-019, SC-020, SC-021,
    #                                             SC-022 + the clamps/clamp-engages cases
    #   resonance_drift_network_spectral_test.cpp SC-001, SC-002, SC-003 [long],
    #                                             SC-015 [long], SC-016 [long]
    #   resonance_drift_network_perf_test.cpp     SC-004 + the FR-060 stage probe [.perf]
    #   resonance_drift_network_nonfinite_test.cpp SC-009 only
    unit/systems/resonance_drift_network_test.cpp
    unit/systems/resonance_drift_network_spectral_test.cpp
    unit/systems/resonance_drift_network_perf_test.cpp
    unit/systems/resonance_drift_network_nonfinite_test.cpp
    # Vorago Phase 5 (specs/vorago-phase5-feedback-ecology): FeedbackEcology.
    # This list is ENUMERATED, not globbed - an unregistered TU silently drops
    # out of the build and its cases never run.
    #   feedback_ecology_test.cpp           SC-005, SC-006, SC-007, SC-008, SC-009,
    #                                       SC-010, SC-011, SC-013, SC-014, SC-015,
    #                                       SC-016, SC-017, SC-018, SC-022, SC-023,
    #                                       SC-024, SC-025
    #   feedback_ecology_spectral_test.cpp  SC-001, SC-002, SC-003, SC-021   (the [long] set)
    #   feedback_ecology_perf_test.cpp      SC-004 (a)-(f) + the FR-080 stage probe   [.perf]
    #   feedback_ecology_nonfinite_test.cpp SC-012 only
    unit/systems/feedback_ecology_test.cpp
    unit/systems/feedback_ecology_spectral_test.cpp
    unit/systems/feedback_ecology_perf_test.cpp
    unit/systems/feedback_ecology_nonfinite_test.cpp
    # Vorago Phase 6 (specs/vorago-phase6-subharmonic): SubharmonicEngine.
    # This list is ENUMERATED, not globbed - an unregistered TU silently drops
    # out of the build and its cases never run.
    #   subharmonic_engine_test.cpp           SC-001, SC-006, SC-007, SC-008, SC-010,
    #                                         SC-011, SC-012, SC-014, SC-019, SC-020,
    #                                         SC-021, SC-022
    #   subharmonic_engine_spectral_test.cpp  SC-002, SC-003, SC-004, SC-005,
    #                                         SC-017 (A)+(B)   (the [long] set)
    #   subharmonic_engine_perf_test.cpp      SC-013 + the FR-071 stage/placement probe [.perf]
    #   subharmonic_engine_nonfinite_test.cpp SC-009 only
    unit/systems/subharmonic_engine_test.cpp
    unit/systems/subharmonic_engine_spectral_test.cpp
    unit/systems/subharmonic_engine_perf_test.cpp
    unit/systems/subharmonic_engine_nonfinite_test.cpp
    # Vorago Phase 7 (specs/vorago-phase7-harmonic-bloom): BloomEngine.
    # This list is ENUMERATED, not globbed - an unregistered TU silently drops
    # out of the build and its cases never run.
    #   bloom_engine_test.cpp           SC-003, SC-005, SC-006, SC-007, SC-008,
    #                                   SC-012, SC-014, SC-016, SC-017, SC-018
    #   bloom_engine_spectral_test.cpp  SC-001, SC-002, SC-004, SC-010, SC-015
    #                                   (the [long] set)
    #   bloom_engine_perf_test.cpp      SC-011 + the FR-073 stage probe   [.perf]
    #   bloom_engine_nonfinite_test.cpp SC-009 only
    unit/systems/bloom_engine_test.cpp
    unit/systems/bloom_engine_spectral_test.cpp
    unit/systems/bloom_engine_perf_test.cpp
    unit/systems/bloom_engine_nonfinite_test.cpp
    # Vorago Phase 8 (specs/vorago-phase8-ecosystem): EcosystemEngine.
    # This list is ENUMERATED, not globbed - an unregistered TU silently drops
    # out of the build and its cases never run.
    #   ecosystem_engine_test.cpp           SC-002, SC-004, SC-006, SC-007, SC-008,
    #                                       SC-009 (d), SC-010 (b), SC-014, SC-015, SC-019,
    #                                       SC-020, SC-021, plus the FR-011/FR-021/FR-064
    #                                       and perturb-edge cases (S14 D-O, D-P)
    #   ecosystem_engine_longrun_test.cpp   SC-001 (a)-(d), SC-003, SC-005, SC-010 (a)(c),
    #                                       SC-012, SC-013, SC-017, SC-018  (the [long] set)
    #   ecosystem_engine_perf_test.cpp      SC-011 + the stage probe   [.perf]
    #   ecosystem_engine_nonfinite_test.cpp SC-009 only
    unit/systems/ecosystem_engine_test.cpp
    unit/systems/ecosystem_engine_longrun_test.cpp
    unit/systems/ecosystem_engine_perf_test.cpp
    unit/systems/ecosystem_engine_nonfinite_test.cpp
    # Vorago Phase 10 (specs/vorago-phase10-voice-engine): VoragoVoice, VoragoEngine,
    # VoragoMacroMatrix + the six dark ContinuousBody materials.
    # This list is ENUMERATED, not globbed - an unregistered TU silently drops out of
    # the build and its cases never run.
    #   vorago_voice_test.cpp           SC-007(voice), SC-017, SC-017a, SC-018a, SC-019a,
    #                                   SC-019b, SC-020a, SC-023(voice), SC-028(voice)
    #   vorago_voice_longrun_test.cpp   SC-018, SC-019, SC-020            (the [long] set)
    #   vorago_engine_test.cpp          SC-004a, SC-006, SC-007(engine), SC-011, SC-012,
    #                                   SC-013a, SC-014, SC-021a, SC-022, SC-023, SC-026,
    #                                   SC-027, SC-028, SC-030, SC-031
    #   vorago_engine_longrun_test.cpp  SC-004b, SC-005, SC-013b, SC-021b (the [long] set)
    #   vorago_macro_test.cpp           SC-008 (eleven macros), SC-009, SC-010, SC-023(matrix),
    #                                   SC-028, FR-067's VoragoMacro_ApplyIsIdempotent
    #   vorago_dark_materials_test.cpp  SC-015, SC-016 (3)(4)
    #   vorago_nonfinite_test.cpp       SC-029 only, + the probe definition
    #   vorago_perf_test.cpp            SC-001a, SC-001b, SC-002 (+ the advanceLifeOnly/L arm),
    #                                   SC-003, VoragoVoice_ClearingPathCost      [.perf]
    unit/systems/vorago_voice_test.cpp
    unit/systems/vorago_voice_longrun_test.cpp
    unit/systems/vorago_engine_test.cpp
    unit/systems/vorago_engine_longrun_test.cpp
    unit/systems/vorago_macro_test.cpp
    unit/systems/vorago_dark_materials_test.cpp
    unit/systems/vorago_nonfinite_test.cpp
    unit/systems/vorago_perf_test.cpp
    # Vorago Phase 10a (specs/vorago-phase10a-ghost-extension): the AtmosphereEngine
    # ghost extension (reverse grains + triggerGrain). ENUMERATED, not globbed -
    # an unregistered TU silently drops out of the build and its cases never run.
    #   atmosphere_ghost_test.cpp          SC-001, SC-002, SC-004 (short arm, never
    #                                      [long]), SC-005, SC-006, SC-007 (a)-(c),
    #                                      SC-008 (a)(c)(d)(e), SC-011, SC-012,
    #                                      FR-049's short twin
    #   atmosphere_ghost_longrun_test.cpp  SC-003, SC-007 (d), SC-004's sweep
    #                                      sibling            (the [long] set)
    #   atmosphere_ghost_nonfinite_test.cpp SC-008 (b) only - the ONE TU that
    #                                      injects NaN/Inf bit patterns, and the
    #                                      only Phase 10a entry in the
    #                                      -fno-fast-math block below
    #   atmosphere_ghost_perf_test.cpp     SC-009 only        [.perf]
    #   vorago_ghost_ext_test.cpp          SC-010 only (the vorago_engine.h side)
    unit/systems/atmosphere_ghost_test.cpp
    unit/systems/atmosphere_ghost_longrun_test.cpp
    unit/systems/atmosphere_ghost_nonfinite_test.cpp
    unit/systems/atmosphere_ghost_perf_test.cpp
    unit/systems/vorago_ghost_ext_test.cpp
    # Vorago Phase 12 (specs/vorago-phase12-parameters): the DSP side of the full
    # parameter surface (VoragoMacroMatrix base overrides, VoragoVoiceParams /
    # applyVoiceParams, per-tone sub level, re-prepare-proof ghost setters, the
    # seven VoragoVoice forwarders). ENUMERATED, not globbed - an unregistered TU
    # silently drops out of the build and its cases never run.
    #   vorago_param_surface_test.cpp       SC-006, SC-019 (+ its [long] arms and
    #                                       their short twins), SC-022, SC-023 (1),
    #                                       FR-004's forwarder unit case - the
    #                                       only Phase 12 entry in the
    #                                       -fno-fast-math block below
    #   vorago_macro_retune_probe_test.cpp  FR-060 retune probe  [.probe] - prints
    #                                       only, never in CI
    unit/systems/vorago_param_surface_test.cpp
    unit/systems/vorago_macro_retune_probe_test.cpp
)

target_link_libraries(dsp_systems_tests
    PRIVATE
        KrateDSP
        Catch2::Catch2
        test_helpers
)

target_compile_features(dsp_systems_tests PRIVATE cxx_std_20)

# Layer 4 (effects)
add_executable(dsp_effects_tests
    # Shared Catch2 main + FTZ/DAZ setup (see dsp_test_main.cpp)
    dsp_test_main.cpp
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
    unit/effects/reverb_test.cpp
    unit/effects/fdn_reverb_test.cpp
    unit/effects/aether_reverb_test.cpp
    unit/effects/aether_reverb_matrix_test.cpp
    unit/effects/aether_reverb_spectral_test.cpp
    unit/effects/aether_reverb_perf_test.cpp
    unit/effects/aether_reverb_nonfinite_test.cpp
    # Vorago Phase 9 (specs/vorago-phase9-cavern-space)
    unit/effects/cavern_verb_test.cpp
    unit/effects/cavern_verb_freeze_test.cpp
    unit/effects/cavern_verb_soak_test.cpp
    unit/effects/cavern_verb_perf_test.cpp
    unit/effects/aether_reverb_damper_offset_test.cpp
    # Vorago Phase 10 (specs/vorago-phase10-voice-engine): the composed
    # engine -> CavernVerb -> processOutputStage chain. It lives HERE, not in
    # dsp_systems_tests, so it compiles under this target's KRATE_DSP_AETHER_TEST_HOOKS
    # consistently with every other TU in this executable (FR-084a / OQ3).
    unit/effects/vorago_composed_chain_test.cpp
)

target_link_libraries(dsp_effects_tests
    PRIVATE
        KrateDSP
        Catch2::Catch2
        test_helpers
)

# Seraphis Phase 6: compiles AetherReverb's FR-083 fault-injection hook
# (injectNonFiniteStateForTest). Target-wide, NOT per-source: the hook changes
# the class definition, so every TU linked into this executable must see the
# same one or it is an ODR violation. KrateDSP itself never compiles
# aether_reverb.h (header-only, no .cpp), and dsp_lint_stub is a separate
# EXCLUDE_FROM_ALL object library, so no other image sees a differently-defined
# AetherReverb. The shipping build never defines this.
target_compile_definitions(dsp_effects_tests PRIVATE KRATE_DSP_AETHER_TEST_HOOKS)

target_compile_features(dsp_effects_tests PRIVATE cxx_std_20)


# ==============================================================================
# Fixture location (must not depend on the launch working directory)
# ==============================================================================
# A few tests read/write committed golden fixtures under dsp/tests/ (the
# ArpeggiatorCore SC-002 baselines and the HarmonizerEngine phase-vocoder
# golden). They used to build those paths relative to the process working
# directory, so the fstream only opened when the executable happened to be
# launched from the repo root: under `ctest` (cwd = the build tree) or from any
# other directory the open failed silently and the test reported an EMPTY
# fixture — a "0 >= 1000" failure that looks like a DSP regression but is not.
# Hand the tests the absolute source path instead, matching the
# <PLUGIN>_FIXTURES_DIR convention already used by the plugin test targets
# (e.g. plugins/gradus/tests/CMakeLists.txt:135).
target_compile_definitions(dsp_processors_tests
    PRIVATE KRATE_DSP_TESTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(dsp_systems_tests
    PRIVATE KRATE_DSP_TESTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# Vorago Phase 1 FR-081: the [.harness] trajectory writer resolves its output
# directory from this macro, so CSVs land in the build tree regardless of the
# launch working directory (same rationale as KRATE_DSP_TESTS_DIR above).
target_compile_definitions(dsp_processors_tests
    PRIVATE VORAGO_P1_HARNESS_DIR="${CMAKE_BINARY_DIR}/vorago_p1/")


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
        # Denormal flush behaviour is only observable under IEEE semantics:
        # -ffast-math implies -ffinite-math-only and can enable FTZ itself,
        # which would make the "denormal survives" baseline unobservable.
        unit/core/scoped_denormal_mode_test.cpp
        unit/core/fast_math_test.cpp
        unit/core/interpolation_test.cpp
        unit/core/pitch_utils_test.cpp
        unit/core/scale_harmonizer_test.cpp
        unit/core/grain_envelope_test.cpp
        unit/core/crossfade_utils_test.cpp
        unit/core/sigmoid_test.cpp
        unit/core/chebyshev_test.cpp
        unit/core/test_wavefold_math.cpp
        unit/core/pattern_freeze_types_test.cpp
        unit/core/test_euclidean_pattern.cpp
        unit/core/spectral_simd_test.cpp
        unit/core/filter_design_test.cpp
        unit/core/filter_tables_test.cpp
        unit/core/midi_utils_tests.cpp
        unit/core/modulation_types_test.cpp
        unit/core/modulation_curves_test.cpp
        unit/core/polyblep_test.cpp
        unit/core/phase_utils_test.cpp
        unit/core/wavetable_data_test.cpp
        unit/core/curve_table_test.cpp
        unit/core/transport_sync_test.cpp
        unit/core/chord_generator_test.cpp
        unit/core/hungarian_algorithm_test.cpp
        unit/core/xorshift32_test.cpp
        unit/primitives/envelope_utils_test.cpp
        unit/primitives/adsr_envelope_test.cpp
        unit/primitives/smoother_test.cpp
        unit/primitives/dc_blocker_test.cpp
        unit/primitives/oversampler_test.cpp
        unit/primitives/delay_line_test.cpp
        unit/primitives/lfo_test.cpp
        unit/primitives/biquad_test.cpp
        unit/primitives/fft_test.cpp
        unit/primitives/spectral_buffer_test.cpp
        unit/primitives/spectral_utils_tests.cpp
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
        unit/primitives/pitch_tracker_test.cpp
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
        unit/primitives/spectral_transient_detector_test.cpp
        unit/processors/multimode_filter_test.cpp
        unit/processors/saturation_processor_test.cpp
        unit/processors/envelope_follower_test.cpp
        unit/processors/dynamics_processor_test.cpp
        unit/processors/ducking_processor_test.cpp
        unit/processors/noise_generator_test.cpp
        unit/processors/midside_processor_test.cpp
        unit/processors/diffusion_network_test.cpp
        unit/processors/pitch_shift_processor_test.cpp
        unit/processors/phase_locking_test.cpp
        unit/processors/phase_reset_test.cpp
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
        unit/processors/chorus_test.cpp
        unit/processors/flanger_test.cpp
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
        unit/systems/vector_mixer_tests.cpp
        unit/systems/voice_allocator_test.cpp
        unit/systems/synth_voice_test.cpp
        unit/systems/poly_synth_engine_test.cpp
        unit/systems/selectable_oscillator_test.cpp
        unit/systems/voice_mod_router_test.cpp
        unit/systems/ext_modulation_test.cpp
        unit/systems/harmonizer_engine_test.cpp
        unit/systems/sympathetic_resonance_test.cpp
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
        unit/effects/reverb_test.cpp
        unit/effects/fdn_reverb_test.cpp
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
        unit/processors/additive_oscillator_test.cpp
        unit/processors/chaos_oscillator_test.cpp
        unit/processors/formant_oscillator_test.cpp
        unit/processors/particle_oscillator_test.cpp
        unit/processors/rungler_test.cpp
        unit/processors/spectral_freeze_oscillator_test.cpp
        unit/processors/multi_stage_envelope_test.cpp
        unit/processors/mono_handler_test.cpp
        unit/processors/note_processor_test.cpp
        unit/processors/trance_gate_test.cpp
        # arpeggiator_core_test.cpp split into spec-area siblings (D1)
        unit/processors/arpeggiator_core_core_test.cpp
        unit/processors/arpeggiator_core_lanes_test.cpp
        unit/processors/arpeggiator_core_baseline_test.cpp
        unit/processors/arpeggiator_core_perstepmods_test.cpp
        unit/processors/arpeggiator_core_ratcheting_test.cpp
        unit/processors/arpeggiator_core_euclidean_test.cpp
        unit/processors/arpeggiator_core_conditional_test.cpp
        unit/processors/arpeggiator_core_spicehuman_test.cpp
        unit/processors/arpeggiator_core_dawloop_test.cpp
        unit/processors/arpeggiator_core_scalechord_test.cpp
        unit/processors/arpeggiator_core_curve_race_test.cpp
        unit/processors/ring_modulator_test.cpp
        unit/processors/harmonic_types_tests.cpp
        unit/processors/dual_stft_tests.cpp
        unit/processors/partial_tracker_tests.cpp
        unit/processors/harmonic_oscillator_bank_tests.cpp
        unit/processors/residual_types_tests.cpp
        unit/processors/residual_analyzer_tests.cpp
        unit/processors/residual_synthesizer_tests.cpp
        unit/processors/spectral_coring_estimator_tests.cpp
        unit/processors/subharmonic_validator_tests.cpp
        unit/processors/harmonic_frame_utils_tests.cpp
        unit/processors/harmonic_snapshot_tests.cpp
        unit/processors/test_harmonic_oscillator_bank_stereo.cpp
        unit/processors/multi_pitch_detector_tests.cpp
        unit/processors/multi_source_sieve_tests.cpp
        unit/processors/partial_tracker_polyphonic_tests.cpp
        unit/processors/test_modal_resonator_bank.cpp
        unit/processors/test_modal_bank_stretch.cpp
        unit/processors/impact_exciter_test.cpp
        unit/processors/waveguide_string_test.cpp
        unit/processors/bow_exciter_test.cpp
        unit/processors/bow_exciter_oversampling_test.cpp
        unit/processors/modal_resonator_bank_bow_test.cpp
        unit/processors/waveguide_string_dc_blocker_test.cpp
        unit/processors/bow_waveguide_coupling_test.cpp
        unit/processors/body_resonance_tests.cpp
        unit/processors/test_modal_bank_frequency.cpp
        # Seraphis Phase 4: T014 injects NaN/Inf via bit patterns in this TU
        # (SC-013). Only this one of the five Phase 4 TUs does so - the other
        # four must NOT be listed here.
        unit/systems/continuous_body_test.cpp
        # Seraphis Phase 5: SC-014's clauses (a)-(c) inject NaN/Inf via bit patterns
        # in this TU and need the FP semantics preserved to assert on them.
        # ONLY this one of the four Phase 5 TUs is listed. The other three must NOT
        # be, and for atmosphere_engine_test.cpp that is DELIBERATE AND LOAD-BEARING,
        # not an omission: SC-014's fourth clause
        # (AtmosphereEngine_NonFiniteGuardSurvivesFastMath) and SC-012's sub-case 6
        # live there precisely so the ITERUM_NOINLINE guard and the
        # ordered-comparison clamp are proved under the /fp:fast + -ffast-math
        # settings the header actually ships in. Adding it here would silently
        # disable the only check that has teeth. The perf TU must stay out too:
        # -fno-fast-math would change the figures the baselines are pinned to.
        unit/systems/atmosphere_engine_nonfinite_test.cpp
        # Vorago Phase 10a: SC-008 (b) injects NaN/Inf via bit patterns in this TU.
        # ONLY this one of the five Phase 10a TUs is listed - the perf TU in
        # particular must stay out, or -fno-fast-math would move the figures it
        # measures. Listed here, beside the Phase 5 AtmosphereEngine entry above,
        # because it is the same component's non-finite TU. atmosphere_ghost_test.cpp
        # stays out DELIBERATELY: SC-004's detection-only NaN/Inf check lives there
        # on the fast-math path and uses the bit-pattern predicate
        # VoragoGhostFix::isNonFiniteBits, so the ITERUM_NOINLINE isFinite guards are
        # proved in the FP mode the header actually ships in.
        unit/systems/atmosphere_ghost_nonfinite_test.cpp
        # Seraphis Phase 6: SC-014 injects NaN/Inf via bit patterns in this TU and
        # needs IEEE semantics to assert on them. ONLY this one of the five Phase 6
        # TUs is listed; the other four must NOT be. For aether_reverb_test.cpp and
        # aether_reverb_spectral_test.cpp that is DELIBERATE AND LOAD-BEARING, not
        # an omission: SC-012's non-finite clause, FR-082's input guard and SC-015
        # live there precisely so the ITERUM_NOINLINE guards are proved in the
        # /fp:fast + -ffast-math mode the header actually ships in. The perf TU
        # must stay out too: -fno-fast-math would change the figures its baselines
        # are pinned to.
        unit/effects/aether_reverb_nonfinite_test.cpp
        # Seraphis Phase 7: SC-018 injects NaN/Inf via bit patterns in this TU and
        # needs IEEE semantics to assert on them. ONLY this one of the five Phase 7
        # TUs is listed; the other four must NOT be. seraphis_voice_test.cpp,
        # seraphis_engine_test.cpp and seraphis_macro_test.cpp stay out so the
        # FR-035/FR-072 guards they exercise are proved in the /fp:fast +
        # -ffast-math mode the headers actually ship in. The perf TU must stay out
        # too: -fno-fast-math would change the figures its baselines are pinned to.
        unit/systems/seraphis_nonfinite_test.cpp
        # Vorago Phase 2: SC-015 injects NaN/Inf via bit patterns in this TU and
        # needs IEEE semantics to assert on them. ONLY this one of the four Phase 2
        # TUs is listed; the other three must NOT be. noise_organism_test.cpp and
        # noise_organism_spectral_test.cpp stay out so the FR-008 guards are proved
        # in the /fp:fast + -ffast-math mode the header actually ships in. The perf
        # TU must stay out too: -fno-fast-math would change the figures its
        # baselines are pinned to.
        unit/systems/noise_organism_nonfinite_test.cpp
        # Vorago Phase 3: SC-009 injects NaN/Inf via bit patterns in this TU and
        # needs IEEE semantics to assert on them. ONLY this one of the four Phase 3
        # TUs is listed; the other three must NOT be. resonance_drift_network_test.cpp
        # and resonance_drift_network_spectral_test.cpp stay out so the FR-008/FR-009
        # guards are proved in the /fp:fast + -ffast-math mode the header actually
        # ships in. The perf TU must stay out too: -fno-fast-math would change the
        # figures its baselines are pinned to.
        unit/systems/resonance_drift_network_nonfinite_test.cpp
        # Vorago Phase 4: this TU owns EVERY non-finite value in the phase -
        # SC-016's NaN/Inf audio injection, SC-018 (b)'s poison-then-reprime arm,
        # and FR-009's non-finite setter arm - because all three build their values
        # from bit patterns through a volatile sink and need IEEE semantics to
        # assert on them. ONLY this one of the four Phase 4 TUs is listed; the
        # other three must NOT be, and none of them may name a non-finite value:
        # std::numeric_limits<float>::quiet_NaN()/infinity() fold to finite garbage
        # on the -ffast-math legs, so such a test passes vacuously or reds at
        # random. spectral_smear_test.cpp and spectral_smear_spectral_test.cpp stay
        # out so the FR-008/FR-009 guards are proved in the /fp:fast + -ffast-math
        # mode the header actually ships in (the
        # resonance_drift_network_test.cpp:38-42 precedent). The perf TU stays out
        # too: -fno-fast-math would change the figures its baselines are pinned to.
        unit/processors/spectral_smear_nonfinite_test.cpp
        # Vorago Phase 5: SC-012 injects NaN/Inf via bit patterns in this TU and
        # needs IEEE semantics to assert on them; it is also the only definition
        # of detail::FeedbackEcologyNonFiniteProbe. ONLY this one of the four
        # Phase 5 TUs is listed; the other three must NOT be.
        # feedback_ecology_test.cpp and feedback_ecology_spectral_test.cpp stay
        # out so the FR-008/FR-009 guards are proved in the /fp:fast +
        # -ffast-math mode the header actually ships in. The perf TU stays out
        # too: -fno-fast-math would change the figures its baselines are pinned
        # to.
        unit/systems/feedback_ecology_nonfinite_test.cpp
        # Vorago Phase 6: SC-009 injects NaN/Inf via bit patterns in this TU and
        # needs IEEE semantics to assert on them; it is also the only definition
        # of detail::SubharmonicEngineNonFiniteProbe. ONLY this one of the four
        # Phase 6 TUs is listed. subharmonic_engine_test.cpp and
        # subharmonic_engine_spectral_test.cpp stay out so the FR-008/FR-009
        # guards are proved in the /fp:fast + -ffast-math mode the header
        # actually ships in. The perf TU stays out too: -fno-fast-math would
        # change the figures its baselines are pinned to.
        unit/systems/subharmonic_engine_nonfinite_test.cpp
        # Vorago Phase 7: SC-009 injects NaN/Inf via bit patterns in this TU and
        # needs IEEE semantics to assert on them; it is also the only definition
        # of detail::BloomEngineNonFiniteProbe. ONLY this one of the four Phase 7
        # TUs is listed. bloom_engine_test.cpp and bloom_engine_spectral_test.cpp
        # stay out so the FR-008/FR-009 guards are proved in the /fp:fast +
        # -ffast-math mode the header actually ships in. The perf TU stays out
        # too: -fno-fast-math would change the figures its baselines are pinned to.
        unit/systems/bloom_engine_nonfinite_test.cpp
        # Vorago Phase 8: SC-009 injects NaN/Inf via bit patterns in this TU and
        # needs IEEE semantics to assert on them; it is also the only definition
        # of detail::EcosystemEngineNonFiniteProbe. ONLY this one of the four
        # Phase 8 TUs is listed - the other three stay out so the FR-064/FR-083
        # guards are proved in the /fp:fast + -ffast-math mode the header actually
        # ships in, and so the perf TU's figures are not changed by the flag.
        unit/systems/ecosystem_engine_nonfinite_test.cpp
        # Vorago Phase 12: injects NaN/Inf bit patterns into the Phase 12 setters.
        unit/systems/vorago_param_surface_test.cpp
        PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
    )

    # GCC 13+ -O3 causes pathological performance regression in the reverb
    # processing loop (100x+ slower than -O2). This is a known class of GCC
    # auto-vectorization bug. Cap these files at -O2 on GCC/Clang.
    set_source_files_properties(
        unit/effects/reverb_test.cpp
        unit/effects/fdn_reverb_test.cpp
        PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only -O2"
    )

    # Seraphis Phase 6: same GCC 13+ -O3 reverb pathology as reverb_test.cpp /
    # fdn_reverb_test.cpp above (an N-channel recirculating delay network), and
    # B-1's 60 s always-on wall budget is a requirement. -fno-fast-math is
    # DELIBERATELY ABSENT here: these two TUs must build in the header's shipping
    # FP mode so the ITERUM_NOINLINE finiteness guards are proved under
    # -ffast-math on the macOS leg. Only aether_reverb_nonfinite_test.cpp gets
    # IEEE semantics, and it is listed in the block above -- not here, because
    # COMPILE_FLAGS is a single string property and a file may appear in only one
    # set_source_files_properties() call.
    set_source_files_properties(
        unit/effects/aether_reverb_test.cpp
        unit/effects/aether_reverb_spectral_test.cpp
        PROPERTIES COMPILE_FLAGS "-O2"
    )
endif()

# ==============================================================================
# CTest Integration
# ==============================================================================
catch_discover_tests(dsp_core_tests REPORTER console)
catch_discover_tests(dsp_primitives_tests REPORTER console)
catch_discover_tests(dsp_processors_tests REPORTER console)
catch_discover_tests(dsp_systems_tests REPORTER console)
catch_discover_tests(dsp_effects_tests REPORTER console)
