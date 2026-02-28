# ==============================================================================
# Iterum VST3 Plugin
# ==============================================================================
# Constitution Principle VII: Modern CMake with target-based configuration
# Links against shared KrateDSP library

# ==============================================================================
# Version from version.json (Single Source of Truth)
# ==============================================================================
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/version.json" VERSION_JSON)
string(JSON ITERUM_VERSION GET ${VERSION_JSON} "version")
string(JSON ITERUM_NAME GET ${VERSION_JSON} "name")
string(JSON ITERUM_DESCRIPTION GET ${VERSION_JSON} "description")
string(JSON ITERUM_PUBLISHER GET ${VERSION_JSON} "publisher")
string(JSON ITERUM_URL GET ${VERSION_JSON} "url")
string(JSON ITERUM_COPYRIGHT GET ${VERSION_JSON} "copyright")

# Parse version string (e.g., "0.9.4") into components for configure_file
string(REPLACE "." ";" VERSION_LIST ${ITERUM_VERSION})
list(GET VERSION_LIST 0 PROJECT_VERSION_MAJOR)
list(GET VERSION_LIST 1 PROJECT_VERSION_MINOR)
list(GET VERSION_LIST 2 PROJECT_VERSION_PATCH)

message(STATUS "[ITERUM] Version: ${ITERUM_VERSION} (${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH})")

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
set(PLUGIN_NAME "${ITERUM_NAME}")

smtg_add_vst3plugin(${PLUGIN_NAME}
    # Plugin Entry
    src/entry.cpp

    # Processor (Audio Thread)
    src/processor/processor.h
    src/processor/processor.cpp

    # Controller (UI Thread)
    src/controller/controller.h
    src/controller/controller.cpp

    # VST3 SDK Common Sources (for MemoryStream used by preset saving)
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp

    # Shared
    src/plugin_ids.h
    src/version.h
    src/delay_mode.h

    # Parameters
    src/parameters/bbd_params.h
    src/parameters/digital_params.h
    src/parameters/dropdown_mappings.h
    src/parameters/freeze_params.h
    src/parameters/granular_params.h
    src/parameters/multitap_params.h
    src/parameters/note_value_ui.h
    src/parameters/pingpong_params.h
    src/parameters/reverse_params.h
    src/parameters/shimmer_params.h
    src/parameters/spectral_params.h
    src/parameters/tape_params.h

    # Preset Configuration (adapter to shared library)
    src/preset/iterum_preset_config.h

    # Custom VSTGUI Views
    src/ui/tap_pattern_editor.h
    src/ui/tap_pattern_editor.cpp
    src/ui/tap_pattern_editor_logic.h
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
        BUNDLE_IDENTIFIER "com.krateaudio.iterum"
        COMPANY_NAME "Krate Audio"
    )

    # ==========================================================================
    # Audio Unit v2 Support (macOS only)
    # ==========================================================================
    option(SMTG_ENABLE_AUV2_BUILDS "Enable AudioUnit v2 builds" OFF)

    if(XCODE AND SMTG_ENABLE_AUV2_BUILDS)
        message(STATUS "[ITERUM] AudioUnit v2 build enabled")

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
            BUNDLE_IDENTIFIER com.krateaudio.iterum.audiounit
            INFO_PLIST_TEMPLATE ${CMAKE_CURRENT_SOURCE_DIR}/resources/au-info.plist
            VST3_PLUGIN_TARGET ${PLUGIN_NAME}
        )

        message(STATUS "[ITERUM] AudioUnit target: ${PLUGIN_NAME}_AU")
    elseif(SMTG_ENABLE_AUV2_BUILDS AND NOT XCODE)
        message(WARNING "[ITERUM] AUv2 builds require Xcode generator (-G Xcode)")
    endif()

    # ==========================================================================
    # Audio Unit v3 Support (macOS + Xcode only)
    # ==========================================================================
    option(SMTG_ENABLE_AUV3_BUILDS "Enable AudioUnit v3 builds" OFF)

    if(XCODE AND SMTG_ENABLE_AUV3_BUILDS)
        message(STATUS "[ITERUM] AudioUnit v3 build enabled")

        list(APPEND CMAKE_MODULE_PATH "${vst3sdk_SOURCE_DIR}/cmake/modules")
        include(SMTG_AddVST3AuV3)

        set(AUV3_SHARED "${CMAKE_SOURCE_DIR}/plugins/shared/resources/auv3")
        set(AUV3_LOCAL  "${CMAKE_CURRENT_SOURCE_DIR}/resources/auv3")

        set(auv3_app_sources
            "${AUV3_SHARED}/macOS/Sources/AppDelegate.h"
            "${AUV3_SHARED}/macOS/Sources/AppDelegate.m"
            "${AUV3_SHARED}/macOS/Sources/ViewController.h"
            "${AUV3_SHARED}/effect/ViewController.m"
            "${AUV3_LOCAL}/audiounitconfig.h"
        )

        set(auv3_ui_resources
            "${AUV3_SHARED}/effect/Main.storyboard"
            "${AUV3_SHARED}/Shared/drumLoop.wav"
        )

        smtg_add_auv3_app(${PLUGIN_NAME}_AUV3
            "macOS"
            "${PLUGIN_NAME} AUv3"
            "com.krateaudio.iterum.auv3"
            "${AUV3_LOCAL}/audiounitconfig.h"
            "${AUV3_LOCAL}/macOS/Iterum.entitlements"
            "${auv3_app_sources}"
            "${auv3_ui_resources}"
            "${AUV3_SHARED}/macOS/Info.plist"
            "${AUV3_SHARED}/Shared/Info.plist"
            ${PLUGIN_NAME}
        )

        message(STATUS "[ITERUM] AudioUnit v3 target: ${PLUGIN_NAME}_AUV3")
    elseif(SMTG_ENABLE_AUV3_BUILDS AND NOT XCODE)
        message(WARNING "[ITERUM] AUv3 builds require Xcode generator (-G Xcode)")
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
# Plugin Tests
# ==============================================================================
if(VSTWORK_BUILD_TESTS)
    add_subdirectory(tests)
endif()
