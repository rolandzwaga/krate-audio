# ==============================================================================
# KrateDSP Library
# ==============================================================================
# Shared DSP library for all Krate Audio plugins
# Contains: Layers 0-4 (core utilities, primitives, processors, systems, effects)

# Define KrateDSP as a static library
# Most DSP code is header-only, but dsp_utils.cpp contains implementations
add_library(KrateDSP STATIC
    include/krate/dsp/core/dsp_utils.cpp
)

# Public include directory for consumers
target_include_directories(KrateDSP
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# C++20 requirement
target_compile_features(KrateDSP PUBLIC cxx_std_20)

# macOS universal binary support (x86_64 + arm64)
# Uses VST3 SDK function - no-op on non-macOS platforms
smtg_target_setup_universal_binary(KrateDSP)

# Compiler warnings
if(MSVC)
    target_compile_options(KrateDSP PRIVATE
        /W4
        /permissive-
        /Zc:__cplusplus
        /wd4100  # Unreferenced formal parameter
    )
else()
    target_compile_options(KrateDSP PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter
    )
endif()

# ==============================================================================
# Header Files (for IDE visibility)
# ==============================================================================
# Layer 0: Core Utilities
set(KRATE_DSP_CORE_HEADERS
    include/krate/dsp/core/block_context.h
    include/krate/dsp/core/crossfade_utils.h
    include/krate/dsp/core/db_utils.h
    include/krate/dsp/core/dsp_utils.h
    include/krate/dsp/core/fast_math.h
    include/krate/dsp/core/grain_envelope.h
    include/krate/dsp/core/interpolation.h
    include/krate/dsp/core/math_constants.h
    include/krate/dsp/core/note_value.h
    include/krate/dsp/core/pitch_utils.h
    include/krate/dsp/core/random.h
    include/krate/dsp/core/stereo_utils.h
    include/krate/dsp/core/window_functions.h
)

# Layer 1: Primitives
set(KRATE_DSP_PRIMITIVES_HEADERS
    include/krate/dsp/primitives/biquad.h
    include/krate/dsp/primitives/bit_crusher.h
    include/krate/dsp/primitives/crossfading_delay_line.h
    include/krate/dsp/primitives/delay_line.h
    include/krate/dsp/primitives/fft.h
    include/krate/dsp/primitives/grain_pool.h
    include/krate/dsp/primitives/i_feedback_processor.h
    include/krate/dsp/primitives/lfo.h
    include/krate/dsp/primitives/oversampler.h
    include/krate/dsp/primitives/reverse_buffer.h
    include/krate/dsp/primitives/sample_rate_reducer.h
    include/krate/dsp/primitives/smoother.h
    include/krate/dsp/primitives/spectral_buffer.h
    include/krate/dsp/primitives/spectrum_fifo.h
    include/krate/dsp/primitives/stft.h
)

# Layer 2: Processors
set(KRATE_DSP_PROCESSORS_HEADERS
    include/krate/dsp/processors/diffusion_network.h
    include/krate/dsp/processors/ducking_processor.h
    include/krate/dsp/processors/dynamics_processor.h
    include/krate/dsp/processors/envelope_follower.h
    include/krate/dsp/processors/grain_processor.h
    include/krate/dsp/processors/grain_scheduler.h
    include/krate/dsp/processors/midside_processor.h
    include/krate/dsp/processors/multimode_filter.h
    include/krate/dsp/processors/noise_generator.h
    include/krate/dsp/processors/pitch_shift_processor.h
    include/krate/dsp/processors/reverse_feedback_processor.h
    include/krate/dsp/processors/saturation_processor.h
)

# Layer 3: Systems
set(KRATE_DSP_SYSTEMS_HEADERS
    include/krate/dsp/systems/character_processor.h
    include/krate/dsp/systems/delay_engine.h
    include/krate/dsp/systems/feedback_network.h
    include/krate/dsp/systems/flexible_feedback_network.h
    include/krate/dsp/systems/granular_engine.h
    include/krate/dsp/systems/modulation_matrix.h
    include/krate/dsp/systems/stereo_field.h
    include/krate/dsp/systems/tap_manager.h
)

# Layer 4: Effects
set(KRATE_DSP_EFFECTS_HEADERS
    include/krate/dsp/effects/bbd_delay.h
    include/krate/dsp/effects/digital_delay.h
    include/krate/dsp/effects/ducking_delay.h
    include/krate/dsp/effects/freeze_mode.h
    include/krate/dsp/effects/granular_delay.h
    include/krate/dsp/effects/multi_tap_delay.h
    include/krate/dsp/effects/ping_pong_delay.h
    include/krate/dsp/effects/reverse_delay.h
    include/krate/dsp/effects/shimmer_delay.h
    include/krate/dsp/effects/spectral_delay.h
    include/krate/dsp/effects/tape_delay.h
)

# Add headers to target for IDE visibility
target_sources(KrateDSP PRIVATE
    ${KRATE_DSP_CORE_HEADERS}
    ${KRATE_DSP_PRIMITIVES_HEADERS}
    ${KRATE_DSP_PROCESSORS_HEADERS}
    ${KRATE_DSP_SYSTEMS_HEADERS}
    ${KRATE_DSP_EFFECTS_HEADERS}
)

# ==============================================================================
# Tests
# ==============================================================================
if(VSTWORK_BUILD_TESTS)
    add_subdirectory(tests)
endif()
