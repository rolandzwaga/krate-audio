# ==============================================================================
# Membrum VST3 Plugin -- Synthesized Drum Machine
# ==============================================================================

# ==============================================================================
# Version + Generated Files (see cmake/KratePlugin.cmake)
# ==============================================================================
krate_plugin_read_version(MEMBRUM)
krate_plugin_configure_generated_files()

# ==============================================================================
# membrum_dsp -- INTERFACE library exposing SDK-free header-only DSP
# ==============================================================================
# Consumed by the Membrum plugin target AND by out-of-plugin tools
# (e.g. tools/membrum-fit) that need to render the production voice without
# pulling in VST3 SDK dependencies. Nothing in this target includes
# pluginterfaces/* or public.sdk/*.
add_library(membrum_dsp INTERFACE)

target_sources(membrum_dsp INTERFACE
    # Shared DSP types
    src/dsp/exciter_type.h
    src/dsp/body_model_type.h
    src/dsp/voice_common_params.h
    src/dsp/exciter_bank.h
    src/dsp/body_bank.h
    src/dsp/tone_shaper.h
    src/dsp/drum_voice.h
    src/dsp/membrane_modes.h

    # Per-pad configuration + default kit templates
    src/dsp/pad_config.h
    src/dsp/default_kit.h

    # Phase 5: cross-pad coupling
    src/dsp/pad_category.h
    src/dsp/coupling_matrix.h

    # Phase 6: publishers (header-only, SDK-free)
    src/dsp/pad_glow_publisher.h

    # Body backends + mode tables
    src/dsp/bodies/membrane_body.h
    src/dsp/bodies/plate_body.h
    src/dsp/bodies/shell_body.h
    src/dsp/bodies/string_body.h
    src/dsp/bodies/bell_body.h
    src/dsp/bodies/noise_body.h
    src/dsp/bodies/plate_modes.h
    src/dsp/bodies/shell_modes.h
    src/dsp/bodies/bell_modes.h
    src/dsp/bodies/membrane_mapper.h
    src/dsp/bodies/plate_mapper.h
    src/dsp/bodies/shell_mapper.h
    src/dsp/bodies/bell_mapper.h
    src/dsp/bodies/string_mapper.h
    src/dsp/bodies/noise_body_mapper.h

    # Exciter backends
    src/dsp/exciters/impulse_exciter.h
    src/dsp/exciters/mallet_exciter.h
    src/dsp/exciters/noise_burst_exciter.h
    src/dsp/exciters/friction_exciter.h
    src/dsp/exciters/fm_impulse_exciter.h
    src/dsp/exciters/feedback_exciter.h

    # Unnatural Zone modules
    src/dsp/unnatural/unnatural_zone.h
    src/dsp/unnatural/material_morph.h
    src/dsp/unnatural/mode_inject.h
    src/dsp/unnatural/nonlinear_coupling.h
)

target_include_directories(membrum_dsp INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(membrum_dsp INTERFACE
    KrateDSP
)

target_compile_features(membrum_dsp INTERFACE cxx_std_20)

# ==============================================================================
# membrum_preset_io -- STATIC library wrapping the SDK-coupled state codec
# ==============================================================================
# The state codec pulls in pluginterfaces/base/ibstream.h, so it must live in a
# target that links the VST3 SDK. Both the Membrum plugin and tools/membrum-fit
# consume this library directly rather than duplicating byte-layout code.
add_library(membrum_preset_io STATIC
    src/state/state_codec.h
    src/state/state_codec.cpp
    src/preset/membrum_preset_container.h
    src/preset/membrum_preset_container.cpp
    # SDK helper: MemoryStream used by both the plugin and tools/membrum-fit.
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp
)

target_include_directories(membrum_preset_io PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(membrum_preset_io
    PUBLIC
        membrum_dsp
        sdk
        pluginterfaces
)

target_compile_features(membrum_preset_io PUBLIC cxx_std_20)

# macOS: match universal-binary arches of the plugin bundle (arm64 + x86_64).
# Without this, Xcode builds this static lib only for the runner's native arch
# (arm64 on Apple Silicon CI runners), causing the x86_64 slice of the plugin
# to fail linking with "Undefined symbols for architecture x86_64".
smtg_target_setup_universal_binary(membrum_preset_io)

# ==============================================================================
# Plugin Target
# ==============================================================================
set(PLUGIN_NAME "${MEMBRUM_NAME}")

smtg_add_vst3plugin(${PLUGIN_NAME}
    # Plugin Entry
    src/entry.cpp

    # Processor (Audio Thread)
    src/processor/processor.h
    src/processor/processor.cpp

    # Controller (UI Thread)
    src/controller/controller.h
    src/controller/controller.cpp
    src/controller/controller_state_codec.h
    src/controller/controller_state_codec.cpp

    # Preset config (plugin-local, header-only)
    src/preset/membrum_preset_config.h

    # Phase 6: macros, custom editor, meters block (SDK-coupled)
    src/processor/macro_mapper.cpp
    src/processor/meters_block.h
    src/ui/pad_grid_view.h
    src/ui/pad_grid_view.cpp
    src/ui/kit_meters_view.h
    src/ui/kit_meters_view.cpp
    src/ui/membrum_editor_controller.cpp

    # Phase 3: voice pool (multi-voice polyphony, stealing, choke groups)
    src/voice_pool/voice_pool.h
    src/voice_pool/voice_pool.cpp
    src/voice_pool/voice_meta.h
    src/voice_pool/voice_stealing_policy.h
    src/voice_pool/choke_group_table.h

    # Shared
    src/plugin_ids.h
    src/version.h
)

# Link SDK, VSTGUI, KrateDSP, and the new membrum_dsp / membrum_preset_io targets
target_link_libraries(${PLUGIN_NAME}
    PRIVATE
        sdk
        vstgui_support
        KrateDSP
        KratePluginsShared
        membrum_dsp
        membrum_preset_io
)

# ==============================================================================
# VSTGUI Resources (Phase 6: custom editor)
# ==============================================================================
smtg_target_add_plugin_resources(${PLUGIN_NAME}
    RESOURCES
        resources/editor.uidesc
)

# Include directories
target_include_directories(${PLUGIN_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Configure version file
smtg_target_configure_version_file(${PLUGIN_NAME})

# ==============================================================================
# Platform-Specific Configuration
# ==============================================================================
krate_plugin_platform_setup(${PLUGIN_NAME}
    TAG MEMBRUM
    BUNDLE_BASE com.krateaudio.membrum
    ENTITLEMENTS Membrum.entitlements
    KIND instrument
)

# ==============================================================================
# Post-Build install + compiler warnings (see cmake/KratePlugin.cmake)
# ==============================================================================
krate_plugin_install_to_system(${PLUGIN_NAME})
krate_plugin_set_warnings(${PLUGIN_NAME})

# ==============================================================================
# Plugin Tests
# ==============================================================================
if(VSTWORK_BUILD_TESTS)
    add_subdirectory(tests)
endif()
