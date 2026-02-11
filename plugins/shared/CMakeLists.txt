# ==============================================================================
# KratePluginsShared - Shared Plugin Infrastructure Library
# ==============================================================================
# Constitution Principle VII: Modern CMake with target-based configuration
#
# Provides shared preset management, UI components, and platform abstractions
# used by all Krate Audio plugins (Iterum, Disrumpo, etc.).
#
# Namespace: Krate::Plugins
# ==============================================================================

set(SHARED_PLATFORM_SOURCES
    src/platform/preset_paths.h
    src/platform/preset_paths.cpp
    src/platform/accessibility_helper.h
    src/platform/accessibility_helper.cpp
)

# FR-025c/FR-027: macOS accessibility detection requires Objective-C++ (.mm)
if(APPLE)
    list(APPEND SHARED_PLATFORM_SOURCES
        src/platform/accessibility_helper.mm
    )
endif()

add_library(KratePluginsShared STATIC
    # Platform Abstraction
    ${SHARED_PLATFORM_SOURCES}

    # MIDI CC Management (Spec 012)
    src/midi/midi_cc_manager.h
    src/midi/midi_cc_manager.cpp

    # Preset Management
    src/preset/preset_manager_config.h
    src/preset/preset_info.h
    src/preset/preset_manager.h
    src/preset/preset_manager.cpp

    # UI Components
    src/ui/color_utils.h
    src/ui/arc_knob.h
    src/ui/fieldset_container.h
    src/ui/step_pattern_editor.h
    src/ui/xy_morph_pad.h
    src/ui/adsr_display.h
    src/ui/mod_matrix_types.h
    src/ui/mod_source_colors.h
    src/ui/bipolar_slider.h
    src/ui/mod_matrix_grid.h
    src/ui/mod_ring_indicator.h
    src/ui/mod_heatmap.h
    src/ui/oscillator_type_selector.h
    src/ui/power_button.h
    src/ui/search_debouncer.h
    src/ui/preset_browser_logic.h
    src/ui/category_tab_bar.h
    src/ui/category_tab_bar.cpp
    src/ui/preset_data_source.h
    src/ui/preset_data_source.cpp
    src/ui/preset_browser_view.h
    src/ui/preset_browser_view.cpp
    src/ui/save_preset_dialog_view.h
    src/ui/save_preset_dialog_view.cpp
)

target_include_directories(KratePluginsShared
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    PRIVATE
        ${vst3sdk_SOURCE_DIR}
        ${vst3sdk_SOURCE_DIR}/vstgui4
)

target_link_libraries(KratePluginsShared
    PUBLIC
        sdk
        vstgui
        KrateDSP
)

# FR-025c: macOS AppKit framework for accessibility detection (NSWorkspace API)
if(APPLE)
    find_library(APPKIT_FRAMEWORK AppKit)
    if(APPKIT_FRAMEWORK)
        target_link_libraries(KratePluginsShared PRIVATE ${APPKIT_FRAMEWORK})
    endif()
endif()

target_compile_features(KratePluginsShared PUBLIC cxx_std_20)

# macOS universal binary support (x86_64 + arm64)
# Uses VST3 SDK function - no-op on non-macOS platforms
smtg_target_setup_universal_binary(KratePluginsShared)

# ==============================================================================
# Compiler Warnings
# ==============================================================================
if(MSVC)
    target_compile_options(KratePluginsShared PRIVATE
        /W4
        /permissive-
        /Zc:__cplusplus
        /wd4100
        /wd4458
    )
    if(CMAKE_CXX_COMPILER_LAUNCHER OR CMAKE_C_COMPILER_LAUNCHER)
        target_compile_options(KratePluginsShared PRIVATE /Z7)
    endif()
else()
    target_compile_options(KratePluginsShared PRIVATE
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
