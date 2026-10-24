#pragma once
#include <array>
#include <complex>
#include <cstdint>
#include <vector>

namespace ape {
// Source-filter formant processing around the pitch shifter (see
// docs/research/2026-10-14-formant-preservation.md). The input is whitened by
// a time-varying all-pole envelope estimated from past samples only; after
// shifting, the envelope of the frame matching the shifter's read position is
// re-applied, optionally frequency-warped. No lookahead and no added latency.
class FormantFilter {
public:
    // Auto uses LPC below 500 Hz and True Envelope above, where each measured best.
    enum Mode { off = 0, lpc = 1, trueEnvelope = 2, automatic = 3 };
    void prepare(double sampleRate);
    void reset() noexcept;
    void setMode(int value) noexcept { mode = value; }
    // Whitens each channel. `mono` feeds the envelope estimate; `f0` (Hz, 0 if
    // unknown) sets the True Envelope smoothing. Advances time by one sample.
    std::array<float, 2> analyse(std::array<float, 2> input, int channels, float mono, float f0) noexcept;
    // Re-applies the envelope from `age` samples before the current sample,
    // warped by `warp` along frequency (>1 raises formants).
    std::array<float, 2> synthesise(std::array<float, 2> residual, int channels, double age, double warp) noexcept;
    int order() const noexcept { return p; }
    // Newest analysis envelope 1/|A| in dB at `hz` (diagnostics and tests).
    double envelopeDb(double hz) const noexcept;
private:
    struct Warped { int64_t frame = -1; double warp = 0, gain = 1; std::vector<double> k; };
    void fft(std::vector<std::complex<double>>& data, bool inverse) const noexcept;
    void computeFrame(int64_t frame, float f0) noexcept;
    void smoothTrueEnvelope(float f0) noexcept;
    bool levinson(std::vector<double>& outA, double& error) noexcept;
    void toReflection(std::vector<double>& coefficients, double* k) noexcept;
    const double* frameK(int64_t frame) const noexcept;
    const Warped& warped(int64_t frame, double warp) noexcept;
    void interpolate(int64_t time, double warp, std::vector<double>& k, double& gain) noexcept;
    double sr = 48000, gamma = 1;
    int mode = off, n = 1024, mask = 1023, hop = 128, p = 40, frameMask = 0, write = 0, cacheNext = 0;
    int64_t now = 0, newestFrame = -1;
    std::vector<float> history;
    std::vector<double> window, power, logMagnitude, target, autocorrelation, a, scratch, kFrames, kAnalysis, kSynthesis, flat;
    std::vector<int64_t> frameIndex;
    std::vector<std::complex<double>> spectrum, twiddles;
    std::vector<int> bitReverse;
    std::array<std::vector<double>, 2> analysisState, synthesisState;
    std::array<Warped, 4> cache;
};
}
