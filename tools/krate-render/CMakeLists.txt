# ==============================================================================
# krate-render -- offline audio render + feature extraction CLI
# ==============================================================================
# Instantiates a plugin Processor directly (no host), triggers a note / applies
# parameters, renders to a WAV, and prints a JSON feature summary (peak, RMS,
# spectral centroid, per-band energy). This is the executor for the mandated
# "render is ground truth" A/B loop.
#
# Currently supports Membrum (the active plugin). Generalizing to the other
# plugins via VST3::Hosting::Module (load the built .vst3) is a follow-up.
# ==============================================================================

add_executable(krate-render
    src/main.cpp
)

target_link_libraries(krate-render
    PRIVATE
        membrum_core
        membrum_dsp
        membrum_preset_io
        KrateDSP
        KratePluginsShared
        # vstgui_support MUST precede sdk so GNU ld resolves the module-init
        # symbols vstguieditor.cpp references against libsdk.a (same convention
        # as the plugin test targets).
        vstgui_support
        sdk
)

target_include_directories(krate-render
    PRIVATE
        ${CMAKE_SOURCE_DIR}/plugins/membrum/src
        ${CMAKE_SOURCE_DIR}/extern/dr_libs
        ${CMAKE_SOURCE_DIR}/tests/test_helpers   # enable_ftz_daz.h, vst_param_changes.h
)

target_compile_features(krate-render PRIVATE cxx_std_20)
krate_plugin_set_warnings(krate-render)
smtg_target_setup_universal_binary(krate-render)
