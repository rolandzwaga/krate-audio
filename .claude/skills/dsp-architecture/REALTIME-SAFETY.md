# Real-Time Audio Thread Safety

The audio thread (`Processor::process()`) has **hard real-time constraints**. Violating these causes audio glitches, dropouts, or crashes.

---

## Forbidden Operations

These operations are **NEVER** allowed on the audio thread:

### Memory Allocation

```cpp
// FORBIDDEN
new / delete
malloc / free
std::make_unique / std::make_shared
std::vector::push_back()      // May reallocate
std::vector::resize()         // Allocates
std::string operations        // Often allocate
std::map / std::unordered_map // Allocate on insert
```

### Blocking Synchronization

```cpp
// FORBIDDEN
std::mutex
std::lock_guard
std::unique_lock
std::condition_variable
std::future::wait()
```

### Exception Handling

```cpp
// FORBIDDEN
throw
try / catch
```

### I/O Operations

```cpp
// FORBIDDEN
std::cout / std::cerr
printf / fprintf
file operations
logging calls
```

---

## Safe Alternatives

### Pre-allocation in setupProcessing()

```cpp
class MyProcessor {
public:
    void setupProcessing(double sampleRate, int maxBlockSize) {
        // Allocate here - called before audio starts
        buffer_.resize(maxBlockSize);
        delayLine_.resize(static_cast<size_t>(sampleRate * maxDelaySeconds_));
    }

    void process(float* data, int numSamples) {
        // Use pre-allocated buffers - no allocation here
        for (int i = 0; i < numSamples; ++i) {
            buffer_[i] = processOneSample(data[i]);
        }
    }

private:
    std::vector<float> buffer_;
    std::vector<float> delayLine_;
};
```

### Atomic Variables for Thread Communication

```cpp
// For simple values crossing threads
std::atomic<float> gain_{1.0f};
std::atomic<bool> bypass_{false};

// In process() - use relaxed ordering for best performance
float currentGain = gain_.load(std::memory_order_relaxed);

// From UI thread
gain_.store(newValue, std::memory_order_relaxed);
```

### Lock-Free SPSC Queues

For complex data crossing threads (parameter changes, messages):

```cpp
// Single-Producer Single-Consumer queue
// UI thread produces, audio thread consumes
LockFreeQueue<ParameterChange> parameterQueue_;

// Audio thread - non-blocking read
ParameterChange change;
while (parameterQueue_.tryPop(change)) {
    applyParameterChange(change);
}
```

### Fixed-Size Containers

```cpp
// Use std::array instead of std::vector when size is known
std::array<float, 512> fixedBuffer_;

// Or pre-size vectors and use indices
std::vector<float> pool_;  // Sized in setupProcessing()
size_t writeIndex_ = 0;
```

---

## Common Patterns

### Parameter Smoothing (Allocation-Free)

```cpp
class SmoothedValue {
public:
    void prepare(double sampleRate, double rampTimeMs) {
        // Called in setupProcessing()
        rampSamples_ = static_cast<int>(sampleRate * rampTimeMs / 1000.0);
    }

    void setTarget(float target) {
        target_ = target;
        if (rampSamples_ > 0) {
            increment_ = (target_ - current_) / rampSamples_;
            samplesRemaining_ = rampSamples_;
        } else {
            current_ = target_;
        }
    }

    float getNext() {
        if (samplesRemaining_ > 0) {
            current_ += increment_;
            --samplesRemaining_;
        }
        return current_;
    }

private:
    float current_ = 0.0f;
    float target_ = 0.0f;
    float increment_ = 0.0f;
    int rampSamples_ = 0;
    int samplesRemaining_ = 0;
};
```

### Circular Buffer (Pre-allocated)

```cpp
class CircularBuffer {
public:
    void resize(size_t size) {
        // Called in setupProcessing()
        buffer_.resize(size);
        mask_ = size - 1;  // Assumes power of 2
        writeIndex_ = 0;
    }

    void write(float sample) {
        buffer_[writeIndex_ & mask_] = sample;
        ++writeIndex_;
    }

    float read(size_t delay) const {
        return buffer_[(writeIndex_ - delay) & mask_];
    }

private:
    std::vector<float> buffer_;
    size_t mask_ = 0;
    size_t writeIndex_ = 0;
};
```

---

## Verification Checklist

Before committing audio thread code:

- [ ] No `new` / `delete` / `malloc` / `free`
- [ ] No `std::vector::push_back()` or `resize()`
- [ ] No `std::mutex` or other locks
- [ ] No `throw` / `try` / `catch`
- [ ] No `std::cout` / `printf` / logging
- [ ] All buffers pre-allocated in `setupProcessing()`
- [ ] Thread communication uses `std::atomic` or lock-free queues

---

## Testing for Allocations

Use the allocation detector in tests:

```cpp
TEST_CASE("Process is allocation-free", "[dsp][realtime]") {
    MyProcessor proc;
    proc.setupProcessing(44100.0, 512);

    std::array<float, 512> buffer{};

    AllocationDetector detector;
    detector.startTracking();

    proc.process(buffer.data(), 512);

    REQUIRE(detector.getAllocationCount() == 0);
}
```

---

## Why This Matters

Audio callbacks typically have deadlines of 1-10ms. A single allocation or mutex lock can take 10-100ms, causing:

- **Audio dropouts** - Buffer underruns
- **Glitches** - Pops and clicks
- **Priority inversion** - Audio thread blocked by lower-priority thread
- **Host timeouts** - DAW kills unresponsive plugin

The audio thread is the most performance-critical code path in the entire plugin.
