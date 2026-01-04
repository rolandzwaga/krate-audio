# ==============================================================================
# Iterum Plugin Tests
# ==============================================================================
# Constitution Principle VIII: Testing Discipline
# - Plugin tests verify controller, parameters, preset, and UI functionality
# - Unit tests and approval tests for the Iterum plugin
# ==============================================================================

# ==============================================================================
# Plugin Unit Tests
# ==============================================================================
add_executable(plugin_tests
    # Controller tests
    unit/controller/visibility_controller_multi_control_test.cpp
    unit/controller/visibility_controller_null_safety_test.cpp

    # Parameter tests
    unit/parameters/dropdown_mappings_test.cpp
    unit/parameters/bbd_params_test.cpp
    unit/parameters/digital_params_test.cpp
    unit/parameters/ducking_params_test.cpp
    unit/parameters/freeze_params_test.cpp
    unit/parameters/granular_params_test.cpp
    unit/parameters/mix_scale_consistency_test.cpp
    unit/parameters/multitap_params_test.cpp
    unit/parameters/pingpong_params_test.cpp
    unit/parameters/reverse_params_test.cpp
    unit/parameters/shimmer_params_test.cpp
    unit/parameters/spectral_params_test.cpp
    unit/parameters/tape_params_test.cpp

    # Preset tests
    unit/preset/platform_paths_test.cpp
    unit/preset/preset_data_source_test.cpp
    unit/preset/preset_info_test.cpp
    unit/preset/preset_loading_consistency_test.cpp
    unit/preset/preset_manager_test.cpp

    # Processor tests
    unit/processor/mode_crossfade_tests.cpp
    unit/processor/mix_parameter_conversion_test.cpp

    # UI tests
    unit/ui/preset_browser_logic_test.cpp
    unit/ui/preset_search_e2e_test.cpp
    unit/ui/preset_search_test.cpp
    unit/ui/preset_selection_test.cpp
    unit/ui/search_debounce_test.cpp
    unit/ui/search_integration_test.cpp

    # VST tests
    unit/vst/control_visibility_test.cpp
    unit/vst/digital_width_param_test.cpp
    unit/vst/editor_lifecycle_test.cpp
    unit/vst/granular_tempo_sync_ui_test.cpp
    unit/vst/string_list_parameter_test.cpp
    unit/vst/toggle_control_test.cpp
    unit/vst/version_display_test.cpp

    # Source files needed for linking
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/preset_paths.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/preset/preset_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/preset_data_source.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp
)

target_link_libraries(plugin_tests
    PRIVATE
        KrateDSP
        Catch2::Catch2
        test_helpers
        sdk
        vstgui
)

target_include_directories(plugin_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_SOURCE_DIR}/tests
        ${vst3sdk_SOURCE_DIR}
        ${vst3sdk_SOURCE_DIR}/vstgui4
)

target_compile_features(plugin_tests PRIVATE cxx_std_20)

# ==============================================================================
# Disable -ffast-math for files requiring IEEE 754 compliance
# ==============================================================================
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set_source_files_properties(
        unit/parameters/bbd_params_test.cpp
        unit/parameters/digital_params_test.cpp
        unit/parameters/ducking_params_test.cpp
        unit/parameters/freeze_params_test.cpp
        unit/parameters/granular_params_test.cpp
        unit/parameters/mix_scale_consistency_test.cpp
        unit/parameters/multitap_params_test.cpp
        unit/parameters/pingpong_params_test.cpp
        unit/parameters/reverse_params_test.cpp
        unit/parameters/shimmer_params_test.cpp
        unit/parameters/spectral_params_test.cpp
        unit/parameters/tape_params_test.cpp
        unit/preset/preset_info_test.cpp
        unit/preset/preset_loading_consistency_test.cpp
        unit/preset/preset_manager_test.cpp
        unit/processor/mode_crossfade_tests.cpp
        unit/processor/mix_parameter_conversion_test.cpp
        PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
    )
endif()

# CTest Integration
catch_discover_tests(plugin_tests)

# ==============================================================================
# Approval Tests
# ==============================================================================
add_executable(approval_tests
    approval/test_approvals_main.cpp
)

target_link_libraries(approval_tests
    PRIVATE
        KrateDSP
        Catch2::Catch2
        ApprovalTests::ApprovalTests
)

target_include_directories(approval_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_SOURCE_DIR}/tests
)

target_compile_features(approval_tests PRIVATE cxx_std_20)

# Approval tests need the approved files
set_target_properties(approval_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/approval
)

catch_discover_tests(approval_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/approval
)
