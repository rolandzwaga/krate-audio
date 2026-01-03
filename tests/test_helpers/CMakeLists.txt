# ==============================================================================
# Test Helpers Library
# ==============================================================================
# Shared utilities for DSP testing (signal generators, comparators, etc.)
# ==============================================================================

add_library(test_helpers INTERFACE)

target_include_directories(test_helpers
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(test_helpers
    INTERFACE
        Catch2::Catch2
)
