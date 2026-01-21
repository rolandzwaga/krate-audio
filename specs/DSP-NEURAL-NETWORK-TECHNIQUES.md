# DSP Neural Network Techniques Reference

A comprehensive reference guide for implementing neural network-based audio effects in real-time digital signal processing. This document covers architectures, inference libraries, training workflows, and integration strategies for the KrateDSP layered architecture.

## Table of Contents

1. [Overview](#overview)
2. [Neural Network Architectures for Audio](#neural-network-architectures-for-audio)
3. [RTNeural: Real-Time Inference Library](#rtneural-real-time-inference-library)
4. [WaveNet Architecture](#wavenet-architecture)
5. [Recurrent Networks (LSTM/GRU)](#recurrent-networks-lstmgru)
6. [Temporal Convolutional Networks (TCN)](#temporal-convolutional-networks-tcn)
7. [Training Workflows](#training-workflows)
8. [Layered Architecture Proposal](#layered-architecture-proposal)
9. [Anti-Aliasing Considerations](#anti-aliasing-considerations)
10. [Performance Optimization](#performance-optimization)
11. [Memory Management for Real-Time Safety](#memory-management-for-real-time-safety)
12. [Integration Patterns](#integration-patterns)
13. [Practical Implementation Guidelines](#practical-implementation-guidelines)
14. [References and Further Reading](#references-and-further-reading)

---

## Overview

Neural network-based audio processing uses deep learning models to emulate analog audio equipment (amplifiers, pedals, compressors) or create novel audio effects. Unlike traditional DSP approaches that model circuits mathematically, neural networks learn transfer characteristics directly from input-output audio pairs.

### Key Advantages

- **Black-box modeling**: No circuit analysis required
- **High accuracy**: Can capture subtle nonlinearities and dynamics
- **Rapid development**: Train on audio samples rather than derive equations
- **Parametric control**: Condition networks on knob positions for controllable effects

### Key Challenges

- **CPU cost**: Significantly higher than traditional DSP
- **Latency**: Some architectures introduce samples of delay
- **Training data**: Requires carefully captured audio pairs
- **Real-time safety**: Standard ML libraries allocate memory, violating audio constraints

### When to Use Neural Networks

| Use Case | Neural Network | Traditional DSP |
|----------|---------------|-----------------|
| Complex nonlinear circuits | Excellent | Difficult |
| High-gain amplifiers | Excellent | Moderate |
| Simple filters/EQ | Overkill | Excellent |
| Memory effects (reverb tail) | Good (large receptive field needed) | Excellent |
| Modulation effects | Possible but inefficient | Excellent |
| Unknown/vintage gear modeling | Only option | Requires analysis |

**Sources:**
- [MDPI - Real-Time Guitar Amplifier Emulation with Deep Learning](https://www.mdpi.com/2076-3417/10/3/766)
- [Neural Amp Modeler](https://www.neuralampmodeler.com/)

---

## Neural Network Architectures for Audio

### Architecture Comparison

| Architecture | Latency | CPU Cost | Memory Effects | Best For |
|--------------|---------|----------|----------------|----------|
| WaveNet (dilated conv) | Low (causal) | High | Long (via dilation) | Amp modeling, high quality |
| LSTM | Zero | Medium | Good | Amp/pedal modeling, embedded |
| GRU | Zero | Low (~33% less than LSTM) | Good | Embedded systems |
| TCN | Low | Medium | Configurable | Compressors, dynamic effects |
| Dense (feedforward) | Zero | Very Low | None | Static waveshaping only |

### Receptive Field

The **receptive field** determines how far back in time the network can "see" to predict the current sample. For audio at 48kHz:

- **1ms** = 48 samples (minimum for basic effects)
- **10ms** = 480 samples (adequate for most amplifiers)
- **100ms** = 4800 samples (needed for compressors, reverb tails)

```
Receptive field = Σ (kernel_size - 1) × dilation_rate + 1
```

### Conditioning (Parametric Control)

To create controllable effects (e.g., gain knob), use **Feature-wise Linear Modulation (FiLM)**:

```cpp
// FiLM conditioning: scale and shift activations based on parameters
output = gamma * activation + beta;
// where gamma and beta are derived from control parameters
```

**Sources:**
- [Comparative Study of State-based Neural Networks](https://asmp-eurasipjournals.springeropen.com/articles/10.1186/s13636-025-00416-3)
- [Efficient Neural Networks for Real-time Analog Audio Effect Modeling](https://arxiv.org/abs/2102.06200)

---

## RTNeural: Real-Time Inference Library

[RTNeural](https://github.com/jatinchowdhury18/RTNeural) is a lightweight C++ neural network inference library designed specifically for real-time audio processing.

### Supported Layers

| Layer Type | Description |
|------------|-------------|
| Dense | Fully connected layer |
| GRU | Gated Recurrent Unit |
| LSTM | Long Short-Term Memory |
| Conv1D | 1-dimensional convolution |
| Conv2D | 2-dimensional convolution |
| BatchNorm1D | Batch normalization (1D) |
| BatchNorm2D | Batch normalization (2D) |
| MaxPooling | Pooling layer |

### Supported Activations

- `tanh` - Hyperbolic tangent (most common for audio)
- `ReLU` - Rectified Linear Unit
- `Sigmoid` - Logistic function
- `SoftMax` - Normalized exponential
- `ELu` - Exponential Linear Unit
- `PReLU` - Parametric ReLU

### Backends

| Backend | Best For | CMake Flag |
|---------|----------|------------|
| **Eigen** | Larger networks | `-DRTNEURAL_EIGEN=ON` |
| **XSIMD** | Smaller networks, SIMD vectorization | `-DRTNEURAL_XSIMD=ON` |
| **STL** | Maximum portability | `-DRTNEURAL_STL=ON` |

### Compile-Time vs Runtime API

**Runtime (Dynamic) - Flexible but slower:**
```cpp
#include <RTNeural/RTNeural.h>

std::ifstream jsonStream("model.json", std::ifstream::binary);
auto model = RTNeural::json_parser::parseJson<float>(jsonStream);
model->reset();

// Process sample
float output = model->forward(&input);
```

**Compile-Time (Static) - Significantly faster:**
```cpp
#include <RTNeural/RTNeural.h>

// Define architecture at compile time
RTNeural::ModelT<float, 1, 1,
    RTNeural::DenseT<float, 1, 8>,
    RTNeural::TanhActivationT<float, 8>,
    RTNeural::LSTMLayerT<float, 8, 8>,
    RTNeural::DenseT<float, 8, 1>
> model;

// Load weights
std::ifstream jsonStream("weights.json");
model.parseJson(jsonStream);
model.reset();

// Process sample (no virtual dispatch)
float output = model.forward(&input);
```

### Integration Example

```cpp
// Header-only usage
#define RTNEURAL_USE_EIGEN 1
#include <RTNeural/RTNeural.h>

class NeuralProcessor {
    RTNeural::ModelT<float, 1, 1,
        RTNeural::LSTMLayerT<float, 1, 16>,
        RTNeural::DenseT<float, 16, 1>
    > model_;
    bool prepared_ = false;

public:
    void loadModel(const std::string& jsonPath) {
        std::ifstream stream(jsonPath);
        model_.parseJson(stream);
        model_.reset();
        prepared_ = true;
    }

    void reset() {
        model_.reset();
    }

    float process(float input) noexcept {
        if (!prepared_) return input;
        return model_.forward(&input);
    }

    void processBlock(float* buffer, size_t numSamples) noexcept {
        if (!prepared_) return;
        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] = model_.forward(&buffer[i]);
        }
    }
};
```

**Sources:**
- [RTNeural GitHub](https://github.com/jatinchowdhury18/RTNeural)
- [RTNeural Documentation](https://ccrma.stanford.edu/~jatin/rtneural/)
- [RTNeural Paper (arXiv)](https://arxiv.org/abs/2106.03037)

---

## WaveNet Architecture

WaveNet uses **dilated causal convolutions** to achieve large receptive fields efficiently. Originally developed by DeepMind for audio generation, it's adapted for real-time effect modeling in Neural Amp Modeler (NAM).

### Architecture Overview

```
Input
  │
  ├─────────────────────────────────────┐
  │                                     │
  ▼                                     │
[Dilated Conv d=1] ──► [Gated Activation] ─┬─► [1x1 Conv] ──► Skip
  │                                     │
  ├─────────────────────────────────────┘
  ▼
[Dilated Conv d=2] ──► [Gated Activation] ─┬─► [1x1 Conv] ──► Skip
  │                                     │
  ...                                   │
  │                                     │
  ▼                                     │
[Dilated Conv d=2^n] ──► [Gated Activation] ──► [1x1 Conv] ──► Skip
                                        │
                                        ▼
                                   [Sum Skips]
                                        │
                                        ▼
                                   [Head 1x1 Conv]
                                        │
                                        ▼
                                     Output
```

### Dilated Convolutions

Dilations expand the receptive field exponentially without increasing parameters:

| Dilation | Pattern | Receptive Field (kernel=3) |
|----------|---------|---------------------------|
| 1 | [1,1,1] | 3 samples |
| 2 | [1,0,1,0,1] | 5 samples |
| 4 | [1,0,0,0,1,0,0,0,1] | 9 samples |
| 8 | [...] | 17 samples |

```cpp
// Dilated convolution conceptually
float dilatedConv(const float* input, const float* kernel,
                  int kernelSize, int dilation, int outputIdx) {
    float sum = 0.0f;
    for (int k = 0; k < kernelSize; ++k) {
        int inputIdx = outputIdx - (kernelSize - 1 - k) * dilation;
        if (inputIdx >= 0) {
            sum += input[inputIdx] * kernel[k];
        }
    }
    return sum;
}
```

### NAM WaveNet Implementation Details

Neural Amp Modeler's WaveNet uses:

- **Stacked layer arrays**: Multiple WaveNet blocks in series
- **Fixed buffer size**: 65536 samples to avoid runtime allocation
- **Buffer rewinding**: When buffer fills, copy receptive field portion to start
- **Gated activation**: `tanh(conv_filter) * sigmoid(conv_gate)`

```cpp
// Simplified NAM-style buffer management
class WaveNetBuffer {
    static constexpr size_t BUFFER_SIZE = 65536;
    std::array<float, BUFFER_SIZE> buffer_;
    size_t writePos_ = 0;
    size_t receptiveField_;

public:
    void prepare(size_t receptiveField) {
        receptiveField_ = receptiveField;
        writePos_ = receptiveField_;  // Leave room for history
    }

    void addSample(float sample) {
        buffer_[writePos_++] = sample;

        // Rewind if approaching end
        if (writePos_ >= BUFFER_SIZE - 1024) {
            rewind();
        }
    }

    void rewind() {
        // Copy receptive field to beginning
        std::copy(buffer_.end() - receptiveField_,
                  buffer_.end(),
                  buffer_.begin());
        writePos_ = receptiveField_;
    }

    float read(size_t samplesBack) const {
        return buffer_[writePos_ - samplesBack - 1];
    }
};
```

### WaveNet Performance Characteristics

| Configuration | Receptive Field | CPU Cost | Quality |
|---------------|-----------------|----------|---------|
| NAM Nano | ~100 samples | Very Low | Basic |
| NAM Feather | ~500 samples | Low | Good |
| NAM Lite | ~2000 samples | Medium | Very Good |
| NAM Standard | ~8000 samples | High | Excellent |

**Sources:**
- [DeepWiki - NAM WaveNet Architecture](https://deepwiki.com/sdatkinson/NeuralAmpModelerCore/2.3.1-wavenet-architecture)
- [Neural Amp Modeler Core](https://github.com/sdatkinson/NeuralAmpModelerCore)
- [Towards Data Science - Neural Networks for Real-Time Audio: WaveNet](https://towardsdatascience.com/neural-networks-for-real-time-audio-wavenet-2b5cdf791c4f/)

---

## Recurrent Networks (LSTM/GRU)

Recurrent neural networks maintain internal state, making them natural for modeling systems with memory (like amplifiers with capacitors).

### LSTM Architecture

```
       ┌────────────────────────────────────┐
       │            Cell State              │
       │  ──────────────────────────────►   │
       │      │           │          │      │
       │      ▼           ▼          ▼      │
       │   [×forget]   [+input]   [tanh]    │
       │      │           │          │      │
       │      ▼           ▼          ▼      │
Input ─┼─► [σ forget] [σ input] [tanh cell] [σ output]
       │       │          │         │          │
       │       └────┬─────┘         │          │
       │            │               │          │
       │            └───────────────┤          │
       │                            │          │
       │                         Hidden ◄──────┘
       │                            │
       └────────────────────────────┼──────────►
                                    ▼
                                 Output
```

### GRU Architecture (Simpler, ~33% faster)

```
Input ─┬─► [σ reset] ─────────┬─► [× reset] ─┬─► [tanh] ─┬─► [× update] ─┬─► Hidden
       │                      │              │           │               │
       ├─► [σ update] ────────┼──────────────┼───────────┘               │
       │                      │              │                           │
       └─► Hidden (prev) ─────┴──────────────┘                           ▼
                                                                      Output
```

### Stateful LSTM for Audio

For real-time audio, use **stateful** processing where state persists across calls:

```cpp
// Stateful LSTM conceptual implementation
class StatefulLSTM {
    // Hidden state (persists between calls)
    std::array<float, HIDDEN_SIZE> h_;
    std::array<float, HIDDEN_SIZE> c_;  // Cell state (LSTM only)

    // Weights
    Matrix Wf_, Wi_, Wc_, Wo_;  // Input weights
    Matrix Uf_, Ui_, Uc_, Uo_;  // Recurrent weights
    Vector bf_, bi_, bc_, bo_;  // Biases

public:
    void reset() {
        std::fill(h_.begin(), h_.end(), 0.0f);
        std::fill(c_.begin(), c_.end(), 0.0f);
    }

    float forward(float input) {
        // Gates
        auto f = sigmoid(Wf_ * input + Uf_ * h_ + bf_);  // Forget
        auto i = sigmoid(Wi_ * input + Ui_ * h_ + bi_);  // Input
        auto c_tilde = tanh(Wc_ * input + Uc_ * h_ + bc_);
        auto o = sigmoid(Wo_ * input + Uo_ * h_ + bo_);  // Output

        // Update cell and hidden state
        c_ = f * c_ + i * c_tilde;
        h_ = o * tanh(c_);

        return h_[0];  // Output (or pass through dense layer)
    }
};
```

### Skip Connections

Adding input directly to output helps the network learn the *difference* rather than full signal:

```cpp
float processWithSkip(float input) {
    return input + network_.forward(input);  // Residual connection
}
```

### Size Guidelines for Embedded Systems

| Platform | LSTM Size | GRU Size | Sample Rate |
|----------|-----------|----------|-------------|
| Daisy Seed (STM32H7) | 8 max | 10 max | 48kHz |
| Raspberry Pi 4 | 32-64 | 40-80 | 48kHz |
| Desktop (single core) | 128+ | 160+ | 48-192kHz |

### Typical Loss Values

| Effect Type | Achievable Loss | Notes |
|-------------|-----------------|-------|
| Distortion pedals | < 0.01 | Excellent |
| Low-gain amps | < 0.02 | Very good |
| High-gain amps | < 0.03 | Good |
| Compressors | < 0.05 | Moderate (needs larger receptive field) |

**Sources:**
- [GuitarML/GuitarLSTM](https://github.com/GuitarML/GuitarLSTM)
- [Towards Data Science - Neural Networks for Real-Time Audio: Stateful LSTM](https://towardsdatascience.com/neural-networks-for-real-time-audio-stateful-lstm-b534babeae5d/)
- [Mini Neural Nets for Guitar Effects with Microcontrollers](https://towardsdatascience.com/mini-neural-nets-for-guitar-effects-with-microcontrollers-ea9cdad2a29c/)

---

## Temporal Convolutional Networks (TCN)

TCNs use 1D convolutions with dilations, similar to WaveNet but often simpler (no gating).

### Architecture

```cpp
// TCN layer structure
class TCNLayer {
    Conv1D conv_;           // Dilated convolution
    BatchNorm1D norm_;      // Optional normalization
    Activation activation_; // ReLU, tanh, etc.

public:
    void forward(const float* input, float* output, size_t length) {
        conv_.forward(input, output, length);
        norm_.forward(output, output, length);
        activation_.forward(output, output, length);
    }
};
```

### Efficient TCN (Rapidly Growing Dilations)

For compressor modeling, use sparse kernels with large dilation growth:

```cpp
// Standard dilations: 1, 2, 4, 8, 16, 32 (receptive field grows linearly)
// Efficient dilations: 1, 3, 9, 27, 81 (receptive field grows faster)

constexpr std::array<int, 5> kEfficientDilations = {1, 3, 9, 27, 81};
// With kernel_size=3: receptive field = 1 + 2*(1+3+9+27+81) = 243 samples
```

**Sources:**
- [Efficient Neural Networks for Real-time Modeling of Analog Dynamic Range Compression](https://csteinmetz1.github.io/tcn-audio-effects/)
- [Randomized Overdrive Neural Networks](https://csteinmetz1.github.io/ronn/)

---

## Training Workflows

### Data Capture Requirements

1. **DI (Direct Input) Signal**: Clean guitar/source signal
2. **Processed Signal**: Output from target amp/pedal
3. **Alignment**: Sample-accurate alignment (same sample count, no offset)

### Capture Setup

```
Guitar ──► DI Box ──┬──► Audio Interface (DI track)
                    │
                    └──► Amp/Pedal ──► Load Box ──► Audio Interface (Processed track)
```

### Training Signal Types

| Signal | Duration | Purpose |
|--------|----------|---------|
| Input sine sweep | 3 min | Captures frequency response |
| Random noise | 3 min | Captures nonlinear behavior |
| Musical content | 5-10 min | Captures dynamic response |
| Parameter sweeps | Per setting | For parametric models |

### Neural Amp Modeler Training Process

```bash
# 1. Record training data
# Use NAM's provided input signal (~3 minutes)

# 2. Train model
python train.py \
    --input_path di_signal.wav \
    --output_path amp_output.wav \
    --architecture standard \
    --epochs 100

# 3. Export for plugin
# Model saved as .nam file (JSON with weights)
```

### PyTorch to RTNeural Export

```python
# Export model weights to JSON
import json
import torch

def export_lstm_model(model, filename):
    state = model.state_dict()

    weights = {
        "lstm": {
            "weights_ih": state["lstm.weight_ih_l0"].tolist(),
            "weights_hh": state["lstm.weight_hh_l0"].tolist(),
            "bias_ih": state["lstm.bias_ih_l0"].tolist(),
            "bias_hh": state["lstm.bias_hh_l0"].tolist()
        },
        "dense": {
            "weights": state["dense.weight"].tolist(),
            "bias": state["dense.bias"].tolist()
        }
    }

    with open(filename, 'w') as f:
        json.dump(weights, f)
```

### ONNX Export (Alternative)

```python
import torch.onnx

# Export to ONNX format
torch.onnx.export(
    model,
    dummy_input,
    "model.onnx",
    export_params=True,
    opset_version=11,
    input_names=['input'],
    output_names=['output'],
    dynamic_axes={'input': {0: 'batch_size'}}
)
```

**Sources:**
- [Neural Amp Modeler Training Guide](https://www.neuralampmodeler.com/)
- [GuitarLSTM Training](https://github.com/GuitarML/GuitarLSTM)
- [PANAMA - Parametric Neural Amp Modeling](https://arxiv.org/html/2509.26564v1)

---

## Layered Architecture Proposal

Neural network components should integrate with the existing KrateDSP layered architecture while maintaining separation of concerns.

### Proposed Layer Organization

```
Layer 0: Core (neural_math.h, neural_activation.h)
    │
    │   - Activation function implementations (tanh, sigmoid, ReLU)
    │   - Fast math approximations for neural networks
    │   - Weight/bias data structures
    │   - No state, pure functions
    │
    ▼
Layer 1: Primitives (dense_layer.h, lstm_cell.h, gru_cell.h, conv1d_layer.h)
    │
    │   - Individual neural network layer primitives
    │   - Stateless compute operations
    │   - No model loading, just forward pass
    │   - Similar to: DelayLine, Biquad, LFO
    │
    ▼
Layer 2: Processors (neural_model.h, wavenet_block.h)
    │
    │   - Composed neural network models
    │   - State management (LSTM hidden state, conv buffers)
    │   - Model weight storage and loading
    │   - Similar to: MultimodeFilter, SaturationProcessor
    │
    ▼
Layer 3: Systems (neural_amp.h, neural_compressor.h)
    │
    │   - Complete neural-based effect systems
    │   - Pre/post processing (DC blocking, gain staging)
    │   - Parameter conditioning (FiLM)
    │   - Similar to: CharacterProcessor, DistortionRack
    │
    ▼
Layer 4: Effects (neural_delay_character.h)
    │
    │   - Integration with other effects
    │   - Neural amp character for delay feedback
    │   - Similar to: TapeDelay, BBDDelay
```

### Layer 0: Core - Neural Math

```cpp
// neural_math.h - Pure functions, no state
namespace Krate::DSP {

// Fast activation functions
namespace NeuralMath {

inline float fastSigmoid(float x) noexcept {
    return x / (1.0f + std::abs(x)) * 0.5f + 0.5f;
}

inline float fastTanh(float x) noexcept {
    // Use existing FastMath::fastTanh
    return FastMath::fastTanh(x);
}

inline float relu(float x) noexcept {
    return x > 0.0f ? x : 0.0f;
}

inline float elu(float x, float alpha = 1.0f) noexcept {
    return x >= 0.0f ? x : alpha * (std::exp(x) - 1.0f);
}

// Gated activation (for WaveNet)
inline float gatedActivation(float filterPath, float gatePath) noexcept {
    return FastMath::fastTanh(filterPath) * fastSigmoid(gatePath);
}

} // namespace NeuralMath
} // namespace Krate::DSP
```

### Layer 1: Primitives - Neural Layers

```cpp
// dense_layer.h - Single dense layer primitive
namespace Krate::DSP {

template<int InputSize, int OutputSize>
class DenseLayer {
    alignas(32) std::array<float, InputSize * OutputSize> weights_;
    alignas(32) std::array<float, OutputSize> bias_;

public:
    void setWeights(const float* weights, const float* bias) noexcept {
        std::copy_n(weights, InputSize * OutputSize, weights_.data());
        std::copy_n(bias, OutputSize, bias_.data());
    }

    void forward(const float* input, float* output) const noexcept {
        for (int o = 0; o < OutputSize; ++o) {
            float sum = bias_[o];
            for (int i = 0; i < InputSize; ++i) {
                sum += input[i] * weights_[o * InputSize + i];
            }
            output[o] = sum;
        }
    }
};

// lstm_cell.h - LSTM cell primitive
template<int InputSize, int HiddenSize>
class LSTMCell {
    // Gate weights: [forget, input, cell, output] concatenated
    alignas(32) std::array<float, 4 * HiddenSize * InputSize> weightsInput_;
    alignas(32) std::array<float, 4 * HiddenSize * HiddenSize> weightsHidden_;
    alignas(32) std::array<float, 4 * HiddenSize> bias_;

    // State
    alignas(32) std::array<float, HiddenSize> hidden_;
    alignas(32) std::array<float, HiddenSize> cell_;

public:
    void reset() noexcept {
        hidden_.fill(0.0f);
        cell_.fill(0.0f);
    }

    void forward(const float* input, float* output) noexcept {
        // Compute gates...
        // Update cell and hidden state...
        // Copy hidden to output
        std::copy_n(hidden_.data(), HiddenSize, output);
    }
};

// gru_cell.h - GRU cell primitive (lighter than LSTM)
template<int InputSize, int HiddenSize>
class GRUCell {
    // Similar structure, but only 3 gates instead of 4
    // ~33% less computation than LSTM
};

// conv1d_layer.h - 1D convolution primitive
template<int InputChannels, int OutputChannels, int KernelSize>
class Conv1DLayer {
    alignas(32) std::array<float, OutputChannels * InputChannels * KernelSize> weights_;
    alignas(32) std::array<float, OutputChannels> bias_;
    int dilation_ = 1;

public:
    void setDilation(int dilation) noexcept { dilation_ = dilation; }

    void forward(const float* input, float* output,
                 size_t inputLength, size_t outputIdx) const noexcept {
        // Dilated convolution implementation
    }
};

} // namespace Krate::DSP
```

### Layer 2: Processors - Neural Models

```cpp
// neural_model.h - Composed model with state management
namespace Krate::DSP {

// Model types enum
enum class NeuralModelType { LSTM, GRU, WaveNet };

// LSTM-based model processor
template<int HiddenSize>
class LSTMModel {
    LSTMCell<1, HiddenSize> lstm_;
    DenseLayer<HiddenSize, 1> output_;
    bool useSkipConnection_ = true;
    bool prepared_ = false;

public:
    void prepare() noexcept {
        reset();
        prepared_ = true;
    }

    void reset() noexcept {
        lstm_.reset();
    }

    bool loadWeights(const std::string& jsonPath);

    [[nodiscard]] float process(float input) noexcept {
        if (!prepared_) return input;

        alignas(32) float lstmInput[1] = {input};
        alignas(32) float lstmOutput[HiddenSize];
        alignas(32) float denseOutput[1];

        lstm_.forward(lstmInput, lstmOutput);
        output_.forward(lstmOutput, denseOutput);

        if (useSkipConnection_) {
            return input + denseOutput[0];
        }
        return denseOutput[0];
    }

    void processBlock(float* buffer, size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] = process(buffer[i]);
        }
    }
};

// WaveNet block processor
class WaveNetBlock {
    static constexpr size_t kBufferSize = 65536;

    // Dilated conv layers
    std::vector<Conv1DLayer<...>> layers_;

    // Circular buffer for history
    std::array<float, kBufferSize> buffer_;
    size_t writePos_ = 0;
    size_t receptiveField_ = 0;

public:
    void prepare(double sampleRate, size_t receptiveField);
    void reset() noexcept;
    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;
};

} // namespace Krate::DSP
```

### Layer 3: Systems - Complete Neural Effects

```cpp
// neural_amp.h - Complete neural amplifier system
namespace Krate::DSP {

class NeuralAmp {
    // Core model (could be LSTM, GRU, or WaveNet)
    std::variant<LSTMModel<16>, LSTMModel<32>, WaveNetBlock> model_;

    // Pre/post processing
    DCBlocker dcBlockerIn_;
    DCBlocker dcBlockerOut_;
    OnePoleSmoother gainSmoother_;

    // Parameters
    float inputGain_ = 1.0f;
    float outputGain_ = 1.0f;

    double sampleRate_ = 0.0;
    bool prepared_ = false;

public:
    void prepare(double sampleRate, int maxBlockSize) noexcept {
        sampleRate_ = sampleRate;
        dcBlockerIn_.prepare(sampleRate, 10.0f);  // 10Hz cutoff
        dcBlockerOut_.prepare(sampleRate, 10.0f);
        gainSmoother_.prepare(sampleRate, 5.0f);  // 5ms smoothing

        std::visit([](auto& m) { m.prepare(); }, model_);
        prepared_ = true;
    }

    void reset() noexcept {
        dcBlockerIn_.reset();
        dcBlockerOut_.reset();
        std::visit([](auto& m) { m.reset(); }, model_);
    }

    bool loadModel(const std::string& path);

    void setInputGain(float db) noexcept {
        inputGain_ = dbToGain(db);
    }

    void setOutputGain(float db) noexcept {
        outputGain_ = dbToGain(db);
    }

    [[nodiscard]] float process(float input) noexcept {
        if (!prepared_) return input;

        // DC block input
        float sample = dcBlockerIn_.process(input);

        // Apply input gain
        sample *= gainSmoother_.process(inputGain_);

        // Neural model
        sample = std::visit([sample](auto& m) {
            return m.process(sample);
        }, model_);

        // DC block output
        sample = dcBlockerOut_.process(sample);

        // Output gain
        return sample * outputGain_;
    }

    void processBlock(float* buffer, size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] = process(buffer[i]);
        }
    }
};

} // namespace Krate::DSP
```

### Directory Structure

```
dsp/include/krate/dsp/
├── core/
│   ├── neural_math.h          # Activation functions, fast math
│   └── neural_types.h         # Weight structures, enums
├── primitives/
│   ├── dense_layer.h          # Dense layer primitive
│   ├── lstm_cell.h            # LSTM cell primitive
│   ├── gru_cell.h             # GRU cell primitive
│   ├── conv1d_layer.h         # 1D convolution primitive
│   └── neural_buffer.h        # Circular buffer for WaveNet
├── processors/
│   ├── lstm_model.h           # LSTM model with weight loading
│   ├── gru_model.h            # GRU model with weight loading
│   ├── wavenet_block.h        # WaveNet block processor
│   └── neural_model_loader.h  # JSON weight parser
├── systems/
│   ├── neural_amp.h           # Complete amp modeling system
│   ├── neural_compressor.h    # Neural compressor system
│   └── neural_saturator.h     # Neural saturation system
└── effects/
    └── neural_delay.h         # Delay with neural amp character
```

---

## Anti-Aliasing Considerations

Neural networks with nonlinear activations (tanh, sigmoid) generate harmonics that can alias. However, the approach differs from traditional distortion:

### When Oversampling Helps

| Architecture | Oversampling Benefit |
|--------------|---------------------|
| WaveNet with tanh | Moderate (already smooth) |
| LSTM/GRU with tanh | Low (inherently bandlimited) |
| Dense + hard clip | High (discontinuities) |
| TCN with ReLU | Moderate |

### Typical Approach

Most neural amp modelers **do not** use oversampling because:
1. The network learns to produce smooth outputs
2. Training data captured at target sample rate
3. Computational cost already high

If aliasing is audible, train with higher sample rate audio instead of oversampling at inference.

---

## Performance Optimization

### SIMD Vectorization

```cpp
// Use Eigen for vectorized matrix operations
#include <Eigen/Dense>

using Vector = Eigen::Matrix<float, Eigen::Dynamic, 1>;
using Matrix = Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic>;

// Vectorized dense layer
void denseForward(const Vector& input, Vector& output,
                  const Matrix& weights, const Vector& bias) {
    output.noalias() = weights * input + bias;
}
```

### Memory Layout

```cpp
// Align weights for SIMD
alignas(32) float weights[...];  // AVX needs 32-byte alignment
alignas(16) float weights[...];  // SSE needs 16-byte alignment

// Use contiguous memory
std::array<float, SIZE> buffer;  // Prefer over std::vector for hot path
```

### Branch Prediction

```cpp
// Activation functions: avoid branches
float fastReLU(float x) noexcept {
    return x * (x > 0.0f);  // Branchless
}

// Use std::max for clarity (compiler optimizes)
float relu(float x) noexcept {
    return std::max(0.0f, x);
}
```

### Performance Targets

| Architecture | Budget (48kHz, single core) |
|--------------|----------------------------|
| LSTM-8 | ~50 ns/sample |
| LSTM-16 | ~100 ns/sample |
| LSTM-32 | ~200 ns/sample |
| GRU-10 | ~40 ns/sample |
| WaveNet Nano | ~150 ns/sample |
| WaveNet Standard | ~500 ns/sample |

---

## Memory Management for Real-Time Safety

### Rules for Audio Thread

```cpp
class RealTimeSafeModel {
    // Pre-allocate all memory
    alignas(32) std::array<float, MAX_HIDDEN> scratch1_;
    alignas(32) std::array<float, MAX_HIDDEN> scratch2_;

    // Weights loaded once, never reallocated
    std::vector<float> weights_;  // Allocated at load time only

public:
    // Load weights BEFORE audio processing starts
    void loadWeights(const std::string& path) {
        // This is OK - called from UI thread
        weights_.resize(calculateWeightCount());
        // ... load weights
    }

    // Process must be lock-free and allocation-free
    float process(float input) noexcept {
        // Use pre-allocated scratch buffers
        // No std::vector operations
        // No mutex locks
        // No exceptions
        return result;
    }
};
```

### Using ANIRA for Thread Safety

[ANIRA](https://arxiv.org/html/2506.12665) provides a framework for separating inference from the audio callback:

```cpp
// ANIRA approach: inference on separate thread
class ANIRAStyleProcessor {
    // Lock-free ring buffer for communication
    moodycamel::ReaderWriterQueue<float> inputQueue_;
    moodycamel::ReaderWriterQueue<float> outputQueue_;

    // Inference runs on dedicated high-priority thread
    std::thread inferenceThread_;
    std::atomic<bool> running_{true};

    void inferenceLoop() {
        while (running_) {
            float input;
            if (inputQueue_.try_dequeue(input)) {
                float output = model_.forward(input);
                outputQueue_.enqueue(output);
            }
        }
    }

public:
    // Audio callback - completely lock-free
    float process(float input) noexcept {
        inputQueue_.enqueue(input);
        float output;
        if (outputQueue_.try_dequeue(output)) {
            return output;
        }
        return lastOutput_;  // Use previous output if new not ready
    }
};
```

**Sources:**
- [ANIRA: Architecture for Neural Network Inference in Real-Time Audio](https://arxiv.org/html/2506.12665)

---

## Integration Patterns

### RTNeural Integration

```cpp
// Add RTNeural as a dependency
// CMakeLists.txt:
// add_subdirectory(extern/RTNeural)
// target_link_libraries(KrateDSP PRIVATE RTNeural)

#include <RTNeural/RTNeural.h>

namespace Krate::DSP {

class RTNeuralWrapper {
    RTNeural::ModelT<float, 1, 1,
        RTNeural::LSTMLayerT<float, 1, 16>,
        RTNeural::DenseT<float, 16, 1>
    > model_;

    bool loaded_ = false;

public:
    bool loadModel(const std::string& jsonPath) {
        try {
            std::ifstream stream(jsonPath);
            if (!stream) return false;
            model_.parseJson(stream);
            model_.reset();
            loaded_ = true;
            return true;
        } catch (...) {
            return false;
        }
    }

    void reset() noexcept {
        model_.reset();
    }

    [[nodiscard]] float process(float input) noexcept {
        if (!loaded_) return input;
        return model_.forward(&input);
    }
};

} // namespace Krate::DSP
```

### NAM Core Integration

```cpp
// Add NAM Core as a dependency (header-only with Eigen)
// CMakeLists.txt:
// target_include_directories(KrateDSP PRIVATE extern/NeuralAmpModelerCore/NAM)

#include "dsp.h"
#include "wavenet.h"

namespace Krate::DSP {

class NAMWrapper {
    std::unique_ptr<nam::DSP> model_;
    double sampleRate_ = 48000.0;

public:
    bool loadModel(const std::string& namPath) {
        try {
            auto dsp = nam::get_dsp(namPath);
            if (!dsp) return false;
            model_ = std::move(dsp);
            model_->prewarm();
            return true;
        } catch (...) {
            return false;
        }
    }

    void prepare(double sampleRate) {
        sampleRate_ = sampleRate;
        // NAM models are trained at specific sample rates
        // May need sample rate conversion if mismatch
    }

    void process(float* buffer, size_t numSamples) noexcept {
        if (!model_) return;
        model_->process(buffer, buffer, static_cast<int>(numSamples));
        model_->finalize_(numSamples);
    }
};

} // namespace Krate::DSP
```

---

## Practical Implementation Guidelines

### Model Selection Decision Tree

```
Is real-time performance critical on embedded?
├── Yes → Use GRU (smallest possible size, 8-10)
└── No → Continue

Is quality the priority?
├── Yes → Use WaveNet (NAM Standard)
└── No → Continue

Is the effect parametric (knobs)?
├── Yes → Use LSTM/GRU with FiLM conditioning
└── No → Use fixed LSTM or WaveNet

Is latency critical?
├── Yes → Use LSTM/GRU (zero latency)
└── No → WaveNet acceptable
```

### Quality Checklist

- [ ] **DC blocking**: Apply before and after neural model
- [ ] **Gain staging**: Normalize input to reasonable range (-1 to 1)
- [ ] **State reset**: Clear state on transport stop/start
- [ ] **Sample rate handling**: Verify model trained at target rate
- [ ] **Denormal flushing**: Apply to model outputs
- [ ] **NaN/Inf checking**: Guard against corrupted models

### Common Pitfalls

1. **Not resetting state** between audio segments (causes pops)
2. **Wrong sample rate** (model sounds different)
3. **Memory allocation** during processing
4. **Missing skip connection** (model only learns residual)
5. **Insufficient training data** (< 3 minutes)
6. **Training with time-based effects** (confuses model)

### Testing Neural Components

```cpp
TEST_CASE("Neural model basic functionality", "[neural]") {
    LSTMModel<16> model;
    model.prepare();

    SECTION("Zero input produces zero output") {
        for (int i = 0; i < 1000; ++i) {
            float out = model.process(0.0f);
            REQUIRE_FALSE(std::isnan(out));
            REQUIRE_FALSE(std::isinf(out));
        }
    }

    SECTION("Reset clears state") {
        // Process some audio
        for (int i = 0; i < 100; ++i) {
            model.process(0.5f);
        }

        model.reset();

        // First output after reset should be near zero
        float out = model.process(0.0f);
        REQUIRE(std::abs(out) < 0.01f);
    }

    SECTION("Stability with impulse") {
        float out = model.process(1.0f);
        for (int i = 0; i < 10000; ++i) {
            out = model.process(0.0f);
            REQUIRE_FALSE(std::isnan(out));
            REQUIRE(std::abs(out) < 10.0f);  // Bounded output
        }
    }
}
```

---

## References and Further Reading

### Academic Papers

- Chowdhury, J. (2021). ["RTNeural: Fast Neural Inferencing for Real-Time Systems"](https://arxiv.org/abs/2106.03037). arXiv:2106.03037.
- Wright, A., et al. (2020). ["Real-Time Guitar Amplifier Emulation with Deep Learning"](https://www.mdpi.com/2076-3417/10/3/766). Applied Sciences.
- Steinmetz, C., & Reiss, J. (2021). ["Efficient Neural Networks for Real-time Modeling of Analog Dynamic Range Compression"](https://arxiv.org/abs/2102.06200).
- Parker, J., et al. (2019). ["Modelling of nonlinear state-space systems using a deep neural network"](https://www.dafx.de/paper-archive/2019/DAFx2019_paper_42.pdf). DAFx-19.

### Libraries and Tools

- [RTNeural](https://github.com/jatinchowdhury18/RTNeural) - Real-time neural network inference
- [Neural Amp Modeler](https://github.com/sdatkinson/neural-amp-modeler) - Amp modeling training/plugin
- [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore) - C++ DSP core
- [GuitarLSTM](https://github.com/GuitarML/GuitarLSTM) - LSTM training for guitar
- [NeuralAudio](https://github.com/mikeoliphant/NeuralAudio) - C++ neural audio library
- [ANIRA](https://arxiv.org/html/2506.12665) - Real-time inference architecture

### Online Resources

- [Towards Data Science - Neural Networks for Real-Time Audio Series](https://medium.com/nerd-for-tech/neural-networks-for-real-time-audio-introduction-ed5d575dc341)
- [CCRMA RTNeural Documentation](https://ccrma.stanford.edu/~jatin/rtneural/)
- [DeepWiki NAM Architecture](https://deepwiki.com/sdatkinson/NeuralAmpModelerCore)
- [GuitarML Papers Collection](https://github.com/GuitarML/mldsp-papers)

### Related Krate DSP Documentation

- `DSP-DISTORTION-TECHNIQUES.md` - Traditional distortion algorithms
- `DSP-FILTER-TECHNIQUES.md` - Filter design reference
- `specs/_architecture_/layer-1-primitives.md` - Layer 1 component documentation

---

*Last updated: January 2026*
