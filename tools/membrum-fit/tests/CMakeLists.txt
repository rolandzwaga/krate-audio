add_executable(membrum_fit_tests
    unit/test_smoke.cpp
    unit/test_loader.cpp
    unit/test_body_classifier.cpp
    unit/test_sfz_ingest.cpp
    unit/test_unnatural_fit.cpp
    unit/modal/test_matrix_pencil.cpp
    unit/modal/test_mp_snr.cpp
    unit/mapper_inversion/test_membrane_inverse.cpp
    unit/preset_io/test_pad_writer.cpp
    unit/preset_io/test_kit_writer_bytes.cpp
    unit/test_golden_kick.cpp
    unit/test_golden_corpus.cpp
    unit/refinement/test_global_crs.cpp
    unit/test_cli_e2e.cpp
    unit/test_body_classifier_full.cpp
    unit/mapper_inversion/test_all_inversions_defaults.cpp
    unit/mapper_inversion/test_membrane_roundtrip.cpp
    unit/mapper_inversion/test_all_body_roundtrips.cpp
    unit/refinement/test_bobyqa.cpp
    unit/test_features.cpp
    unit/test_segmentation.cpp
    unit/preset_io/test_json_writer.cpp
    unit/preset_io/test_pad_blob_roundtrip.cpp
    unit/preset_io/test_pad_json.cpp
    unit/refinement/test_loss.cpp
    unit/test_pitch_env_detector.cpp
    unit/test_wav_dir.cpp
    unit/test_sfz_notename.cpp
    unit/modal/test_mode_selection.cpp
    unit/test_loader_advanced.cpp
    unit/test_cli_parse.cpp
)

target_link_libraries(membrum_fit_tests PRIVATE
    membrum_fit_core
    Catch2::Catch2
)

target_include_directories(membrum_fit_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(membrum_fit_tests PRIVATE cxx_std_20)

set_target_properties(membrum_fit_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

include(Catch)
catch_discover_tests(membrum_fit_tests REPORTER console)
