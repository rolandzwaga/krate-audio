# ==============================================================================
# Krate Audio - Shared Test Infrastructure
# ==============================================================================
# Constitution Principle VIII: Testing Discipline
#
# This file defines:
#   - test_helpers: Shared utilities for all tests
#   - Benchmarks: Performance measurement tools
#
# Test executables are defined in:
#   - dsp/tests/CMakeLists.txt (DSP unit tests)
#   - plugins/iterum/tests/CMakeLists.txt (Plugin tests)
# ==============================================================================

# Note: Catch2 and ApprovalTests are fetched in root CMakeLists.txt

# ==============================================================================
# Test Helper Library (Shared by all tests)
# ==============================================================================
# add_subdirectory(test_helpers) is called from root CMakeLists.txt
