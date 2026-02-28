# ==============================================================================
# Ruinae VST3 Plugin
# ==============================================================================
# Constitution Principle VII: Modern CMake with target-based configuration
# Links against shared KrateDSP library

# ==============================================================================
# Version from version.json (Single Source of Truth)
# ==============================================================================
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/version.json" VERSION_JSON)
string(JSON RUINAE_VERSION GET ${VERSION_JSON} "version")
string(JSON RUINAE_NAME GET ${VERSION_JSON} "name")
string(JSON RUINAE_DESCRIPTION GET ${VERSION_JSON} "description")
string(JSON RUINAE_PUBLISHER GET ${VERSION_JSON} "publisher")
string(JSON RUINAE_URL GET ${VERSION_JSON} "url")
string(JSON RUINAE_COPYRIGHT GET ${VERSION_JSON} "copyright")

# Parse version string (e.g., "0.1.0") into components for configure_file
string(REPLACE "." ";" VERSION_LIST ${RUINAE_VERSION})
list(GET VERSION_LIST 0 PROJECT_VERSION_MAJOR)
list(GET VERSION_LIST 1 PROJECT_VERSION_MINOR)
list(GET VERSION_LIST 2 PROJECT_VERSION_PATCH)

message(STATUS "[RUINAE] Version: ${RUINAE_VERSION} (${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH})")

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
set(PLUGIN_NAME "${RUINAE_NAME}")

smtg_add_vst3plugin(${PLUGIN_NAME}
    # Plugin Entry
    src/entry.cpp

    # Processor (Audio Thread)
    src/processor/processor.h
    src/processor/processor.cpp

    # Controller (UI Thread)
    src/controller/controller.h
    src/controller/controller.cpp
    src/controller/parameter_helpers.h

    # VST3 SDK Common Sources (for MemoryStream used by preset saving)
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp

    # Shared
    src/plugin_ids.h
    src/version.h

    # Parameter Packs (19 sections)
    src/parameters/dropdown_mappings.h
    src/parameters/note_value_ui.h
    src/parameters/global_params.h
    src/parameters/osc_a_params.h
    src/parameters/osc_b_params.h
    src/parameters/mixer_params.h
    src/parameters/filter_params.h
    src/parameters/distortion_params.h
    src/parameters/trance_gate_params.h
    src/parameters/amp_env_params.h
    src/parameters/filter_env_params.h
    src/parameters/mod_env_params.h
    src/parameters/lfo1_params.h
    src/parameters/lfo2_params.h
    src/parameters/chaos_mod_params.h
    src/parameters/mod_matrix_params.h
    src/parameters/global_filter_params.h
    src/parameters/delay_params.h
    src/parameters/reverb_params.h
    src/parameters/mono_mode_params.h

    # Preset Configuration (adapter to shared library)
    src/preset/ruinae_preset_config.h

    # Engine (plugin-specific composition)
    src/engine/ruinae_engine.h
    src/engine/ruinae_voice.h
    src/engine/ruinae_effects_chain.h
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
        BUNDLE_IDENTIFIER "com.krateaudio.ruinae"
        COMPANY_NAME "Krate Audio"
    )

    # ==========================================================================
    # Audio Unit v2 Support (macOS only)
    # ==========================================================================
    option(SMTG_ENABLE_AUV2_BUILDS "Enable AudioUnit v2 builds" OFF)

    if(XCODE AND SMTG_ENABLE_AUV2_BUILDS)
        message(STATUS "[RUINAE] AudioUnit v2 build enabled")

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
            BUNDLE_IDENTIFIER com.krateaudio.ruinae.audiounit
            INFO_PLIST_TEMPLATE ${CMAKE_CURRENT_SOURCE_DIR}/resources/au-info.plist
            VST3_PLUGIN_TARGET ${PLUGIN_NAME}
        )

        message(STATUS "[RUINAE] AudioUnit target: ${PLUGIN_NAME}_AU")
    elseif(SMTG_ENABLE_AUV2_BUILDS AND NOT XCODE)
        message(WARNING "[RUINAE] AUv2 builds require Xcode generator (-G Xcode)")
    endif()

    # ==========================================================================
    # Audio Unit v3 Support (macOS + Xcode only)
    # ==========================================================================
    option(SMTG_ENABLE_AUV3_BUILDS "Enable AudioUnit v3 builds" OFF)

    if(XCODE AND SMTG_ENABLE_AUV3_BUILDS)
        message(STATUS "[RUINAE] AudioUnit v3 build enabled")

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
            "com.krateaudio.ruinae.auv3"
            "${AUV3_LOCAL}/audiounitconfig.h"
            "${AUV3_LOCAL}/macOS/Ruinae.entitlements"
            "${auv3_app_sources}"
            "${auv3_ui_resources}"
            "${AUV3_SHARED}/macOS/Info.plist"
            "${AUV3_SHARED}/Shared/Info.plist"
            ${PLUGIN_NAME}
        )

        # Xcode generator doesn't resolve target_link_libraries into explicit
        # build dependencies, causing "library not found" with parallel builds.
        if(TARGET auv3_wrapper_macos AND AUV3_EXTENSION_TARGET)
            add_dependencies(${AUV3_EXTENSION_TARGET} auv3_wrapper_macos)
        endif()

        message(STATUS "[RUINAE] AudioUnit v3 target: ${PLUGIN_NAME}_AUV3")
    elseif(SMTG_ENABLE_AUV3_BUILDS AND NOT XCODE)
        message(WARNING "[RUINAE] AUv3 builds require Xcode generator (-G Xcode)")
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
# Post-Build: Install Factory Presets to ProgramData
# ==============================================================================
# Copies generated factory presets from resources/presets/ to the system
# factory preset directory so PresetManager can locate them at runtime.
# Presets are generated by the ruinae_preset_generator tool (see root CMakeLists.txt).
set(RUINAE_PRESETS_SRC "${CMAKE_CURRENT_SOURCE_DIR}/resources/presets")

if(SMTG_WIN)
    set(FACTORY_PRESET_DIR "$ENV{PROGRAMDATA}/Krate Audio/Ruinae")
    add_custom_command(TARGET ${PLUGIN_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "[VSTWORK] Installing Ruinae factory presets..."
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${RUINAE_PRESETS_SRC}"
            "${FACTORY_PRESET_DIR}"
        COMMAND ${CMAKE_COMMAND} -E echo "[VSTWORK] Factory presets installed to ${FACTORY_PRESET_DIR}"
        COMMENT "Installing Ruinae factory presets to system directory"
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
