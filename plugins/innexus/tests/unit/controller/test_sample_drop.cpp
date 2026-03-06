// ==============================================================================
// Sample Drop Target Tests - File extension validation
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "controller/sample_drop_target.h"

TEST_CASE("isSupportedAudioFile accepts valid extensions", "[controller][drop]")
{
    CHECK(Innexus::isSupportedAudioFile("sample.wav"));
    CHECK(Innexus::isSupportedAudioFile("sample.aiff"));
    CHECK(Innexus::isSupportedAudioFile("sample.aif"));
    CHECK(Innexus::isSupportedAudioFile("/path/to/my sample.wav"));
    CHECK(Innexus::isSupportedAudioFile("C:\\Users\\test\\audio.WAV"));
    CHECK(Innexus::isSupportedAudioFile("file.AIFF"));
    CHECK(Innexus::isSupportedAudioFile("file.Aif"));
}

TEST_CASE("isSupportedAudioFile rejects invalid extensions", "[controller][drop]")
{
    CHECK_FALSE(Innexus::isSupportedAudioFile("sample.mp3"));
    CHECK_FALSE(Innexus::isSupportedAudioFile("sample.flac"));
    CHECK_FALSE(Innexus::isSupportedAudioFile("sample.ogg"));
    CHECK_FALSE(Innexus::isSupportedAudioFile("sample.txt"));
    CHECK_FALSE(Innexus::isSupportedAudioFile("sample.wav.bak"));
    CHECK_FALSE(Innexus::isSupportedAudioFile("noextension"));
    CHECK_FALSE(Innexus::isSupportedAudioFile(""));
    // ".wav" is a valid filename (dotfile), so isSupportedAudioFile accepts it
}

TEST_CASE("isSupportedAudioFile handles edge cases", "[controller][drop]")
{
    // Dotfile with extension
    CHECK(Innexus::isSupportedAudioFile(".hidden.wav"));
    // Multiple dots
    CHECK(Innexus::isSupportedAudioFile("my.sample.file.aiff"));
}
