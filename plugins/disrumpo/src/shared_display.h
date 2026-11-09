#pragma once

// ==============================================================================
// SharedDisplay: the Tier-3 display bridge record (processor -> controller)
// ==============================================================================
// The processor registers a pointer to its SharedDisplay with
// Krate::Plugins::SharedDisplayBridge (a void* registry); the controller looks
// it up and casts it back. Both sides include this one definition, so the
// layout the controller reads is the layout the processor wrote (audit D-112).
// Holds only pointers: no processor or controller types.
// ==============================================================================

#include <krate/dsp/primitives/spectrum_fifo.h>

#include <atomic>

namespace Disrumpo {

struct SharedDisplay {
    Krate::DSP::SpectrumFIFO<8192>* inputFIFO;
    Krate::DSP::SpectrumFIFO<8192>* outputFIFO;
    std::atomic<float>* sampleRate;
};

} // namespace Disrumpo
