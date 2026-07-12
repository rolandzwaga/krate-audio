# ==============================================================================
# KratePlugin.cmake — Shared CMake helpers for Krate Audio VST3 plugins
# ==============================================================================
# Consolidates boilerplate that was copy-pasted (with only a name/prefix change)
# across every plugins/<name>/CMakeLists.txt:
#
#   krate_plugin_read_version(PREFIX)            — parse version.json
#   krate_plugin_configure_generated_files()     — version.h / .rc / AU config
#   krate_plugin_platform_setup(target ...)      — macOS AU targets + win32 .rc
#   krate_plugin_install_to_system(target)       — POST_BUILD copy to system VST3
#   krate_plugin_set_warnings(target)            — MSVC/GCC warning posture
#
# What is deliberately LEFT in each plugin's CMakeLists (legitimate divergence):
#   - the smtg_add_vst3plugin() source list and FUIDs
#   - target_link_libraries / target_include_directories
#   - plugin-local extra targets (e.g. membrum_dsp, ruinae factory-preset install)
#   - resource globs (e.g. disrumpo bitmaps)
#
# Include once from the root CMakeLists.txt (before add_subdirectory(plugins/*));
# functions/macros defined here are then visible to every plugin subdirectory.
# ==============================================================================

# ------------------------------------------------------------------------------
# krate_plugin_read_version(PREFIX)
# ------------------------------------------------------------------------------
# Reads ${CMAKE_CURRENT_SOURCE_DIR}/version.json and sets, in the CALLER's scope
# (this is a macro), the metadata variables consumed by the generated headers:
#   ${PREFIX}_VERSION / _NAME / _DESCRIPTION / _PUBLISHER / _URL / _COPYRIGHT
# plus PROJECT_VERSION_MAJOR / _MINOR / _PATCH (used by configure_file + the SDK).
#
# PREFIX (e.g. ITERUM, GRADUS, MEMBRUM) selects the version.json metadata; the
# values are mirrored into neutral KRATE_PLUGIN_* vars consumed by the shared
# cmake/version.h.in and cmake/win32resource.rc.in templates.
# ------------------------------------------------------------------------------
macro(krate_plugin_read_version PREFIX)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/version.json" VERSION_JSON)
    string(JSON ${PREFIX}_VERSION     GET ${VERSION_JSON} "version")
    string(JSON ${PREFIX}_NAME        GET ${VERSION_JSON} "name")
    string(JSON ${PREFIX}_DESCRIPTION GET ${VERSION_JSON} "description")
    string(JSON ${PREFIX}_PUBLISHER   GET ${VERSION_JSON} "publisher")
    string(JSON ${PREFIX}_URL         GET ${VERSION_JSON} "url")
    string(JSON ${PREFIX}_COPYRIGHT   GET ${VERSION_JSON} "copyright")

    # Parse version string (e.g. "1.0.0") into components for configure_file
    string(REPLACE "." ";" VERSION_LIST ${${PREFIX}_VERSION})
    list(GET VERSION_LIST 0 PROJECT_VERSION_MAJOR)
    list(GET VERSION_LIST 1 PROJECT_VERSION_MINOR)
    list(GET VERSION_LIST 2 PROJECT_VERSION_PATCH)

    # Neutral mirrors consumed by the shared cmake/version.h.in and
    # cmake/win32resource.rc.in templates (which use @KRATE_PLUGIN_*@ tokens so a
    # single template serves every plugin).
    set(KRATE_PLUGIN_NAME        "${${PREFIX}_NAME}")
    set(KRATE_PLUGIN_DESCRIPTION "${${PREFIX}_DESCRIPTION}")
    set(KRATE_PLUGIN_PUBLISHER   "${${PREFIX}_PUBLISHER}")
    set(KRATE_PLUGIN_URL         "${${PREFIX}_URL}")
    set(KRATE_PLUGIN_COPYRIGHT   "${${PREFIX}_COPYRIGHT}")

    # Default UI display version: `"<Name> v" VERSION_STR`. A plugin may override
    # KRATE_PLUGIN_UI_VERSION before krate_plugin_configure_generated_files();
    # Iterum uses the bare `"v" VERSION_STR` form. VERSION_STR is left literal for
    # the C preprocessor (configure_file runs with @ONLY).
    set(KRATE_PLUGIN_UI_VERSION "\"${${PREFIX}_NAME} v\" VERSION_STR")

    message(STATUS "[${PREFIX}] Version: ${${PREFIX}_VERSION} (${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH})")
endmacro()

# ------------------------------------------------------------------------------
# krate_plugin_configure_generated_files()
# ------------------------------------------------------------------------------
# Generates:
#   src/version.h                      (from shared cmake/version.h.in)
#   resources/win32resource.rc         (from shared cmake/win32resource.rc.in)
#   resources/auv3/audiounitconfig.h   (kAUcomponentVersion synced to version.json)
#
# Must be called AFTER krate_plugin_read_version() so the @KRATE_PLUGIN_*@ and
# @PROJECT_VERSION_*@ tokens resolve. This is a macro so those caller-scope
# variables are visible to configure_file().
# ------------------------------------------------------------------------------
macro(krate_plugin_configure_generated_files)
    # Single shared templates (cmake/version.h.in, cmake/win32resource.rc.in) with
    # neutral @KRATE_PLUGIN_*@ tokens, generated into each plugin's source tree.
    configure_file(
        "${CMAKE_SOURCE_DIR}/cmake/version.h.in"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/version.h"
        @ONLY
    )

    configure_file(
        "${CMAKE_SOURCE_DIR}/cmake/win32resource.rc.in"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/win32resource.rc"
        @ONLY
    )

    # Hex-style AU version: (major << 16) | (minor << 8) | patch. Used by macOS
    # hosts for AU plugin caching. CMake's math() evaluates the shift in decimal.
    math(EXPR AU_COMPONENT_VERSION
        "(${PROJECT_VERSION_MAJOR} << 16) | (${PROJECT_VERSION_MINOR} << 8) | ${PROJECT_VERSION_PATCH}")
    set(AU_VERSION_COMMENT
        "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH} = (${PROJECT_VERSION_MAJOR} << 16) | (${PROJECT_VERSION_MINOR} << 8) | ${PROJECT_VERSION_PATCH} = ${AU_COMPONENT_VERSION}")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/auv3/audiounitconfig.h.in"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/auv3/audiounitconfig.h"
        @ONLY
    )
endmacro()

# ------------------------------------------------------------------------------
# krate_plugin_platform_setup(target
#     TAG          <UPPERCASE_PREFIX>          # for [TAG] status messages
#     BUNDLE_BASE  <com.krateaudio.name>       # reverse-DNS bundle id base
#     ENTITLEMENTS <Name.entitlements>         # file under resources/auv3/macOS/
#     KIND         <instrument|effect>)        # selects AUv3 storyboard/sources
# ------------------------------------------------------------------------------
# Encapsulates the per-plugin platform block:
#   - macOS: bundle identity + AudioUnit v2 (Xcode) + AudioUnit v3 (Xcode) targets
#   - Windows: attach the generated win32resource.rc to the plugin target
#
# KIND chooses the shared AUv3 scaffolding:
#   instrument -> instrument/ViewController.m + instrument/Main.storyboard
#   effect     -> effect/ViewController.m     + effect/Main.storyboard + drumLoop.wav
#
# NOTE: the macOS AU branch cannot be exercised on the Windows-primary CI; verify
# with an Xcode build before relying on changes here.
# ------------------------------------------------------------------------------
function(krate_plugin_platform_setup target)
    set(oneValueArgs TAG BUNDLE_BASE ENTITLEMENTS KIND)
    cmake_parse_arguments(KP "" "${oneValueArgs}" "" ${ARGN})

    if(NOT KP_KIND STREQUAL "instrument" AND NOT KP_KIND STREQUAL "effect")
        message(FATAL_ERROR "krate_plugin_platform_setup: KIND must be 'instrument' or 'effect' (got '${KP_KIND}')")
    endif()

    if(SMTG_MAC)
        smtg_target_set_bundle(${target}
            BUNDLE_IDENTIFIER "${KP_BUNDLE_BASE}"
            COMPANY_NAME "Krate Audio"
        )

        # ----------------------------------------------------------------------
        # Audio Unit v2 Support (macOS only)
        # ----------------------------------------------------------------------
        option(SMTG_ENABLE_AUV2_BUILDS "Enable AudioUnit v2 builds" OFF)

        if(XCODE AND SMTG_ENABLE_AUV2_BUILDS)
            message(STATUS "[${KP_TAG}] AudioUnit v2 build enabled")

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

            smtg_target_add_auv2(${target}_AU
                BUNDLE_NAME ${target}
                BUNDLE_IDENTIFIER ${KP_BUNDLE_BASE}.audiounit
                INFO_PLIST_TEMPLATE ${CMAKE_CURRENT_SOURCE_DIR}/resources/au-info.plist
                VST3_PLUGIN_TARGET ${target}
            )

            message(STATUS "[${KP_TAG}] AudioUnit target: ${target}_AU")
        elseif(SMTG_ENABLE_AUV2_BUILDS AND NOT XCODE)
            message(WARNING "[${KP_TAG}] AUv2 builds require Xcode generator (-G Xcode)")
        endif()

        # ----------------------------------------------------------------------
        # Audio Unit v3 Support (macOS + Xcode only)
        # ----------------------------------------------------------------------
        option(SMTG_ENABLE_AUV3_BUILDS "Enable AudioUnit v3 builds" OFF)

        if(XCODE AND SMTG_ENABLE_AUV3_BUILDS)
            message(STATUS "[${KP_TAG}] AudioUnit v3 build enabled")

            list(APPEND CMAKE_MODULE_PATH "${vst3sdk_SOURCE_DIR}/cmake/modules")
            include(SMTG_AddVST3AuV3)

            set(AUV3_SHARED "${CMAKE_SOURCE_DIR}/plugins/shared/resources/auv3")
            set(AUV3_LOCAL  "${CMAKE_CURRENT_SOURCE_DIR}/resources/auv3")

            set(auv3_app_sources
                "${AUV3_SHARED}/macOS/Sources/AppDelegate.h"
                "${AUV3_SHARED}/macOS/Sources/AppDelegate.m"
                "${AUV3_SHARED}/macOS/Sources/ViewController.h"
                "${AUV3_SHARED}/${KP_KIND}/ViewController.m"
                "${AUV3_LOCAL}/audiounitconfig.h"
            )

            if(KP_KIND STREQUAL "effect")
                set(auv3_ui_resources
                    "${AUV3_SHARED}/effect/Main.storyboard"
                    "${AUV3_SHARED}/Shared/drumLoop.wav"
                )
            else()
                set(auv3_ui_resources
                    "${AUV3_SHARED}/instrument/Main.storyboard"
                )
            endif()

            smtg_add_auv3_app(${target}_AUV3
                "macOS"
                "${target} AUv3"
                "${KP_BUNDLE_BASE}.auv3"
                "${AUV3_LOCAL}/audiounitconfig.h"
                "${AUV3_LOCAL}/macOS/${KP_ENTITLEMENTS}"
                "${auv3_app_sources}"
                "${auv3_ui_resources}"
                "${AUV3_SHARED}/macOS/Info.plist"
                "${AUV3_SHARED}/Shared/Info.plist"
                ${target}
            )

            # Xcode generator doesn't resolve target_link_libraries into explicit
            # build dependencies, causing "library not found" with parallel builds.
            if(TARGET auv3_wrapper_macos AND AUV3_EXTENSION_TARGET)
                add_dependencies(${AUV3_EXTENSION_TARGET} auv3_wrapper_macos)
            endif()

            # ViewController.m imports ViewController.h from a sibling directory.
            # Also need SDK root for public.sdk/source/vst/auv3wrapper includes.
            if(AUV3_APP_TARGET)
                target_include_directories(${AUV3_APP_TARGET} PRIVATE
                    "${AUV3_SHARED}/macOS/Sources"
                    "${vst3sdk_SOURCE_DIR}"
                )
            endif()

            message(STATUS "[${KP_TAG}] AudioUnit v3 target: ${target}_AUV3")
        elseif(SMTG_ENABLE_AUV3_BUILDS AND NOT XCODE)
            message(WARNING "[${KP_TAG}] AUv3 builds require Xcode generator (-G Xcode)")
        endif()

    elseif(SMTG_WIN)
        target_sources(${target}
            PRIVATE
                resources/win32resource.rc
        )
    endif()
endfunction()

# ------------------------------------------------------------------------------
# krate_plugin_install_to_system(target)
# ------------------------------------------------------------------------------
# POST_BUILD: copy the built .vst3 bundle into the per-user system VST3 folder so
# it is picked up by locally installed hosts. Windows only; no-op elsewhere.
# ------------------------------------------------------------------------------
function(krate_plugin_install_to_system target)
    option(VSTWORK_COPY_TO_VST3_FOLDER "Copy plugin to system VST3 folder after build" ON)

    if(VSTWORK_COPY_TO_VST3_FOLDER AND SMTG_WIN)
        set(VST3_SYSTEM_FOLDER "$ENV{LOCALAPPDATA}/Programs/Common/VST3")
        set(VST3_BUILD_OUTPUT "${CMAKE_BINARY_DIR}/VST3/$<CONFIG>/${target}.vst3")

        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E echo "[VSTWORK] Copying plugin to ${VST3_SYSTEM_FOLDER}..."
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${VST3_SYSTEM_FOLDER}/${target}.vst3"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${VST3_BUILD_OUTPUT}"
                "${VST3_SYSTEM_FOLDER}/${target}.vst3"
            COMMAND ${CMAKE_COMMAND} -E echo "[VSTWORK] Plugin installed to ${VST3_SYSTEM_FOLDER}/${target}.vst3"
            COMMENT "Installing ${target}.vst3 to system VST3 folder"
            VERBATIM
        )
    endif()
endfunction()

# ------------------------------------------------------------------------------
# krate_plugin_install_presets(target [SRC_SUBDIR <dir>] [DEST_SUBDIR <dir>])
# ------------------------------------------------------------------------------
# POST_BUILD: copy the plugin's generated factory presets from resources/presets/
# into the shared runtime factory dir (%PROGRAMDATA%\Krate Audio\<target>[\<DEST_SUBDIR>]),
# matching Krate::Shared getFactoryPresetDirectory(). Windows only; no-op elsewhere.
#
# Most plugins keep category folders directly under resources/presets/ and install to
# .../Krate Audio/<target> — call with no options. Membrum nests presets under
# "resources/presets/Kit Presets" and the runtime scans a "Kits" subdir, so it passes
# SRC_SUBDIR "Kit Presets" DEST_SUBDIR "Kits".
# ------------------------------------------------------------------------------
function(krate_plugin_install_presets target)
    cmake_parse_arguments(KPIP "" "SRC_SUBDIR;DEST_SUBDIR" "" ${ARGN})

    if(NOT SMTG_WIN)
        return()
    endif()

    set(_src "${CMAKE_CURRENT_SOURCE_DIR}/resources/presets")
    if(KPIP_SRC_SUBDIR)
        set(_src "${_src}/${KPIP_SRC_SUBDIR}")
    endif()

    set(_dest "$ENV{PROGRAMDATA}/Krate Audio/${target}")
    if(KPIP_DEST_SUBDIR)
        set(_dest "${_dest}/${KPIP_DEST_SUBDIR}")
    endif()

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "[VSTWORK] Installing ${target} factory presets..."
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${_src}" "${_dest}"
        COMMAND ${CMAKE_COMMAND} -E echo "[VSTWORK] Factory presets installed to ${_dest}"
        COMMENT "Installing ${target} factory presets to system directory"
        VERBATIM
    )
endfunction()

# ------------------------------------------------------------------------------
# krate_plugin_set_warnings(target)
# ------------------------------------------------------------------------------
# Apply the project-standard warning posture (Constitution Principle III).
# MSVC: /W4 /permissive- ; GCC/Clang: -Wall -Wextra -Wpedantic.
# ------------------------------------------------------------------------------
function(krate_plugin_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /permissive-
            /Zc:__cplusplus
            /wd4100
            /wd4458
        )
        if(CMAKE_CXX_COMPILER_LAUNCHER OR CMAKE_C_COMPILER_LAUNCHER)
            target_compile_options(${target} PRIVATE /Z7)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wno-unused-parameter
        )
    endif()
endfunction()
