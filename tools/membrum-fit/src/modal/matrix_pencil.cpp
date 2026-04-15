#include "matrix_pencil.h"

#include <Eigen/Dense>
#include <Eigen/Eigenvalues>
#include <Eigen/SVD>

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace MembrumFit::Modal {

namespace {

// Hilbert transform via FFT: produce analytic signal from a real signal.
std::vector<std::complex<double>> hilbertAnalytic(std::span<const float> x) {
    const std::size_t n = x.size();
    std::size_t N = 1; while (N < n) N <<= 1;
    std::vector<std::complex<double>> X(N, {0.0, 0.0});
    for (std::size_t i = 0; i < n; ++i) X[i] = { static_cast<double>(x[i]), 0.0 };
    // In-place radix-2 forward FFT.
    auto fft = [](std::vector<std::complex<double>>& a, bool invert) {
        const std::size_t M = a.size();
        std::size_t j = 0;
        for (std::size_t i = 1; i < M; ++i) {
            std::size_t bit = M >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap(a[i], a[j]);
        }
        for (std::size_t len = 2; len <= M; len <<= 1) {
            const double ang = (invert ? 2.0 : -2.0) * 3.14159265358979323846 / static_cast<double>(len);
            const std::complex<double> wlen(std::cos(ang), std::sin(ang));
            for (std::size_t i = 0; i < M; i += len) {
                std::complex<double> w(1.0, 0.0);
                for (std::size_t k = 0; k < len / 2; ++k) {
                    const auto u = a[i + k];
                    const auto v = a[i + k + len / 2] * w;
                    a[i + k] = u + v;
                    a[i + k + len / 2] = u - v;
                    w *= wlen;
                }
            }
        }
        if (invert) for (auto& v : a) v /= static_cast<double>(M);
    };
    fft(X, false);
    // Zero negative frequencies, double positive ones (except DC/Nyquist).
    X[0] *= 1.0;
    for (std::size_t k = 1; k < N / 2; ++k) X[k] *= 2.0;
    for (std::size_t k = N / 2; k < N; ++k) X[k] = {0.0, 0.0};
    fft(X, true);
    X.resize(n);
    return X;
}

}  // namespace

ModalDecomposition extractModesMatrixPencil(std::span<const float> decay,
                                            double sampleRate,
                                            int    maxModes) {
    ModalDecomposition out;
    const std::size_t N = decay.size();
    if (N < 64 || sampleRate <= 0.0 || maxModes <= 0) return out;

    // Analytic signal -> complex samples; avoids conjugate-pair doubling.
    const auto analytic = hilbertAnalytic(decay);

    // Pencil parameter L = N/3 (Hua & Sarkar §III-C).
    const std::size_t L = std::max<std::size_t>(static_cast<std::size_t>(N / 3), static_cast<std::size_t>(16));
    if (L >= N) return out;

    // Hankel matrix Y of size (N-L) x L.
    const std::size_t rows = N - L;
    Eigen::MatrixXcd Y(rows, L);
    for (std::size_t i = 0; i < rows; ++i)
        for (std::size_t j = 0; j < L; ++j)
            Y(i, j) = analytic[i + j];

    Eigen::JacobiSVD<Eigen::MatrixXcd> svd(Y, Eigen::ComputeFullU | Eigen::ComputeFullV);
    const auto& S = svd.singularValues();

    // Determine effective rank by keeping singular values > threshold.
    const double sv0 = (S.size() > 0) ? S(0) : 0.0;
    const double tol = sv0 * 1e-3;
    int M = 0;
    for (int i = 0; i < S.size() && i < maxModes; ++i) if (S(i) > tol) ++M;
    if (M == 0) return out;

    // V1 / V2 from Vh (last M columns of V).
    const Eigen::MatrixXcd V = svd.matrixV().leftCols(M);
    Eigen::MatrixXcd V1 = V.topRows(V.rows() - 1);
    Eigen::MatrixXcd V2 = V.bottomRows(V.rows() - 1);

    // Solve V1 * Psi = V2  -> eigenvalues of Psi are the poles.
    Eigen::MatrixXcd Psi = V1.completeOrthogonalDecomposition().solve(V2);
    Eigen::ComplexEigenSolver<Eigen::MatrixXcd> ces(Psi);
    const auto poles = ces.eigenvalues();

    // Recover (f, gamma) per pole. Reject growing exponentials (|z|>=1+eps)
    // and DC/Nyquist poles.
    struct Raw { double freqHz; double gamma; std::complex<double> pole; };
    std::vector<Raw> raws;
    for (int i = 0; i < poles.size(); ++i) {
        const auto z = poles(i);
        const double mag = std::abs(z);
        if (mag <= 0.0 || mag > 1.0 + 1e-3) continue;
        const double omega = std::arg(z);  // sign depends on Hilbert/SVD convention
        if (std::abs(omega) < 1e-6) continue;
        const double freqHz = std::abs(omega) * sampleRate / (2.0 * 3.14159265358979323846);
        const double gamma  = -std::log(mag) * sampleRate;
        if (gamma < -1.0) continue;  // allow tiny negatives from numerical noise
        if (freqHz <= 10.0 || freqHz >= sampleRate * 0.49) continue;
        raws.push_back({freqHz, std::max(gamma, 0.0), z});
    }
    // Deduplicate (frequency-collapse poles within ~1 Hz are conjugate pairs).
    std::sort(raws.begin(), raws.end(), [](const Raw& a, const Raw& b){ return a.freqHz < b.freqHz; });
    {
        std::vector<Raw> uniq;
        for (const auto& r : raws) {
            if (uniq.empty() || std::abs(r.freqHz - uniq.back().freqHz) > 1.0) {
                uniq.push_back(r);
            }
        }
        raws.swap(uniq);
    }
    if (raws.empty()) return out;

    // Solve amplitudes/phases via Vandermonde LS: y[n] = Σ a_k * z_k^n.
    Eigen::MatrixXcd A(static_cast<Eigen::Index>(N), static_cast<Eigen::Index>(raws.size()));
    for (std::size_t n = 0; n < N; ++n) {
        for (std::size_t k = 0; k < raws.size(); ++k) {
            A(static_cast<Eigen::Index>(n), static_cast<Eigen::Index>(k)) =
                std::pow(raws[k].pole, static_cast<double>(n));
        }
    }
    Eigen::VectorXcd y(static_cast<Eigen::Index>(N));
    for (std::size_t n = 0; n < N; ++n) y(static_cast<Eigen::Index>(n)) = analytic[n];
    const Eigen::VectorXcd amps = A.jacobiSvd(Eigen::ComputeThinU | Eigen::ComputeThinV).solve(y);

    double totalRmsSq = 0.0;
    for (std::size_t n = 0; n < N; ++n) totalRmsSq += std::norm(analytic[n]);
    out.totalRms = static_cast<float>(std::sqrt(totalRmsSq / static_cast<double>(N)));

    for (std::size_t k = 0; k < raws.size(); ++k) {
        Mode m;
        m.freqHz    = static_cast<float>(raws[k].freqHz);
        m.decayRate = static_cast<float>(raws[k].gamma);
        const auto a = amps(static_cast<Eigen::Index>(k));
        m.amplitude = static_cast<float>(std::abs(a));
        m.phase     = static_cast<float>(std::arg(a));
        m.quality   = m.amplitude;
        out.modes.push_back(m);
    }
    std::sort(out.modes.begin(), out.modes.end(),
              [](const Mode& a, const Mode& b){ return a.amplitude > b.amplitude; });
    if (static_cast<int>(out.modes.size()) > maxModes) out.modes.resize(maxModes);

    // Residual RMS (signal minus resynthesised).
    double resSq = 0.0;
    for (std::size_t n = 0; n < N; ++n) {
        std::complex<double> synth(0.0, 0.0);
        for (std::size_t k = 0; k < out.modes.size(); ++k) {
            const std::complex<double> z = raws[k].pole;
            synth += amps(static_cast<Eigen::Index>(k)) * std::pow(z, static_cast<double>(n));
        }
        resSq += std::norm(analytic[n] - synth);
    }
    out.residualRms = static_cast<float>(std::sqrt(resSq / static_cast<double>(N)));
    return out;
}

}  // namespace MembrumFit::Modal
