# ==============================================================================
# Ruinae Plugin Tests
# ==============================================================================
# Constitution Principle VIII: Testing Discipline
# - Plugin tests verify controller, parameters, preset, and UI functionality
# - Unit tests for the Ruinae plugin
# ==============================================================================

# ==============================================================================
# Plugin Unit Tests (placeholder - add test files as they are created)
# ==============================================================================
# add_executable(ruinae_plugin_tests
#     # Parameter tests
#     unit/parameters/global_params_test.cpp
#
#     # Source files needed for linking
#     ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp
# )
#
# target_link_libraries(ruinae_plugin_tests
#     PRIVATE
#         KrateDSP
#         KratePluginsShared
#         Catch2::Catch2
#         test_helpers
#         sdk
#         vstgui
# )
#
# target_include_directories(ruinae_plugin_tests
#     PRIVATE
#         ${CMAKE_CURRENT_SOURCE_DIR}/../src
#         ${CMAKE_SOURCE_DIR}/tests
#         ${vst3sdk_SOURCE_DIR}
#         ${vst3sdk_SOURCE_DIR}/vstgui4
# )
#
# target_compile_features(ruinae_plugin_tests PRIVATE cxx_std_20)
#
# # CTest Integration
# catch_discover_tests(ruinae_plugin_tests REPORTER console)
