# ==============================================================================
# FindVST3SDK.cmake
# ==============================================================================
# Helper module to locate and validate VST3 SDK
# ==============================================================================

# Check if SDK path is set
if(NOT DEFINED vst3sdk_SOURCE_DIR)
    set(vst3sdk_SOURCE_DIR "${CMAKE_SOURCE_DIR}/extern/vst3sdk"
        CACHE PATH "Path to VST3 SDK")
endif()

# Validate SDK exists
if(NOT EXISTS "${vst3sdk_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "VST3 SDK not found at: ${vst3sdk_SOURCE_DIR}\n"
        "To fix this:\n"
        "  1. Run: git submodule add https://github.com/steinbergmedia/vst3sdk.git extern/vst3sdk\n"
        "  2. Run: git submodule update --init --recursive\n"
        "Or set vst3sdk_SOURCE_DIR to your SDK location."
    )
endif()

# Check for required SDK components
set(_required_files
    "pluginterfaces/base/ftypes.h"
    "base/source/fobject.h"
    "public.sdk/source/vst/vstaudioeffect.h"
)

foreach(_file ${_required_files})
    if(NOT EXISTS "${vst3sdk_SOURCE_DIR}/${_file}")
        message(FATAL_ERROR
            "VST3 SDK appears incomplete. Missing: ${_file}\n"
            "Try: git submodule update --init --recursive"
        )
    endif()
endforeach()

message(STATUS "Found VST3 SDK at: ${vst3sdk_SOURCE_DIR}")
