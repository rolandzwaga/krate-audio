# Testing Patterns

## Catch2 Patterns

### Sections for Shared Setup

```cpp
TEST_CASE("BiquadFilter modes", "[dsp][filter]") {
    BiquadFilter filter;
    std::array<float, 1024> buffer;

    // Shared setup
    generateWhiteNoise(buffer.data(), 1024);

    SECTION("lowpass") {
        filter.setLowpass(1000.0f, 44100.0f, 0.707f);
        filter.process(buffer.data(), 1024);

        auto spectrum = computeFFT(buffer.data(), 1024);
        REQUIRE(spectrum.highFrequencyEnergy() < spectrum.lowFrequencyEnergy());
    }

    SECTION("highpass") {
        filter.setHighpass(1000.0f, 44100.0f, 0.707f);
        filter.process(buffer.data(), 1024);

        auto spectrum = computeFFT(buffer.data(), 1024);
        REQUIRE(spectrum.highFrequencyEnergy() > spectrum.lowFrequencyEnergy());
    }
}
```

### Using Approx for Floating-Point

```cpp
// Default epsilon (scale-dependent)
REQUIRE(value == Approx(expected));

// Custom margin (absolute tolerance)
REQUIRE(value == Approx(expected).margin(0.001f));

// Custom epsilon (relative tolerance)
REQUIRE(value == Approx(expected).epsilon(0.01f));

// For near-zero values, use margin
REQUIRE(nearZeroValue == Approx(0.0f).margin(1e-6f));
```

### REQUIRE vs CHECK

```cpp
TEST_CASE("Processing chain", "[dsp]") {
    Processor proc;

    // REQUIRE: Stops test on failure (use for preconditions)
    REQUIRE(proc.initialize());

    // CHECK: Continues on failure (use for multiple independent assertions)
    std::array<float, 100> buffer{};
    proc.process(buffer.data(), 100);

    CHECK(buffer[0] == Approx(expected0));
    CHECK(buffer[50] == Approx(expected50));
    CHECK(buffer[99] == Approx(expected99));
}
```

### Matchers for Complex Assertions

```cpp
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

TEST_CASE("Filter coefficients", "[dsp][filter]") {
    auto coeffs = calculateCoefficients(1000.0f, 44100.0f);

    REQUIRE_THAT(coeffs.a0, WithinAbs(1.0f, 0.001f));
    REQUIRE_THAT(coeffs.b1, WithinRel(expectedB1, 0.01f));
}
```

### Generators for Property-Based Testing

```cpp
TEST_CASE("dB/linear roundtrip", "[dsp][property]") {
    auto linearValue = GENERATE(0.001f, 0.01f, 0.1f, 0.5f, 1.0f, 2.0f, 10.0f);

    float dB = linearToDb(linearValue);
    float recovered = dBToLinear(dB);

    REQUIRE(recovered == Approx(linearValue).epsilon(0.001f));
}
```

### Dynamic Sections

```cpp
TEST_CASE("Lowpass attenuates above cutoff", "[filter]") {
    const std::array<float, 3> cutoffs = {500.0f, 1000.0f, 2000.0f};

    for (float cutoff : cutoffs) {
        DYNAMIC_SECTION("cutoff: " << cutoff << " Hz") {
            Filter f;
            f.setLowpass(cutoff, 44100.0f);

            auto response = measureFrequencyResponse(f, cutoff * 2);
            REQUIRE(response < -6.0f);  // At least -6dB at 2x cutoff
        }
    }
}
```

---

## Test Doubles

### When to Use What

| Type | Use Case | DSP Example |
|------|----------|-------------|
| **Stub** | Return canned data | Fixed parameter values |
| **Fake** | Working simple implementation | In-memory preset storage |
| **Mock** | Verify interactions | Host callback verification |
| **Spy** | Record what happened | Track parameter changes |

### Prefer Fakes Over Mocks for DSP

```cpp
// GOOD: Fake audio buffer
class TestAudioBuffer {
public:
    TestAudioBuffer(size_t size) : data_(size) {}

    float* data() { return data_.data(); }
    size_t size() const { return data_.size(); }

    void fillSine(float freq, float sr) {
        for (size_t i = 0; i < data_.size(); ++i) {
            data_[i] = std::sin(2.0f * M_PI * freq * i / sr);
        }
    }

    float rms() const {
        float sum = 0.0f;
        for (float s : data_) sum += s * s;
        return std::sqrt(sum / data_.size());
    }

private:
    std::vector<float> data_;
};

TEST_CASE("Gain reduces level", "[dsp]") {
    TestAudioBuffer buffer(512);
    buffer.fillSine(440.0f, 44100.0f);
    float originalRms = buffer.rms();

    applyGain(buffer.data(), buffer.size(), 0.5f);

    REQUIRE(buffer.rms() == Approx(originalRms * 0.5f));
}
```

### Minimal Mocking (Only External Dependencies)

```cpp
class MockHostCallback : public IHostCallback {
public:
    void beginEdit(ParamID id) override { editedParams_.push_back(id); }
    void performEdit(ParamID id, float value) override {}
    void endEdit(ParamID id) override {}

    std::vector<ParamID> editedParams_;
};

TEST_CASE("Controller notifies host of edits", "[controller]") {
    MockHostCallback host;
    Controller ctrl(&host);

    ctrl.setParameterFromUI(kGainId, 0.5f);

    REQUIRE(host.editedParams_.size() == 1);
    REQUIRE(host.editedParams_[0] == kGainId);
}
```

---

## UI Testing with VSTGUI Mocks

Unlike DSP code, UI testing often requires mocking VSTGUI components.

### Mock CDataBrowser Example

```cpp
class MockDataBrowser {
public:
    int32_t selectedRow_ = -1;
    bool unselectAllCalled_ = false;

    int32_t getSelectedRow() const { return selectedRow_; }
    void setSelectedRow(int32_t row) { selectedRow_ = row; }
    void unselectAll() {
        unselectAllCalled_ = true;
        selectedRow_ = -1;
    }
    void resetMockState() {
        unselectAllCalled_ = false;
    }
};
```

### Testing Selection Toggle Logic

```cpp
TEST_CASE("PresetDataSource toggle selection behavior", "[ui][preset-browser]") {
    MockDataBrowser mockBrowser;
    PresetDataSource dataSource;

    SECTION("clicking unselected row allows default selection") {
        mockBrowser.setSelectedRow(-1);
        auto result = dataSource.handleMouseDown(0, &mockBrowser);
        REQUIRE(result == kMouseEventNotHandled);
        REQUIRE_FALSE(mockBrowser.unselectAllCalled_);
    }

    SECTION("clicking already-selected row deselects it") {
        mockBrowser.setSelectedRow(2);
        auto result = dataSource.handleMouseDown(2, &mockBrowser);
        REQUIRE(result == kMouseEventHandled);
        REQUIRE(mockBrowser.unselectAllCalled_);
    }
}
```

---

## The Humble Object Pattern for UI

Extract testable logic from UI components:

```cpp
// Pure function - easily testable without mocks
enum class SelectionAction { AllowDefault, Deselect };

inline SelectionAction determineSelectionAction(int32_t clickedRow, int32_t currentSelectedRow) {
    if (currentSelectedRow >= 0 && currentSelectedRow == clickedRow) {
        return SelectionAction::Deselect;
    }
    return SelectionAction::AllowDefault;
}

// In the UI component - just calls the pure function
VSTGUI::CMouseEventResult PresetDataSource::dbOnMouseDown(...) {
    auto action = determineSelectionAction(row, previousSelectedRow_);

    if (action == SelectionAction::Deselect) {
        browser->unselectAll();
        return VSTGUI::kMouseEventHandled;
    }
    return VSTGUI::kMouseEventNotHandled;
}
```

Now logic is trivially testable:

```cpp
TEST_CASE("determineSelectionAction", "[ui][preset-browser]") {
    SECTION("no selection - allow default") {
        REQUIRE(determineSelectionAction(0, -1) == SelectionAction::AllowDefault);
    }

    SECTION("clicking selected row - deselect") {
        REQUIRE(determineSelectionAction(2, 2) == SelectionAction::Deselect);
    }

    SECTION("clicking different row - allow default") {
        REQUIRE(determineSelectionAction(5, 2) == SelectionAction::AllowDefault);
    }
}
```

---

## Floating-Point Testing

### Comparison Strategies

| Situation | Strategy | Example |
|-----------|----------|---------|
| Known exact value | Direct | `REQUIRE(x == 0.0f)` |
| Expected approximate | Margin | `Approx(expected).margin(1e-5f)` |
| Relative precision | Epsilon | `Approx(expected).epsilon(0.01f)` |
| Near zero | Margin only | `Approx(0.0f).margin(1e-7f)` |

### Handling Platform Differences

Floating-point results can vary across compilers, optimization levels, and architectures:

```cpp
TEST_CASE("Filter is stable", "[dsp][filter]") {
    // Use tolerance that accounts for platform differences
    constexpr float tolerance = 1e-5f;

    BiquadFilter filter;
    filter.setLowpass(1000.0f, 44100.0f, 0.707f);

    std::array<float, 100> buffer;
    std::fill(buffer.begin(), buffer.end(), 1.0f);
    filter.process(buffer.data(), 100);

    REQUIRE(buffer[99] == Approx(expectedSteadyState).margin(tolerance));
}
```

### Testing for NaN/Inf

```cpp
inline bool isValidSample(float sample) {
    return std::isfinite(sample) && std::abs(sample) <= 100.0f;
}

TEST_CASE("Output is always valid", "[dsp][safety]") {
    Processor proc;
    proc.prepare(44100.0, 512);

    std::array<float, 512> buffer;

    SECTION("denormals") {
        std::fill(buffer.begin(), buffer.end(), 1e-40f);
    }

    SECTION("very large") {
        std::fill(buffer.begin(), buffer.end(), 1e10f);
    }

    proc.processBlock(buffer.data(), 512);

    for (size_t i = 0; i < 512; ++i) {
        REQUIRE(isValidSample(buffer[i]));
    }
}
```

---

## Approval Testing (Golden Masters)

Compare current output against previously approved output:

```cpp
#include <ApprovalTests.hpp>

TEST_CASE("TapeMode output matches approved", "[regression]") {
    TapeMode tape;
    tape.prepare(44100.0, 512);
    tape.setWow(0.5f);
    tape.setFlutter(0.3f);

    std::array<float, 4096> buffer;
    generateSine(buffer.data(), 4096, 440.0f, 44100.0f);
    tape.process(buffer.data(), 4096);

    // Convert to string for approval
    std::ostringstream oss;
    for (size_t i = 0; i < 4096; i += 64) {
        oss << std::fixed << std::setprecision(6) << buffer[i] << "\n";
    }

    Approvals::verify(oss.str());
}
```

### When to Update Golden Masters

**Update when:**
- Intentionally changing algorithm behavior
- Fixing a bug in the reference
- Improving quality (document the change)

**Never update because:**
- Tests are "red" after unrelated changes
- You don't understand why output changed

---

## Debugging Failing Tests

When a test fails, use Catch2's diagnostic tools BEFORE changing code.

### CAPTURE and INFO

```cpp
TEST_CASE("Filter frequency response", "[filter]") {
    float cutoff = 1000.0f;
    float response = measureResponse(filter, 2000.0f);

    CAPTURE(cutoff, response);  // Printed on failure
    INFO("Expected ~-6dB at 2x cutoff");

    REQUIRE(response < -3.0f);
}
```

**Output on failure:**
```
with expansion:
  -1.2f < -3.0f
with messages:
  cutoff := 1000.0f
  response := -1.2f
  Expected ~-6dB at 2x cutoff
```

### When to Use What

| Tool | When | Output |
|------|------|--------|
| `CAPTURE(x, y)` | See variable values | On failure only |
| `INFO("msg")` | Add context | On failure only |
| `WARN("msg")` | Always see output | Always printed |
| `-s` flag | Verbose run | All sections shown |

### The Debugging Workflow

1. **Test fails** → Read the failure message first
2. **Add `CAPTURE()`** for suspicious variables
3. **Re-run the test** → See values on failure
4. **Fix the code** (not the test expectations)

**DO NOT:** Rebuild "to make sure" the test compiled. If it ran and failed, it compiled
