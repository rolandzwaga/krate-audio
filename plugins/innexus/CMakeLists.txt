# ==============================================================================
# Innexus VST3 Plugin
# ==============================================================================
# Constitution Principle VII: Modern CMake with target-based configuration
# Links against shared KrateDSP library

# ==============================================================================
# Version + Generated Files (see cmake/KratePlugin.cmake)
# ==============================================================================
krate_plugin_read_version(INNEXUS)
krate_plugin_configure_generated_files()

# ==============================================================================
# Plugin Target
# ==============================================================================
set(PLUGIN_NAME "${INNEXUS_NAME}")

smtg_add_vst3plugin(${PLUGIN_NAME}
    # Plugin Entry
    src/entry.cpp

    # Processor (Audio Thread)
    src/processor/processor.h
    src/processor/processor.cpp
    src/processor/processor_params.cpp
    src/processor/processor_state.cpp
    src/processor/processor_midi.cpp
    src/processor/processor_messages.cpp

    # Controller (UI Thread)
    src/controller/controller.h
    src/controller/controller.cpp
    src/controller/controller_param_display.cpp
    src/controller/controller_presets.cpp
    # further split (D2): state, view sync, ADSR wiring
    src/controller/controller_state.cpp
    src/controller/controller_view_sync.cpp
    src/controller/controller_adsr.cpp
    src/controller/display_data.h
    src/controller/modulator_sub_controller.h
    src/controller/modulator_sub_controller.cpp
    src/controller/views/harmonic_display_view.h
    src/controller/views/harmonic_display_view.cpp
    src/controller/views/confidence_indicator_view.h
    src/controller/views/confidence_indicator_view.cpp
    src/controller/views/memory_slot_status_view.h
    src/controller/views/memory_slot_status_view.cpp
    src/controller/views/evolution_position_view.h
    src/controller/views/evolution_position_view.cpp
    src/controller/views/modulator_activity_view.h
    src/controller/views/modulator_activity_view.cpp
    src/controller/adsr_expanded_overlay.h

    # VST3 SDK Common Sources (for MemoryStream used by preset saving)
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp

    # DSP (Analysis Pipeline)
    src/dsp/sample_analysis.h
    src/dsp/sample_analyzer.h
    src/dsp/sample_analyzer.cpp
    src/dsp/live_analysis_pipeline.h
    src/dsp/live_analysis_pipeline.cpp

    # Parameters
    src/parameters/note_value_ui.h

    # Shared
    src/plugin_ids.h
    src/version.h
)

# Link SDK, VSTGUI, and KrateDSP
target_link_libraries(${PLUGIN_NAME}
    PRIVATE
        sdk
        vstgui_support
        KrateDSP
        KratePluginsShared
)

# Include directories
target_include_directories(${PLUGIN_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/extern/dr_libs
)

# Configure version file
smtg_target_configure_version_file(${PLUGIN_NAME})

# ==============================================================================
# Platform-Specific Configuration (macOS AU targets + Windows .rc)
# ==============================================================================
krate_plugin_platform_setup(${PLUGIN_NAME}
    TAG INNEXUS
    BUNDLE_BASE com.krateaudio.innexus
    ENTITLEMENTS Innexus.entitlements
    KIND instrument
)

# ==============================================================================
# VSTGUI Resources
# ==============================================================================
smtg_target_add_plugin_resources(${PLUGIN_NAME}
    RESOURCES
        resources/editor.uidesc
)

# ==============================================================================
# Post-Build install + compiler warnings (see cmake/KratePlugin.cmake)
# ==============================================================================
krate_plugin_install_to_system(${PLUGIN_NAME})
krate_plugin_install_presets(${PLUGIN_NAME})
krate_plugin_set_warnings(${PLUGIN_NAME})

# ==============================================================================
# Plugin Tests
# ==============================================================================
if(VSTWORK_BUILD_TESTS)
    add_subdirectory(tests)
endif()
