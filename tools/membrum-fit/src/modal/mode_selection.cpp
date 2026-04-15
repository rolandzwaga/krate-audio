#include "mode_selection.h"

#include <algorithm>
#include <cmath>

namespace MembrumFit::Modal {

// Minimum Description Length order selection using an energy-gradient proxy:
// we approximate the descending singular-value spectrum of a short Hankel
// matrix by the decaying autocorrelation of the signal, then pick the smallest
// N whose cumulative energy crosses 95 % of the total. This is the MDL
// family's same core idea (Wax & Kailath 1985) without re-running SVD.
int selectModelOrder(std::span<const float> decay,
                     double /*sampleRate*/,
                     int    minN,
                     int    maxN) {
    if (decay.size() < 64) return std::clamp((minN + maxN) / 2, minN, maxN);

    // Coarse autocorrelation at lag k, k=1..maxN.
    const int L = std::min<int>(maxN, static_cast<int>(decay.size() / 4));
    std::vector<float> ac(L + 1, 0.0f);
    float norm = 0.0f;
    for (float x : decay) norm += x * x;
    if (norm < 1e-12f) return (minN + maxN) / 2;
    for (int k = 0; k <= L; ++k) {
        float s = 0.0f;
        for (std::size_t i = 0; i + static_cast<std::size_t>(k) < decay.size(); ++i) {
            s += decay[i] * decay[i + k];
        }
        ac[k] = s / norm;
    }
    // Energy in the first K coefficients of the spectrum is approximately
    // sum of |ac[k]| for k=0..K. Find smallest N where sum_{k<=N} |ac[k]|
    // reaches 0.95 of the total.
    float totalAbs = 0.0f;
    for (int k = 1; k <= L; ++k) totalAbs += std::abs(ac[k]);
    if (totalAbs < 1e-9f) return (minN + maxN) / 2;
    float cum = 0.0f;
    for (int k = 1; k <= L; ++k) {
        cum += std::abs(ac[k]);
        if (cum / totalAbs >= 0.95f) return std::clamp(k + 4, minN, maxN);
    }
    return maxN;
}

}  // namespace MembrumFit::Modal
