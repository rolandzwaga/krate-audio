# ==============================================================================
# Disrumpo VST3 Plugin
# ==============================================================================
# Constitution Principle VII: Modern CMake with target-based configuration
# Links against shared KrateDSP library

# ==============================================================================
# Version + Generated Files (see cmake/KratePlugin.cmake)
# ==============================================================================
krate_plugin_read_version(DISRUMPO)
krate_plugin_configure_generated_files()

# ==============================================================================
# Plugin Target
# ==============================================================================
set(PLUGIN_NAME "${DISRUMPO_NAME}")

smtg_add_vst3plugin(${PLUGIN_NAME}
    # Plugin Entry
    src/entry.cpp

    # Processor (Audio Thread)
    src/processor/processor.h
    src/processor/processor.cpp

    # Controller (UI Thread)
    src/controller/controller.h
    src/controller/controller.cpp
    src/controller/parameter_registration.cpp
    src/controller/visibility_controllers.h
    src/controller/visibility_controllers.cpp
    src/controller/format_helpers.h
    src/controller/custom_buttons.h

    # Custom Views (UI Components)
    src/controller/views/spectrum_display.h
    src/controller/views/spectrum_display.cpp
    src/controller/views/spectrum_analyzer.h
    src/controller/views/morph_pad.h
    src/controller/views/morph_pad.cpp
    src/controller/views/dynamic_node_selector.h
    src/controller/views/dynamic_node_selector.cpp
    src/controller/views/node_editor_border.h
    src/controller/views/node_editor_border.cpp
    src/controller/views/sweep_indicator.h
    src/controller/views/sweep_indicator.cpp
    src/controller/views/custom_curve_editor.h
    src/controller/views/custom_curve_editor.cpp
    src/controller/views/mod_slider.h

    # Sub-Controllers for Band Parameter Remapping
    src/controller/sub_controllers.h

    # Animated Expand Controller (Spec 012)
    src/controller/animated_expand_controller.h
    src/controller/animated_expand_controller.cpp

    # Keyboard Shortcut Handler (Spec 012)
    src/controller/keyboard_shortcut_handler.h
    src/controller/keyboard_shortcut_handler.cpp

    # Morph Link Functions (US8)
    src/controller/morph_link.h
    src/controller/morph_link.cpp

    # DSP Components
    src/dsp/distortion_types.h
    src/dsp/distortion_adapter.h
    src/dsp/distortion_adapter.cpp
    src/dsp/band_processor.h
    src/dsp/band_state.h
    src/dsp/crossover_network.h
    src/dsp/morph_engine.h
    src/dsp/morph_engine.cpp
    src/dsp/morph_node.h

    # VST3 SDK Common Sources (for MemoryStream used by preset saving)
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp

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

# ==============================================================================
# VSTGUI Resources
# ==============================================================================
file(GLOB BITMAP_RESOURCES CONFIGURE_DEPENDS resources/bitmaps/*.png)
smtg_target_add_plugin_resources(${PLUGIN_NAME}
    RESOURCES
        resources/editor.uidesc
        ${BITMAP_RESOURCES}
)

# Include directories
target_include_directories(${PLUGIN_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Configure version file
smtg_target_configure_version_file(${PLUGIN_NAME})

# ==============================================================================
# Platform-Specific Configuration (macOS AU targets + Windows .rc)
# ==============================================================================
krate_plugin_platform_setup(${PLUGIN_NAME}
    TAG DISRUMPO
    BUNDLE_BASE com.krateaudio.disrumpo
    ENTITLEMENTS Disrumpo.entitlements
    KIND effect
)

# ==============================================================================
# Post-Build install + compiler warnings (see cmake/KratePlugin.cmake)
# ==============================================================================
krate_plugin_install_to_system(${PLUGIN_NAME})
krate_plugin_install_presets(${PLUGIN_NAME})
krate_plugin_set_warnings(${PLUGIN_NAME})

# ==============================================================================
# Tests
# ==============================================================================
if(VSTWORK_BUILD_TESTS)
    add_subdirectory(tests)
endif()
