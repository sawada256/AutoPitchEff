#include "dsp/Formant.h"
#include <algorithm>
#include <cmath>

namespace ape {
namespace { constexpr double twoPi = 6.283185307179586476925; }

void FormantFilter::prepare(double sampleRate) {
    sr = sampleRate;
    n = sr <= 50000 ? 1024 : sr <= 100000 ? 2048 : 4096; // ~21 ms window
    mask = n - 1; hop = n / 8;
    p = std::clamp(static_cast<int>(std::lround(40 * sr / 48000)), 16, 64);
    gamma = std::exp(-3.141592653589793 * 75 / sr); // 75 Hz bandwidth expansion
    int frames = 1; while (frames < sr * .2 / hop + 4) frames <<= 1; // > shifter history
    frameMask = frames - 1;
    history.assign(static_cast<size_t>(n), 0);
    window.resize(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) window[static_cast<size_t>(i)] = .5 - .5 * std::cos(twoPi * i / n);
    for (auto* v : {&power, &logMagnitude, &target}) v->assign(static_cast<size_t>(n), 0);
    for (auto* v : {&autocorrelation, &a, &scratch}) v->assign(static_cast<size_t>(p + 1), 0);
    for (auto* v : {&kAnalysis, &kSynthesis, &flat}) v->assign(static_cast<size_t>(p), 0);
    kFrames.assign(static_cast<size_t>(frames * p), 0);
    frameIndex.assign(static_cast<size_t>(frames), -1);
    spectrum.assign(static_cast<size_t>(n), 0);
    twiddles.resize(static_cast<size_t>(n / 2));
    for (int i = 0; i < n / 2; ++i) twiddles[static_cast<size_t>(i)] = std::polar(1.0, -twoPi * i / n);
    bitReverse.resize(static_cast<size_t>(n));
    int bits = 0; while ((1 << bits) < n) ++bits;
    for (int i = 0; i < n; ++i) {
        int r = 0; for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
        bitReverse[static_cast<size_t>(i)] = r;
    }
    for (auto& state : analysisState) state.assign(static_cast<size_t>(p), 0);
    for (auto& state : synthesisState) state.assign(static_cast<size_t>(p), 0);
    for (auto& entry : cache) entry.k.assign(static_cast<size_t>(p), 0);
    reset();
}

void FormantFilter::reset() noexcept {
    std::fill(history.begin(), history.end(), 0.0f);
    std::fill(kFrames.begin(), kFrames.end(), 0.0);
    std::fill(frameIndex.begin(), frameIndex.end(), -1);
    for (auto& state : analysisState) std::fill(state.begin(), state.end(), 0.0);
    for (auto& state : synthesisState) std::fill(state.begin(), state.end(), 0.0);
    for (auto& entry : cache) entry.frame = -1;
    now = 0; newestFrame = -1; write = 0; cacheNext = 0;
}

void FormantFilter::fft(std::vector<std::complex<double>>& data, bool inverse) const noexcept {
    for (int i = 0; i < n; ++i) {
        const int j = bitReverse[static_cast<size_t>(i)];
        if (i < j) std::swap(data[static_cast<size_t>(i)], data[static_cast<size_t>(j)]);
    }
    for (int length = 2; length <= n; length <<= 1) {
        const int step = n / length, half = length / 2;
        for (int i = 0; i < n; i += length)
            for (int j = 0; j < half; ++j) {
                auto w = twiddles[static_cast<size_t>(j * step)];
                if (inverse) w = std::conj(w);
                const auto u = data[static_cast<size_t>(i + j)], v = data[static_cast<size_t>(i + j + half)] * w;
                data[static_cast<size_t>(i + j)] = u + v; data[static_cast<size_t>(i + j + half)] = u - v;
            }
    }
    if (inverse) for (auto& x : data) x /= n;
}

bool FormantFilter::levinson(std::vector<double>& outA, double& error) noexcept {
    auto& r = autocorrelation;
    r[0] = r[0] * 1.0001 + 1e-30; // white-noise regularisation
    std::fill(outA.begin(), outA.end(), 0.0); outA[0] = 1;
    error = r[0];
    for (int i = 1; i <= p; ++i) {
        double acc = r[static_cast<size_t>(i)];
        for (int j = 1; j < i; ++j) acc += outA[static_cast<size_t>(j)] * r[static_cast<size_t>(i - j)];
        const double k = -acc / error;
        if (!(std::abs(k) < 1)) return false;
        for (int j = 1; j < i; ++j) scratch[static_cast<size_t>(j)] = outA[static_cast<size_t>(j)] + k * outA[static_cast<size_t>(i - j)];
        for (int j = 1; j < i; ++j) outA[static_cast<size_t>(j)] = scratch[static_cast<size_t>(j)];
        outA[static_cast<size_t>(i)] = k;
        error *= 1 - k * k;
        if (!(error > 0)) return false;
    }
    return true;
}

void FormantFilter::toReflection(std::vector<double>& c, double* k) noexcept {
    for (int i = p; i >= 1; --i) {
        const double ki = std::clamp(c[static_cast<size_t>(i)], -.999, .999);
        k[i - 1] = ki;
        const double d = 1 - ki * ki;
        for (int j = 1; j < i; ++j) scratch[static_cast<size_t>(j)] = (c[static_cast<size_t>(j)] - ki * c[static_cast<size_t>(i - j)]) / d;
        for (int j = 1; j < i; ++j) c[static_cast<size_t>(j)] = scratch[static_cast<size_t>(j)];
    }
}

// Röbel & Rodet's True Envelope: iterated cepstral smoothing that rides on the
// harmonic peaks instead of averaging between them (LPC's high-F0 weakness).
void FormantFilter::smoothTrueEnvelope(float f0) noexcept {
    double peak = 0; for (double v : power) peak = std::max(peak, v);
    const double floor = peak * 1e-6 + 1e-30; // -60 dB: shallow valleys converge in few iterations
    for (int i = 0; i < n; ++i) {
        logMagnitude[static_cast<size_t>(i)] = .5 * std::log(power[static_cast<size_t>(i)] + floor);
        target[static_cast<size_t>(i)] = logMagnitude[static_cast<size_t>(i)];
    }
    const int order = std::clamp(static_cast<int>(.6 * sr / f0), 4, n / 2 - 2);
    for (int iteration = 0; iteration < 12; ++iteration) {
        for (int i = 0; i < n; ++i) spectrum[static_cast<size_t>(i)] = target[static_cast<size_t>(i)];
        fft(spectrum, true);
        for (int i = order + 1; i < n - order; ++i) spectrum[static_cast<size_t>(i)] = 0;
        fft(spectrum, false);
        double worst = 0;
        for (int i = 0; i < n; ++i) {
            const double v = spectrum[static_cast<size_t>(i)].real();
            power[static_cast<size_t>(i)] = v;
            worst = std::max(worst, logMagnitude[static_cast<size_t>(i)] - v);
            target[static_cast<size_t>(i)] = std::max(target[static_cast<size_t>(i)], v);
        }
        if (worst < .23) break; // within 2 dB of every peak
    }
    for (auto& v : power) v = std::exp(2 * v);
}

void FormantFilter::computeFrame(int64_t frame, float f0) noexcept {
    for (int i = 0; i < n; ++i)
        spectrum[static_cast<size_t>(i)] = history[static_cast<size_t>((write + i) & mask)] * window[static_cast<size_t>(i)];
    fft(spectrum, false);
    for (int i = 0; i < n; ++i) power[static_cast<size_t>(i)] = std::norm(spectrum[static_cast<size_t>(i)]);
    double* out = &kFrames[static_cast<size_t>((frame & frameMask) * p)];
    frameIndex[static_cast<size_t>(frame & frameMask)] = frame; newestFrame = frame;
    if (*std::max_element(power.begin(), power.end()) < 1e-20) {
        std::fill(out, out + p, 0.0); return; // silence: flat envelope
    }
    if (f0 > 0 && (mode == trueEnvelope || (mode == automatic && f0 >= 500))) smoothTrueEnvelope(f0);
    for (int i = 0; i < n; ++i) spectrum[static_cast<size_t>(i)] = power[static_cast<size_t>(i)];
    fft(spectrum, true);
    for (int j = 0; j <= p; ++j) autocorrelation[static_cast<size_t>(j)] = spectrum[static_cast<size_t>(j)].real();
    double error = 0;
    if (autocorrelation[0] < 1e-18 || !levinson(a, error)) { std::fill(out, out + p, 0.0); return; }
    double g = 1; for (int j = 1; j <= p; ++j) { g *= gamma; a[static_cast<size_t>(j)] *= g; }
    toReflection(a, out);
}

const double* FormantFilter::frameK(int64_t frame) const noexcept {
    if (newestFrame < 0 || frame < 0) return flat.data();
    frame = std::clamp(frame, std::max<int64_t>(0, newestFrame - frameMask), newestFrame);
    if (frameIndex[static_cast<size_t>(frame & frameMask)] != frame) return flat.data();
    return &kFrames[static_cast<size_t>((frame & frameMask) * p)];
}

// Envelope 1/|A(w/warp)|^2 refitted with an all-pole model. The refit's error
// power is the gain that keeps the warped envelope at the original level.
const FormantFilter::Warped& FormantFilter::warped(int64_t frame, double warp) noexcept {
    for (const auto& entry : cache) if (entry.frame == frame && std::abs(entry.warp / warp - 1) < .002) return entry;
    auto& entry = cache[static_cast<size_t>(cacheNext)]; cacheNext = (cacheNext + 1) & 3;
    entry.frame = frame; entry.warp = warp;
    const double* k = frameK(frame);
    std::fill(a.begin(), a.end(), 0.0); a[0] = 1;
    for (int i = 1; i <= p; ++i) {
        const double ki = k[i - 1];
        for (int j = 1; j < i; ++j) scratch[static_cast<size_t>(j)] = a[static_cast<size_t>(j)] + ki * a[static_cast<size_t>(i - j)];
        for (int j = 1; j < i; ++j) a[static_cast<size_t>(j)] = scratch[static_cast<size_t>(j)];
        a[static_cast<size_t>(i)] = ki;
    }
    std::fill(spectrum.begin(), spectrum.end(), 0.0);
    for (int j = 0; j <= p; ++j) spectrum[static_cast<size_t>(j)] = a[static_cast<size_t>(j)];
    fft(spectrum, false);
    const int half = n / 2;
    for (int i = 0; i <= half; ++i) power[static_cast<size_t>(i)] = std::norm(spectrum[static_cast<size_t>(i)]);
    for (int i = 0; i <= half; ++i) {
        const double x = i / warp;
        double v = power[static_cast<size_t>(half)];
        if (x < half) { const int b = static_cast<int>(x); const double t = x - b; v = power[static_cast<size_t>(b)] + t * (power[static_cast<size_t>(b + 1)] - power[static_cast<size_t>(b)]); }
        target[static_cast<size_t>(i)] = 1 / std::max(v, 1e-9);
    }
    for (int i = 0; i <= half; ++i) spectrum[static_cast<size_t>(i)] = target[static_cast<size_t>(i)];
    for (int i = half + 1; i < n; ++i) spectrum[static_cast<size_t>(i)] = target[static_cast<size_t>(n - i)];
    fft(spectrum, true);
    for (int j = 0; j <= p; ++j) autocorrelation[static_cast<size_t>(j)] = spectrum[static_cast<size_t>(j)].real();
    double error = 1;
    if (!levinson(a, error)) { std::copy(k, k + p, entry.k.begin()); entry.gain = 1; return entry; }
    entry.gain = std::clamp(std::sqrt(error), .05, 20.0);
    toReflection(a, entry.k.data());
    return entry;
}

void FormantFilter::interpolate(int64_t time, double warp, std::vector<double>& k, double& gain) noexcept {
    const int64_t frame = time / hop;
    const double t = static_cast<double>(time - frame * hop) / hop;
    if (std::abs(warp - 1) < 1e-6) {
        const double* from = frameK(frame - 1); const double* to = frameK(frame);
        for (int i = 0; i < p; ++i) k[static_cast<size_t>(i)] = from[i] + t * (to[i] - from[i]);
        gain = 1; return;
    }
    {   const auto& from = warped(frame - 1, warp);
        for (int i = 0; i < p; ++i) k[static_cast<size_t>(i)] = (1 - t) * from.k[static_cast<size_t>(i)];
        gain = (1 - t) * from.gain; }
    {   const auto& to = warped(frame, warp);
        for (int i = 0; i < p; ++i) k[static_cast<size_t>(i)] += t * to.k[static_cast<size_t>(i)];
        gain += t * to.gain; }
}

std::array<float, 2> FormantFilter::analyse(std::array<float, 2> input, int channels, float mono, float f0) noexcept {
    if (now % hop == 0) computeFrame(now / hop, f0);
    history[static_cast<size_t>(write)] = mono; write = (write + 1) & mask;
    double unused = 1; interpolate(now, 1, kAnalysis, unused);
    std::array<float, 2> out {};
    for (int c = 0; c < channels; ++c) {
        auto& b = analysisState[static_cast<size_t>(c)];
        double f = input[static_cast<size_t>(c)], below = f; // f_0(n) = b_0(n) = x
        for (int i = 0; i < p; ++i) {
            const double old = b[static_cast<size_t>(i)], k = kAnalysis[static_cast<size_t>(i)];
            const double next = f + k * old;
            b[static_cast<size_t>(i)] = below;
            below = old + k * f; f = next;
        }
        out[static_cast<size_t>(c)] = static_cast<float>(f);
    }
    ++now;
    return out;
}

std::array<float, 2> FormantFilter::synthesise(std::array<float, 2> residual, int channels, double age, double warp) noexcept {
    const int64_t source = std::max<int64_t>(0, now - 1 - static_cast<int64_t>(std::llround(age)));
    double gain = 1; interpolate(source, warp, kSynthesis, gain);
    std::array<float, 2> out {};
    for (int c = 0; c < channels; ++c) {
        auto& b = synthesisState[static_cast<size_t>(c)];
        double f = residual[static_cast<size_t>(c)] * gain;
        for (int i = p - 1; i >= 0; --i) {
            const double k = kSynthesis[static_cast<size_t>(i)];
            f -= k * b[static_cast<size_t>(i)];
            if (i + 1 < p) b[static_cast<size_t>(i + 1)] = b[static_cast<size_t>(i)] + k * f;
        }
        b[0] = f;
        out[static_cast<size_t>(c)] = std::isfinite(f) ? static_cast<float>(std::clamp(f, -16.0, 16.0)) : 0.0f;
    }
    return out;
}
double FormantFilter::envelopeDb(double hz) const noexcept {
    const double* k = frameK(newestFrame);
    std::vector<double> c(static_cast<size_t>(p + 1), 0.0), t(c); c[0] = 1;
    for (int i = 1; i <= p; ++i) {
        for (int j = 1; j < i; ++j) t[static_cast<size_t>(j)] = c[static_cast<size_t>(j)] + k[i - 1] * c[static_cast<size_t>(i - j)];
        for (int j = 1; j < i; ++j) c[static_cast<size_t>(j)] = t[static_cast<size_t>(j)];
        c[static_cast<size_t>(i)] = k[i - 1];
    }
    std::complex<double> response = 0;
    for (int j = 0; j <= p; ++j) response += c[static_cast<size_t>(j)] * std::polar(1.0, -twoPi * hz * j / sr);
    return -20 * std::log10(std::abs(response) + 1e-12);
}
}
