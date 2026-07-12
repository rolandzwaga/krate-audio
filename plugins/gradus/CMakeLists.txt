# ==============================================================================
# Gradus VST3 Plugin — Standalone Step Arpeggiator
# ==============================================================================

# ==============================================================================
# Version + Generated Files (see cmake/KratePlugin.cmake)
# ==============================================================================
krate_plugin_read_version(GRADUS)
krate_plugin_configure_generated_files()

# ==============================================================================
# Plugin Target
# ==============================================================================
set(PLUGIN_NAME "${GRADUS_NAME}")

smtg_add_vst3plugin(${PLUGIN_NAME}
    # Plugin Entry
    src/entry.cpp

    # Processor (Audio Thread)
    src/processor/processor.h
    src/processor/processor.cpp

    # Controller (UI Thread)
    src/controller/controller.h
    src/controller/controller.cpp
    src/controller/controller_arp.cpp
    src/controller/controller_verify_view.cpp
    src/controller/controller_view_sync.cpp
    src/controller/controller_presets.cpp
    src/controller/parameter_helpers.h

    # Parameters
    src/parameters/arpeggiator_params.h
    src/parameters/note_value_ui.h

    # VST3 SDK Common Sources (for MemoryStream used by preset saving)
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp

    # Circular Ring UI (Gradus-specific)
    src/ui/ring_geometry.h
    src/ui/ring_data_bridge.h
    src/ui/ring_renderer.h
    src/ui/ring_display.h
    src/ui/euclidean_center.h
    src/ui/detail_strip.h
    src/ui/lane_tab_bar.h

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
)

# Configure version file
smtg_target_configure_version_file(${PLUGIN_NAME})

# ==============================================================================
# Platform-Specific Configuration
# ==============================================================================
krate_plugin_platform_setup(${PLUGIN_NAME}
    TAG GRADUS
    BUNDLE_BASE com.krateaudio.gradus
    ENTITLEMENTS Gradus.entitlements
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
