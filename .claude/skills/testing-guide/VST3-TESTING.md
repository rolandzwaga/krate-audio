# VST3-Specific Testing

## Steinberg vstvalidator

The VST3 SDK includes a command-line validator tool that checks plugin conformity.

**Location:** `extern/vst3sdk/public.sdk/samples/vst-hosting/validator/`

**Usage:**
```bash
vstvalidator path/to/plugin.vst3
```

**What it tests:**
- Component initialization/termination
- State save/restore
- Parameter handling
- Bus configuration
- Audio processing
- Threading requirements

---

## VST3 Plugin Test Host

The SDK includes a graphical test host with built-in test suites.

**Location:** `extern/vst3sdk/public.sdk/samples/vst-hosting/`

**Features:**
- Load and test plugins interactively
- Run automated test suites
- View detailed test results
- Debug plugin behavior

---

## Writing Custom VST3 Tests

### State Roundtrip Test

```cpp
TEST_CASE("Plugin state saves and restores", "[vst3][state]") {
    // Create processor
    auto processor = createProcessor();
    processor->initialize(nullptr);

    // Set some parameters
    processor->setParameter(kGainId, 0.75f);
    processor->setParameter(kDelayTimeId, 0.5f);

    // Save state
    MemoryStream stream;
    processor->getState(&stream);

    // Create new processor and restore
    auto processor2 = createProcessor();
    processor2->initialize(nullptr);
    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    processor2->setState(&stream);

    // Verify parameters match
    REQUIRE(processor2->getParameter(kGainId) == Approx(0.75f));
    REQUIRE(processor2->getParameter(kDelayTimeId) == Approx(0.5f));
}
```

### Bypass Behavior Test

```cpp
TEST_CASE("Bypass passes audio unchanged", "[vst3][bypass]") {
    auto processor = createProcessor();
    processor->initialize(nullptr);
    processor->setParameter(kBypassId, 1.0f);  // Enable bypass

    std::array<float, 512> input, output;
    generateSine(input.data(), 512, 440.0f, 44100.0f);
    std::copy(input.begin(), input.end(), output.begin());

    processor->process(output.data(), 512);

    for (size_t i = 0; i < 512; ++i) {
        REQUIRE(output[i] == Approx(input[i]));
    }
}
```

---

## Testing Processor/Controller Separation

### Processor Independence Test

```cpp
TEST_CASE("Processor works without controller", "[vst3][separation]") {
    auto processor = createProcessor();
    processor->initialize(nullptr);
    processor->setupProcessing(ProcessSetup{44100.0, 512});
    processor->setActive(true);

    std::array<float, 512> buffer{};
    generateSine(buffer.data(), 512, 440.0f, 44100.0f);

    // Should process without crashing
    REQUIRE_NOTHROW(processor->process(buffer.data(), 512));
}
```

### State Flow Test

```cpp
TEST_CASE("State flows correctly from Processor to Controller", "[vst3][state]") {
    auto processor = createProcessor();
    auto controller = createController();

    processor->initialize(nullptr);
    controller->initialize(nullptr);

    // Set parameter on processor
    processor->setParameter(kGainId, 0.25f);

    // Get processor state
    MemoryStream stream;
    processor->getState(&stream);

    // Controller should update from processor state
    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    controller->setComponentState(&stream);

    REQUIRE(controller->getParamNormalized(kGainId) == Approx(0.25f));
}
```

---

## Common VST3 Test Patterns

### Parameter Normalization Test

```cpp
TEST_CASE("Parameter normalization roundtrip", "[vst3][parameter]") {
    // Test that toPlain and toNormalized are inverses

    auto controller = createController();
    controller->initialize(nullptr);

    // Test at various normalized values
    for (float norm : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        float plain = controller->normalizedParamToPlain(kGainId, norm);
        float recovered = controller->plainParamToNormalized(kGainId, plain);
        REQUIRE(recovered == Approx(norm).margin(1e-5f));
    }
}
```

### Bus Configuration Test

```cpp
TEST_CASE("Plugin supports stereo I/O", "[vst3][bus]") {
    auto processor = createProcessor();
    processor->initialize(nullptr);

    // Check bus count
    REQUIRE(processor->getBusCount(kAudio, kInput) >= 1);
    REQUIRE(processor->getBusCount(kAudio, kOutput) >= 1);

    // Check stereo support
    BusInfo inputInfo, outputInfo;
    processor->getBusInfo(kAudio, kInput, 0, inputInfo);
    processor->getBusInfo(kAudio, kOutput, 0, outputInfo);

    REQUIRE(inputInfo.channelCount == 2);
    REQUIRE(outputInfo.channelCount == 2);
}
```

### Sample Rate Change Test

```cpp
TEST_CASE("Plugin handles sample rate changes", "[vst3][samplerate]") {
    auto processor = createProcessor();
    processor->initialize(nullptr);

    std::array<float, 512> buffer{};

    for (double sampleRate : {44100.0, 48000.0, 88200.0, 96000.0, 192000.0}) {
        ProcessSetup setup{};
        setup.sampleRate = sampleRate;
        setup.maxSamplesPerBlock = 512;

        REQUIRE(processor->setupProcessing(setup) == kResultOk);
        processor->setActive(true);

        // Should process without issues
        REQUIRE_NOTHROW(processor->process(buffer.data(), 512));

        processor->setActive(false);
    }
}
```

---

## Integration with VST3 SDK Types

When testing VST3 code, be aware of SDK type requirements:

```cpp
// Use Steinberg types explicitly to avoid compilation errors
Steinberg::int32 parameterIndex = 0;  // Not just 'int32'
Steinberg::Vst::ParamID paramId = kGainId;

// Use SDK memory streams
Steinberg::MemoryStream stream;

// Use SDK result codes
if (result == Steinberg::kResultOk) { /* ... */ }
```

---

## Validation Checklist

Before releasing, ensure:

1. **vstvalidator passes** with no errors
2. **State roundtrip** works correctly
3. **Bypass** passes audio unchanged
4. **Processor works** without controller attached
5. **All sample rates** are supported (44.1k - 192k)
6. **Block sizes** from 1 to 4096 work correctly
7. **Parameter automation** is smooth (no clicks/pops)
