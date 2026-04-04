# ==============================================================================
# Gradus VST3 Plugin — Standalone Step Arpeggiator
# ==============================================================================

# ==============================================================================
# Version from version.json (Single Source of Truth)
# ==============================================================================
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/version.json" VERSION_JSON)
string(JSON GRADUS_VERSION GET ${VERSION_JSON} "version")
string(JSON GRADUS_NAME GET ${VERSION_JSON} "name")
string(JSON GRADUS_DESCRIPTION GET ${VERSION_JSON} "description")
string(JSON GRADUS_PUBLISHER GET ${VERSION_JSON} "publisher")
string(JSON GRADUS_URL GET ${VERSION_JSON} "url")
string(JSON GRADUS_COPYRIGHT GET ${VERSION_JSON} "copyright")

# Parse version string (e.g., "1.0.0") into components for configure_file
string(REPLACE "." ";" VERSION_LIST ${GRADUS_VERSION})
list(GET VERSION_LIST 0 PROJECT_VERSION_MAJOR)
list(GET VERSION_LIST 1 PROJECT_VERSION_MINOR)
list(GET VERSION_LIST 2 PROJECT_VERSION_PATCH)

message(STATUS "[GRADUS] Version: ${GRADUS_VERSION} (${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH})")

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
if(SMTG_MAC)
    smtg_target_set_bundle(${PLUGIN_NAME}
        BUNDLE_IDENTIFIER "com.krateaudio.gradus"
        COMPANY_NAME "Krate Audio"
    )

    # ==========================================================================
    # Audio Unit v2 Support (macOS only)
    # ==========================================================================
    option(SMTG_ENABLE_AUV2_BUILDS "Enable AudioUnit v2 builds" OFF)

    if(XCODE AND SMTG_ENABLE_AUV2_BUILDS)
        message(STATUS "[GRADUS] AudioUnit v2 build enabled")

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
            BUNDLE_IDENTIFIER com.krateaudio.gradus.audiounit
            INFO_PLIST_TEMPLATE ${CMAKE_CURRENT_SOURCE_DIR}/resources/au-info.plist
            VST3_PLUGIN_TARGET ${PLUGIN_NAME}
        )

        message(STATUS "[GRADUS] AudioUnit target: ${PLUGIN_NAME}_AU")
    elseif(SMTG_ENABLE_AUV2_BUILDS AND NOT XCODE)
        message(WARNING "[GRADUS] AUv2 builds require Xcode generator (-G Xcode)")
    endif()

    # ==========================================================================
    # Audio Unit v3 Support (macOS + Xcode only)
    # ==========================================================================
    option(SMTG_ENABLE_AUV3_BUILDS "Enable AudioUnit v3 builds" OFF)

    if(XCODE AND SMTG_ENABLE_AUV3_BUILDS)
        message(STATUS "[GRADUS] AudioUnit v3 build enabled")

        list(APPEND CMAKE_MODULE_PATH "${vst3sdk_SOURCE_DIR}/cmake/modules")
        include(SMTG_AddVST3AuV3)

        set(AUV3_SHARED "${CMAKE_SOURCE_DIR}/plugins/shared/resources/auv3")
        set(AUV3_LOCAL  "${CMAKE_CURRENT_SOURCE_DIR}/resources/auv3")

        set(auv3_app_sources
            "${AUV3_SHARED}/macOS/Sources/AppDelegate.h"
            "${AUV3_SHARED}/macOS/Sources/AppDelegate.m"
            "${AUV3_SHARED}/macOS/Sources/ViewController.h"
            "${AUV3_SHARED}/instrument/ViewController.m"
            "${AUV3_LOCAL}/audiounitconfig.h"
        )

        set(auv3_ui_resources
            "${AUV3_SHARED}/instrument/Main.storyboard"
        )

        smtg_add_auv3_app(${PLUGIN_NAME}_AUV3
            "macOS"
            "${PLUGIN_NAME} AUv3"
            "com.krateaudio.gradus.auv3"
            "${AUV3_LOCAL}/audiounitconfig.h"
            "${AUV3_LOCAL}/macOS/Gradus.entitlements"
            "${auv3_app_sources}"
            "${auv3_ui_resources}"
            "${AUV3_SHARED}/macOS/Info.plist"
            "${AUV3_SHARED}/Shared/Info.plist"
            ${PLUGIN_NAME}
        )

        if(TARGET auv3_wrapper_macos AND AUV3_EXTENSION_TARGET)
            add_dependencies(${AUV3_EXTENSION_TARGET} auv3_wrapper_macos)
        endif()

        if(AUV3_APP_TARGET)
            target_include_directories(${AUV3_APP_TARGET} PRIVATE
                "${AUV3_SHARED}/macOS/Sources"
                "${vst3sdk_SOURCE_DIR}"
            )
        endif()

        message(STATUS "[GRADUS] AudioUnit v3 target: ${PLUGIN_NAME}_AUV3")
    elseif(SMTG_ENABLE_AUV3_BUILDS AND NOT XCODE)
        message(WARNING "[GRADUS] AUv3 builds require Xcode generator (-G Xcode)")
    endif()

elseif(SMTG_WIN)
    target_sources(${PLUGIN_NAME}
        PRIVATE
            resources/win32resource.rc
    )
endif()

# ==============================================================================
# VSTGUI Resources
# ==============================================================================
smtg_target_add_plugin_resources(${PLUGIN_NAME}
    RESOURCES
        resources/editor.uidesc
)

# ==============================================================================
# Post-Build: Copy to VST3 System Folder
# ==============================================================================
option(VSTWORK_COPY_TO_VST3_FOLDER "Copy plugin to system VST3 folder after build" ON)

if(VSTWORK_COPY_TO_VST3_FOLDER AND SMTG_WIN)
    set(VST3_SYSTEM_FOLDER "$ENV{LOCALAPPDATA}/Programs/Common/VST3")
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
# Compiler Warnings
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
# Plugin Tests
# ==============================================================================
if(VSTWORK_BUILD_TESTS)
    add_subdirectory(tests)
endif()
