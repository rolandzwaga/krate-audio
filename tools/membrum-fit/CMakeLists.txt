# ==============================================================================
# tools/membrum-fit -- Offline drum-sample to Membrum-preset fitter
# ==============================================================================
# See specs/membrum-fit-tool.md for the analysis-by-synthesis design.
# ==============================================================================

set(MEMBRUM_FIT_SOURCES
    src/main.cpp
    src/cli.cpp
    src/loader.cpp
    src/segmentation.cpp
    src/features.cpp
    src/exciter_classifier.cpp
    src/body_classifier.cpp
    src/tone_shaper_fit.cpp
    src/unnatural_fit.cpp

    src/modal/matrix_pencil.cpp
    src/modal/esprit.cpp
    src/modal/mode_selection.cpp

    src/mapper_inversion/membrane_inverse.cpp
    src/mapper_inversion/plate_inverse.cpp
    src/mapper_inversion/shell_inverse.cpp
    src/mapper_inversion/bell_inverse.cpp
    src/mapper_inversion/string_inverse.cpp
    src/mapper_inversion/noise_body_inverse.cpp

    src/refinement/render_voice.cpp
    src/refinement/loss.cpp
    src/refinement/bobyqa_refine.cpp
    src/refinement/cmaes_refine.cpp

    src/ingestion/wav_dir.cpp
    src/ingestion/sfz_ingest.cpp

    src/preset_io/kit_preset_writer.cpp
    src/preset_io/pad_preset_writer.cpp
    src/preset_io/json_writer.cpp
)

# Core analysis library -- lets tests link against the exact same code paths as
# the CLI binary without re-compiling main.cpp.
add_library(membrum_fit_core STATIC
    ${MEMBRUM_FIT_SOURCES}
)

target_include_directories(membrum_fit_core PUBLIC
    src
    ${CMAKE_SOURCE_DIR}/extern/dr_libs
)

target_link_libraries(membrum_fit_core PUBLIC
    KrateDSP
    membrum_dsp
    membrum_preset_io
    Eigen3::Eigen
    nlohmann_json::nlohmann_json
    CLI11::CLI11
)

if(MEMBRUM_FIT_ENABLE_NLOPT)
    target_link_libraries(membrum_fit_core PUBLIC nlopt)
    target_compile_definitions(membrum_fit_core PUBLIC MEMBRUM_FIT_HAVE_NLOPT=1)
else()
    target_compile_definitions(membrum_fit_core PUBLIC MEMBRUM_FIT_HAVE_NLOPT=0)
endif()

target_compile_features(membrum_fit_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(membrum_fit_core PRIVATE
        /W4
        /permissive-
        /Zc:__cplusplus
        /wd4100  # unused parameter
        /wd4244  # double -> float narrowing (Eigen / signal math)
        /wd4267  # size_t -> int narrowing
    )
else()
    target_compile_options(membrum_fit_core PRIVATE
        -Wall -Wextra -Wpedantic -Wno-unused-parameter
    )
endif()

# ==============================================================================
# membrum_fit executable -- CLI entry point
# ==============================================================================
# main.cpp is linked separately so tests can reuse every other translation unit.
add_executable(membrum_fit src/entry.cpp)
target_link_libraries(membrum_fit PRIVATE membrum_fit_core)
set_target_properties(membrum_fit PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# ==============================================================================
# membrum-fit-gen -- companion tool: render a JSON PadConfig to WAV
# ==============================================================================
# Used to generate the golden-test corpus (spec §5.3).
add_executable(membrum_fit_gen src/gen_entry.cpp)
target_link_libraries(membrum_fit_gen PRIVATE membrum_fit_core)
set_target_properties(membrum_fit_gen PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# ==============================================================================
# Tests
# ==============================================================================
if(VSTWORK_BUILD_TESTS)
    add_subdirectory(tests)
endif()
