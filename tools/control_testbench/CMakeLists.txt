# ==============================================================================
# Control Testbench - Standalone UI Control Testing Application
# ==============================================================================
# A standalone executable for rapid iteration on custom VSTGUI controls
# without needing to build the full plugin or load a DAW.
#
# Usage:
#   cmake --build build --target control_testbench
#   ./build/bin/control_testbench[.exe]
# ==============================================================================

set(target control_testbench)

# ==============================================================================
# Source Files
# ==============================================================================
set(${target}_sources
    src/main.cpp
    src/testbench_delegate.h
    src/testbench_delegate.cpp
    src/control_registry.h
    src/control_registry.cpp
    src/parameter_logger.h
    src/parameter_logger.cpp
)

# ==============================================================================
# TapPatternEditor Sources (adapted for standalone)
# ==============================================================================
# We include the TapPatternEditor sources directly and provide standalone
# compatibility through mock headers.
set(${target}_control_sources
    ${CMAKE_SOURCE_DIR}/plugins/iterum/src/ui/tap_pattern_editor.h
    ${CMAKE_SOURCE_DIR}/plugins/iterum/src/ui/tap_pattern_editor.cpp
    ${CMAKE_SOURCE_DIR}/plugins/iterum/src/ui/tap_pattern_editor_logic.h
)

# ==============================================================================
# Create Executable
# ==============================================================================
if(SMTG_WIN)
    # Windows: Create a GUI application (no console window)
    add_executable(${target} WIN32
        ${${target}_sources}
        ${${target}_control_sources}
    )
    # Set the entry point for Windows
    if(MSVC_CXX_ARCHITECTURE_ID MATCHES "^(x86|X86)$")
        set_target_properties(${target} PROPERTIES LINK_FLAGS "/INCLUDE:_wWinMain@16")
    elseif(MSVC_CXX_ARCHITECTURE_ID MATCHES "ARM64EC")
        set_target_properties(${target} PROPERTIES LINK_FLAGS "/INCLUDE:#wWinMain")
    else()
        set_target_properties(${target} PROPERTIES LINK_FLAGS "/INCLUDE:wWinMain")
    endif()
elseif(SMTG_MAC)
    # macOS: Create an app bundle
    add_executable(${target} MACOSX_BUNDLE
        ${${target}_sources}
        ${${target}_control_sources}
    )
    set_target_properties(${target} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.krateaudio.controltestbench"
        MACOSX_BUNDLE_BUNDLE_NAME "Control Testbench"
        MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
    )
else()
    # Linux
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTKMM3 REQUIRED gtkmm-3.0)
    add_executable(${target}
        ${${target}_sources}
        ${${target}_control_sources}
    )
endif()

# ==============================================================================
# Include Directories
# ==============================================================================
target_include_directories(${target}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        # IMPORTANT: Mock headers must come BEFORE plugin src to override plugin_ids.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/mocks
        # Include plugin src for TapPatternEditor
        ${CMAKE_SOURCE_DIR}/plugins/iterum/src
        # Shared UI components (ArcKnob, etc.)
        ${CMAKE_SOURCE_DIR}/plugins/shared/src
        # VSTGUI includes
        ${vst3sdk_SOURCE_DIR}/vstgui4
)

# ==============================================================================
# Compile Definitions
# ==============================================================================
# App identifier for VSTGUI standalone
target_compile_definitions(${target} PRIVATE
    VSTGUI_STANDALONE_APP_URI="com.krateaudio.controltestbench"
)

# ==============================================================================
# Link Libraries
# ==============================================================================
target_link_libraries(${target}
    PRIVATE
        vstgui
        vstgui_uidescription
        vstgui_standalone
)

# Platform-specific libraries
if(SMTG_WIN)
    target_link_libraries(${target}
        PRIVATE
            Comctl32
            Shlwapi
    )
elseif(SMTG_MAC)
    find_library(COCOA_FRAMEWORK Cocoa)
    find_library(OPENGL_FRAMEWORK OpenGL)
    find_library(ACCELERATE_FRAMEWORK Accelerate)
    find_library(QUARTZCORE_FRAMEWORK QuartzCore)
    target_link_libraries(${target}
        PRIVATE
            ${COCOA_FRAMEWORK}
            ${OPENGL_FRAMEWORK}
            ${ACCELERATE_FRAMEWORK}
            ${QUARTZCORE_FRAMEWORK}
    )
elseif(LINUX)
    target_link_libraries(${target}
        PRIVATE
            ${GTKMM3_LIBRARIES}
    )
    target_include_directories(${target} PRIVATE ${GTKMM3_INCLUDE_DIRS})
endif()

# ==============================================================================
# Compiler Settings
# ==============================================================================
target_compile_features(${target} PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(${target} PRIVATE
        /W4
        /permissive-
        /Zc:__cplusplus
        /wd4100  # unreferenced formal parameter
        /wd4458  # declaration hides class member
    )
else()
    target_compile_options(${target} PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter
    )
endif()

# ==============================================================================
# Output Directory
# ==============================================================================
set_target_properties(${target} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/${target}"
)

# ==============================================================================
# Resources
# ==============================================================================
# Copy uidesc files to output directory
if(SMTG_MAC)
    # macOS: Resources go in the app bundle
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/testbench.uidesc"
        PROPERTIES MACOSX_PACKAGE_LOCATION "Resources"
    )
    target_sources(${target} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/testbench.uidesc"
    )
else()
    # Windows/Linux: Copy to Resources folder next to executable
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:${target}>/Resources"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/testbench.uidesc"
            "$<TARGET_FILE_DIR:${target}>/Resources/"
        COMMENT "Copying testbench resources"
    )
endif()
