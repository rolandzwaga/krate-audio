#pragma once

// ==============================================================================
// Custom tap pattern: registered default time ratios
// ==============================================================================
// specs/iterum-tap-editor/design.md 3.1 (ruling 3): the defaults fill the length coarse to fine, so each block of
// taps revealed by the Taps slider lands on the gaps the earlier block left. Taps 1-4 sit on the quarters of the
// length, taps 5-8 on the odd eighths, taps 9-16 on the odd sixteenths. A fresh 4-tap pattern equals Reset, a fresh
// 8 is every eighth, a fresh 16 every sixteenth; all 16 values are distinct and above 0.
//
// Read by the parameter registration, the MultiTapParams constructor and the view's constructor, so the three
// cannot drift. Standard library only.
// ==============================================================================

#include <array>

namespace Iterum {

inline constexpr std::array<float, 16> kDefaultCustomTimeRatios = {
    0.25f,   0.5f,    0.75f,   1.0f,                                     // quarters
    0.125f,  0.375f,  0.625f,  0.875f,                                   // odd eighths
    0.0625f, 0.1875f, 0.3125f, 0.4375f, 0.5625f, 0.6875f, 0.8125f, 0.9375f  // odd sixteenths
};

} // namespace Iterum
