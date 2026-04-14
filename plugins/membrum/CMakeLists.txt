# ==============================================================================
# Membrum VST3 Plugin -- Synthesized Drum Machine
# ==============================================================================

# ==============================================================================
# Version from version.json (Single Source of Truth)
# ==============================================================================
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/version.json" VERSION_JSON)
string(JSON MEMBRUM_VERSION GET ${VERSION_JSON} "version")
string(JSON MEMBRUM_NAME GET ${VERSION_JSON} "name")
string(JSON MEMBRUM_DESCRIPTION GET ${VERSION_JSON} "description")
string(JSON MEMBRUM_PUBLISHER GET ${VERSION_JSON} "publisher")
string(JSON MEMBRUM_URL GET ${VERSION_JSON} "url")
string(JSON MEMBRUM_COPYRIGHT GET ${VERSION_JSON} "copyright")

# Parse version string (e.g., "0.1.0") into components for configure_file
string(REPLACE "." ";" VERSION_LIST ${MEMBRUM_VERSION})
list(GET VERSION_LIST 0 PROJECT_VERSION_MAJOR)
list(GET VERSION_LIST 1 PROJECT_VERSION_MINOR)
list(GET VERSION_LIST 2 PROJECT_VERSION_PATCH)

message(STATUS "[MEMBRUM] Version: ${MEMBRUM_VERSION} (${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH})")

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

    # DSP (Plugin-local)
    src/dsp/drum_voice.h
    src/dsp/membrane_modes.h

    # Phase 2: shared DSP types
    src/dsp/exciter_type.h
    src/dsp/body_model_type.h
    src/dsp/voice_common_params.h
    src/dsp/exciter_bank.h
    src/dsp/body_bank.h
    src/dsp/tone_shaper.h

    # Phase 4: per-pad configuration and presets
    src/dsp/pad_config.h
    src/dsp/default_kit.h
    src/preset/membrum_preset_config.h

    # Shared state / preset codec (one on-wire format for state + kit presets)
    src/state/state_codec.h
    src/state/state_codec.cpp

    # Phase 5: cross-pad coupling (sympathetic resonance)
    src/dsp/pad_category.h
    src/dsp/coupling_matrix.h

    # Phase 6: macros, custom editor, publishers, meters block
    src/processor/macro_mapper.cpp
    src/processor/meters_block.h
    src/dsp/pad_glow_publisher.h
    src/dsp/matrix_activity_publisher.h
    src/ui/pad_grid_view.h
    src/ui/pad_grid_view.cpp
    src/ui/coupling_matrix_view.h
    src/ui/coupling_matrix_view.cpp
    src/ui/kit_meters_view.h
    src/ui/kit_meters_view.cpp
    src/ui/membrum_editor_controller.cpp

    # Phase 2: exciter backends (header-only)
    src/dsp/exciters/impulse_exciter.h
    src/dsp/exciters/mallet_exciter.h
    src/dsp/exciters/noise_burst_exciter.h
    src/dsp/exciters/friction_exciter.h
    src/dsp/exciters/fm_impulse_exciter.h
    src/dsp/exciters/feedback_exciter.h

    # Phase 2: body backends (header-only)
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

    # Phase 2: unnatural zone modules (header-only)
    src/dsp/unnatural/unnatural_zone.h
    src/dsp/unnatural/material_morph.h
    src/dsp/unnatural/mode_inject.h
    src/dsp/unnatural/nonlinear_coupling.h

    # Phase 3: voice pool (multi-voice polyphony, stealing, choke groups)
    src/voice_pool/voice_pool.h
    src/voice_pool/voice_pool.cpp
    src/voice_pool/voice_meta.h
    src/voice_pool/voice_stealing_policy.h
    src/voice_pool/choke_group_table.h

    # Shared
    src/plugin_ids.h
    src/version.h

    # SDK helper: MemoryStream (needed by controller kit preset StateProvider)
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp
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
if(SMTG_MAC)
    smtg_target_set_bundle(${PLUGIN_NAME}
        BUNDLE_IDENTIFIER "com.krateaudio.membrum"
        COMPANY_NAME "Krate Audio"
    )

    # ==========================================================================
    # Audio Unit v2 Support (macOS only)
    # ==========================================================================
    option(SMTG_ENABLE_AUV2_BUILDS "Enable AudioUnit v2 builds" OFF)

    if(XCODE AND SMTG_ENABLE_AUV2_BUILDS)
        message(STATUS "[MEMBRUM] AudioUnit v2 build enabled")

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
            BUNDLE_IDENTIFIER com.krateaudio.membrum.audiounit
            INFO_PLIST_TEMPLATE ${CMAKE_CURRENT_SOURCE_DIR}/resources/au-info.plist
            VST3_PLUGIN_TARGET ${PLUGIN_NAME}
        )

        message(STATUS "[MEMBRUM] AudioUnit target: ${PLUGIN_NAME}_AU")
    elseif(SMTG_ENABLE_AUV2_BUILDS AND NOT XCODE)
        message(WARNING "[MEMBRUM] AUv2 builds require Xcode generator (-G Xcode)")
    endif()

    # ==========================================================================
    # Audio Unit v3 Support (macOS + Xcode only)
    # ==========================================================================
    option(SMTG_ENABLE_AUV3_BUILDS "Enable AudioUnit v3 builds" OFF)

    if(XCODE AND SMTG_ENABLE_AUV3_BUILDS)
        message(STATUS "[MEMBRUM] AudioUnit v3 build enabled")

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
            "com.krateaudio.membrum.auv3"
            "${AUV3_LOCAL}/audiounitconfig.h"
            "${AUV3_LOCAL}/macOS/Membrum.entitlements"
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

        message(STATUS "[MEMBRUM] AudioUnit v3 target: ${PLUGIN_NAME}_AUV3")
    elseif(SMTG_ENABLE_AUV3_BUILDS AND NOT XCODE)
        message(WARNING "[MEMBRUM] AUv3 builds require Xcode generator (-G Xcode)")
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
