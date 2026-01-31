# ==============================================================================
# Disrumpo VST3 Plugin
# ==============================================================================
# Constitution Principle VII: Modern CMake with target-based configuration
# Links against shared KrateDSP library

# ==============================================================================
# Version from version.json (Single Source of Truth)
# ==============================================================================
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/version.json" VERSION_JSON)
string(JSON DISRUMPO_VERSION GET ${VERSION_JSON} "version")
string(JSON DISRUMPO_NAME GET ${VERSION_JSON} "name")
string(JSON DISRUMPO_DESCRIPTION GET ${VERSION_JSON} "description")
string(JSON DISRUMPO_PUBLISHER GET ${VERSION_JSON} "publisher")
string(JSON DISRUMPO_URL GET ${VERSION_JSON} "url")
string(JSON DISRUMPO_COPYRIGHT GET ${VERSION_JSON} "copyright")

# Parse version string (e.g., "0.1.0") into components for configure_file
string(REPLACE "." ";" VERSION_LIST ${DISRUMPO_VERSION})
list(GET VERSION_LIST 0 PROJECT_VERSION_MAJOR)
list(GET VERSION_LIST 1 PROJECT_VERSION_MINOR)
list(GET VERSION_LIST 2 PROJECT_VERSION_PATCH)

message(STATUS "[DISRUMPO] Version: ${DISRUMPO_VERSION} (${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH})")

# ==============================================================================
# Version Header Generation
# ==============================================================================
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/version.h.in"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/version.h"
    @ONLY
)

# ==============================================================================
# Windows Resource Generation
# ==============================================================================
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/win32resource.rc.in"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/win32resource.rc"
    @ONLY
)

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

    # Custom Views (UI Components)
    src/controller/views/spectrum_display.h
    src/controller/views/spectrum_display.cpp
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
# Platform-Specific Configuration
# ==============================================================================
if(SMTG_MAC)
    smtg_target_set_bundle(${PLUGIN_NAME}
        BUNDLE_IDENTIFIER "com.krateaudio.disrumpo"
        COMPANY_NAME "Krate Audio"
    )

    # ==========================================================================
    # Audio Unit v2 Support (macOS only)
    # ==========================================================================
    option(SMTG_ENABLE_AUV2_BUILDS "Enable AudioUnit v2 builds" OFF)

    if(XCODE AND SMTG_ENABLE_AUV2_BUILDS)
        message(STATUS "[DISRUMPO] AudioUnit v2 build enabled")

        include(FetchContent)
        FetchContent_Declare(
            AudioUnitSDK
            GIT_REPOSITORY https://github.com/apple/AudioUnitSDK.git
            GIT_TAG AudioUnitSDK-1.1.0
        )
        FetchContent_MakeAvailable(AudioUnitSDK)
        FetchContent_GetProperties(
            AudioUnitSDK
            SOURCE_DIR SMTG_AUDIOUNIT_SDK_PATH
        )

        list(APPEND CMAKE_MODULE_PATH "${vst3sdk_SOURCE_DIR}/cmake/modules")
        include(SMTG_AddVST3AuV2)

        smtg_target_add_auv2(${PLUGIN_NAME}_AU
            BUNDLE_NAME ${PLUGIN_NAME}
            BUNDLE_IDENTIFIER com.krateaudio.disrumpo.audiounit
            INFO_PLIST_TEMPLATE ${CMAKE_CURRENT_SOURCE_DIR}/resources/au-info.plist
            VST3_PLUGIN_TARGET ${PLUGIN_NAME}
        )

        message(STATUS "[DISRUMPO] AudioUnit target: ${PLUGIN_NAME}_AU")
    elseif(SMTG_ENABLE_AUV2_BUILDS AND NOT XCODE)
        message(WARNING "[DISRUMPO] AUv2 builds require Xcode generator (-G Xcode)")
    endif()

elseif(SMTG_WIN)
    target_sources(${PLUGIN_NAME}
        PRIVATE
            resources/win32resource.rc
    )
endif()

# ==============================================================================
# Post-Build: Copy to VST3 System Folder
# ==============================================================================
option(VSTWORK_COPY_TO_VST3_FOLDER "Copy plugin to system VST3 folder after build" ON)

if(VSTWORK_COPY_TO_VST3_FOLDER AND SMTG_WIN)
    set(VST3_SYSTEM_FOLDER "C:/Program Files/Common Files/VST3")
    set(VST3_BUILD_OUTPUT "${CMAKE_BINARY_DIR}/VST3/$<CONFIG>/${PLUGIN_NAME}.vst3")

    add_custom_command(TARGET ${PLUGIN_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "[VSTWORK] Copying plugin to ${VST3_SYSTEM_FOLDER}..."
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${VST3_SYSTEM_FOLDER}/${PLUGIN_NAME}.vst3"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${VST3_BUILD_OUTPUT}"
            "${VST3_SYSTEM_FOLDER}/${PLUGIN_NAME}.vst3"
        COMMAND ${CMAKE_COMMAND} -E echo "[VSTWORK] Plugin installed to ${VST3_SYSTEM_FOLDER}/${PLUGIN_NAME}.vst3"
        COMMENT "Installing ${PLUGIN_NAME}.vst3 to system VST3 folder"
        VERBATIM
    )
endif()

# ==============================================================================
# Compiler Warnings (Constitution Principle III: Modern C++)
# ==============================================================================
if(MSVC)
    target_compile_options(${PLUGIN_NAME} PRIVATE
        /W4
        /permissive-
        /Zc:__cplusplus
        /wd4100
        /wd4458
    )
    if(CMAKE_CXX_COMPILER_LAUNCHER OR CMAKE_C_COMPILER_LAUNCHER)
        target_compile_options(${PLUGIN_NAME} PRIVATE /Z7)
    endif()
else()
    target_compile_options(${PLUGIN_NAME} PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter
    )
endif()

# ==============================================================================
# Tests
# ==============================================================================
if(VSTWORK_BUILD_TESTS)
    add_subdirectory(tests)
endif()
