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

    # The VST3 SDK stamps PROJECT_VERSION (not the components) into the bundle's
    # moduleinfo.json "Version" (SMTG_AddVST3Library.cmake) and the macOS
    # Info.plist CFBundleShortVersionString (SMTG_Bundle.cmake). Plugins are
    # add_subdirectory()'d without their own project() call, so without this
    # override they inherit the root project's version (every bundle shipped
    # "0.8.0" while its classes said e.g. 0.12.2.1). tools/check-bundle.js gates it.
    set(PROJECT_VERSION "${${PREFIX}_VERSION}")

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
#   resources/au-info.plist            (only when resources/au-info.plist.in exists; AUv2 version)
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

    # AUv2 Info.plist carrying the plugin version (CFBundleVersion, CFBundleShortVersionString and the
    # AudioComponents version = kAUcomponentVersion). Every plugin has resources/au-info.plist.in; it gets
    # resources/au-info.plist generated here (gitignored, never committed) and krate_plugin_platform_setup passes
    # that file to smtg_target_add_auv2; the SDK does not stamp the version into the AUv2 plist itself (Ruinae audit
    # R-191). Check (every plugin, CI and the commit gate): tools/check-au-plist.js.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/au-info.plist.in")
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/au-info.plist.in"
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/au-info.plist"
            @ONLY
        )
    endif()
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
#   krate_check_audiounit_sdk_standard(sdk_dir cxx_std)
#   Fails configure when the fetched AudioUnitSDK's Xcode project needs a newer C++ standard than cxx_std, the
#   standard the VST3 SDK's AU wrapper compiles at. Without it a bad pin surfaces minutes into the macOS build as
#   dozens of std::expected errors in *_AU (PR #293; 89f7b1c3). An unreadable project or an unknown standard
#   token fails too, so the guard can never pass vacuously. Self-test: tools/test-audiounit-sdk-guard.cmake.
function(krate_check_audiounit_sdk_standard sdk_dir cxx_std)
    set(_pbx "${sdk_dir}/AudioUnitSDK.xcodeproj/project.pbxproj")
    if(NOT EXISTS "${_pbx}")
        message(FATAL_ERROR "[KRATE] AudioUnitSDK guard: ${_pbx} not found; cannot read the SDK's C++ standard")
    endif()
    file(STRINGS "${_pbx}" _lines REGEX "CLANG_CXX_LANGUAGE_STANDARD")
    if(NOT _lines)
        message(FATAL_ERROR "[KRATE] AudioUnitSDK guard: no CLANG_CXX_LANGUAGE_STANDARD in ${_pbx}")
    endif()
    foreach(_line IN LISTS _lines)
        if(NOT _line MATCHES "\\+\\+([0-9][0-9a-z])\"")
            message(FATAL_ERROR "[KRATE] AudioUnitSDK guard: unreadable standard in '${_line}'")
        endif()
        set(_tok "${CMAKE_MATCH_1}")
        set(_map_1z 17)
        set(_map_2a 20)
        set(_map_2b 23)
        set(_map_2c 26)
        if(DEFINED _map_${_tok})
            set(_std "${_map_${_tok}}")
        elseif(_tok MATCHES "^[0-9][0-9]$")
            set(_std "${_tok}")
        else()
            message(FATAL_ERROR "[KRATE] AudioUnitSDK guard: unknown standard token '${_tok}' in '${_line}'")
        endif()
        if(_std LESS 50)  # c++98 sorts after c++23 numerically; 98 and 03 are older than any cxx_std we use
            if(_std GREATER cxx_std)
                message(FATAL_ERROR "[KRATE] AudioUnitSDK at ${sdk_dir} needs C++${_std} (${_line}), but the "
                    "VST3 SDK AU wrapper compiles as C++${cxx_std}: every *_AU target would fail "
                    "(std::expected, PR #293). Pin an AudioUnitSDK release built at C++${cxx_std} or older.")
            endif()
        endif()
    endforeach()
endfunction()

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

        # Release (DEPLOYMENT_POSTPROCESSING = YES, SMTG_AddSMTGLibrary.cmake) would run Xcode's Strip on the binary
        # after the post-build phase, where the SDK's "Adhoc codesign workaround for Xcode 15"
        # (SMTG_AddVST3Library.cmake) signed it so moduleinfotool could load it; Strip then warns, per architecture,
        # that it invalidates that signature. So Xcode's Strip is off for plugin targets and the post-build strips
        # itself, as its last steps (after moduleinfo; this function runs after smtg_add_vst3plugin, and the later
        # krate_plugin_install_* steps are Windows-only): drop the signature, strip with Xcode's own flags
        # (`strip -D -x -T`, from the CI log), re-sign ad hoc. Every run of the phase ENDS SIGNED. The previous
        # version only dropped the signature and relied on Xcode's CodeSign after Strip; CI's separate AU pass re-runs
        # this phase with no Strip or CodeSign after it, so Iterum.vst3 shipped unsigned and arm64 refused to load it
        # ("Invalid Module!", validator, macOS CI 2026-11-15). Debug: no strip, `true` leaves the signature alone.
        # The path is the SDK's own (SMTG_PLUGIN_PACKAGE_PATH, the .vst3 bundle its workaround signs), never
        # $<TARGET_FILE>: that names <target>.bundle/Contents/MacOS/<target>, which does not exist because the SDK
        # renames the bundle to .vst3 (macOS CI 2026-11-15: "Vorago.bundle/Contents/MacOS/Vorago: No such file").
        if(XCODE)
            get_target_property(_krate_package_path ${target} SMTG_PLUGIN_PACKAGE_PATH)
            set_target_properties(${target} PROPERTIES XCODE_ATTRIBUTE_STRIP_INSTALLED_PRODUCT NO)
            set(_krate_release_tool "$<IF:$<CONFIG:Release>,xcrun,true>")
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${_krate_release_tool} codesign --remove-signature "${_krate_package_path}"
                COMMAND ${_krate_release_tool} strip -D -x -T
                    "${_krate_package_path}/Contents/MacOS/$<TARGET_FILE_NAME:${target}>"
                COMMAND ${_krate_release_tool} codesign --force --sign - "${_krate_package_path}"
                COMMENT "[KRATE] Strip the unsigned binary, then re-sign ad hoc (Xcode's Strip is off for this target)"
            )
        endif()

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
                # PINNED: 1.3.0. Do not bump without building every *_AU target on macOS.
                # - 1.4.0+ uses C++23 std::expected in AUUtility.h / AUBuffer.h, but the VST3 SDK's AU wrapper
                #   (auwrapper.mm, aucocoaview.mm) compiles at CMAKE_CXX_STANDARD (20): every *_AU target fails
                #   with "no template named 'expected' in namespace 'std'". Bumped to 1.4.0 and broken twice
                #   (PR #293, then 89f7b1c3); krate_check_audiounit_sdk_standard below now fails configure instead.
                # - 1.1.0 / 1.2.0 lack ENABLE_USER_SCRIPT_SANDBOXING = YES: every AU build warned that the
                #   unsandboxed "Install clang-format hook" phase delays Copy Headers (CI warning gate).
                # 1.3.0 is the one release with sandboxing at C++20 (its project: CLANG_CXX_LANGUAGE_STANDARD c++20).
                GIT_TAG AudioUnitSDK-1.3.0
            )
            FetchContent_MakeAvailable(AudioUnitSDK)
            FetchContent_GetProperties(
                AudioUnitSDK
                SOURCE_DIR SMTG_AUDIOUNIT_SDK_PATH
            )
            krate_check_audiounit_sdk_standard("${SMTG_AUDIOUNIT_SDK_PATH}" "${CMAKE_CXX_STANDARD}")

            list(APPEND CMAKE_MODULE_PATH "${vst3sdk_SOURCE_DIR}/cmake/modules")
            include(SMTG_AddVST3AuV2)

            smtg_target_add_auv2(${target}_AU
                BUNDLE_NAME ${target}
                BUNDLE_IDENTIFIER ${KP_BUNDLE_BASE}.audiounit
                INFO_PLIST_TEMPLATE ${CMAKE_CURRENT_SOURCE_DIR}/resources/au-info.plist
                VST3_PLUGIN_TARGET ${target}
            )
            # The target compiles the SDK's auwrapper.mm, third-party source we cannot modify: C++ variable-length
            # arrays (clang -Wvla-cxx-extension). KRATE_CXX_HAS_WVLA_CXX_EXTENSION: root CMakeLists.txt.
            if(KRATE_CXX_HAS_WVLA_CXX_EXTENSION)
                target_compile_options(${target}_AU PRIVATE -Wno-vla-cxx-extension)
            endif()

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

            # effect/ViewController.m filters its open panel with UTType (allowedContentTypes); link the
            # framework explicitly rather than relying on Xcode's module auto-linking.
            if(AUV3_APP_TARGET AND KP_KIND STREQUAL "effect")
                target_link_libraries(${AUV3_APP_TARGET} PRIVATE "-framework UniformTypeIdentifiers")
            endif()

            # smtg_add_auv3_app never applies the SDK's universal-binary setting (the VST3 and AU targets
            # get it), so the container app and its extension shipped arm64-only beside a universal VST3:
            # E_FACTS_ARCH_MISMATCH in every macOS pkg (backfill-sign, 2026-11-02).
            foreach(auv3_target IN ITEMS ${AUV3_APP_TARGET} ${AUV3_EXTENSION_TARGET})
                smtg_target_setup_universal_binary(${auv3_target})
            endforeach()

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
#
# -DVSTWORK_INSTALL_PRESETS=OFF skips the copy. A build in a git worktree (a parallel
# fix lane) sets it together with -DVSTWORK_COPY_TO_VST3_FOLDER=OFF: several lanes
# building at once must not write the same system folders, and a lane's unreviewed
# presets must not replace the installed ones.
# ------------------------------------------------------------------------------
function(krate_plugin_install_presets target)
    cmake_parse_arguments(KPIP "" "SRC_SUBDIR;DEST_SUBDIR" "" ${ARGN})
    option(VSTWORK_INSTALL_PRESETS "Copy factory presets to the system preset folder after build" ON)

    if(NOT SMTG_WIN OR NOT VSTWORK_INSTALL_PRESETS)
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
