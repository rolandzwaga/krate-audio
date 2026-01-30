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

add_library(KratePluginsShared STATIC
    # Platform Abstraction
    src/platform/preset_paths.h
    src/platform/preset_paths.cpp

    # Preset Management
    src/preset/preset_manager_config.h
    src/preset/preset_info.h
    src/preset/preset_manager.h
    src/preset/preset_manager.cpp

    # UI Components
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
)

target_compile_features(KratePluginsShared PUBLIC cxx_std_20)

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
