#include "PitchEngine.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace ape {
void Biquad::configure(double rate, double hz, bool high) {
    const auto w = 2 * pi * std::clamp(hz, 5.0, rate * 0.45) / rate;
    const auto c = std::cos(w), alpha = std::sin(w) / std::sqrt(2.0), inv = 1 / (1 + alpha);
    b0 = (high ? 1 + c : 1 - c) * 0.5 * inv;
    b1 = (high ? -(1 + c) : 1 - c) * inv; b2 = b0;
    a1 = -2 * c * inv; a2 = (1 - alpha) * inv;
}
void Biquad::lowpass(double r, double f) { configure(r, f, false); }
void Biquad::highpass(double r, double f) { configure(r, f, true); }
float Biquad::process(float x) noexcept {
    const double y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y;
    return static_cast<float>(y);
}

void PitchDetector::prepare(double sr) {
    nativeRate = sr;
    int capacity = 1; while (capacity < sr * .06) capacity <<= 1;
    nativeHistory.assign(static_cast<size_t>(capacity), 0);
    nativeBrightHistory.assign(static_cast<size_t>(capacity), 0);
    nativeEmphasis.highpass(sr, 650); nativeMask = capacity - 1;
    decimation = std::max(1, static_cast<int>(std::lround(sr / 16000)));
    analysisRate = sr / decimation;
    hop = std::max(8, static_cast<int>(std::lround(analysisRate * 0.0015)));
    hp.highpass(sr, 110); lp1.lowpass(sr, 4300); lp2.lowpass(sr, 4300);
    emphasis.highpass(analysisRate, 650);
    reset();
}
void PitchDetector::reset() noexcept {
    history.fill(0); brightHistory.fill(0); magnitude.fill(0);
    std::fill(nativeHistory.begin(), nativeHistory.end(), 0);
    std::fill(nativeBrightHistory.begin(), nativeBrightHistory.end(), 0); nativeEmphasis.reset(); nativePosition = 0;
    hp.reset(); lp1.reset(); lp2.reset(); emphasis.reset();
    position = filled = hopCount = decimationCount = frameCount = missed = 0;
    previousHz = pendingHz = 0; pendingCount = 0; result = {};
}
void PitchDetector::setSettings(const Settings& s) { settings = s; }
float PitchDetector::at(int age, bool high) const noexcept {
    const auto index = (position - 1 - age) & (size - 1);
    return high ? brightHistory[static_cast<size_t>(index)] : history[static_cast<size_t>(index)];
}
bool PitchDetector::push(float x) noexcept {
    const auto filtered = lp2.process(lp1.process(hp.process(x)));
    nativeHistory[static_cast<size_t>(nativePosition)] = filtered;
    nativeBrightHistory[static_cast<size_t>(nativePosition)] = nativeEmphasis.process(filtered);
    nativePosition = (nativePosition + 1) & nativeMask;
    if (++decimationCount < decimation) return false;
    decimationCount = 0;
    history[static_cast<size_t>(position)] = filtered;
    brightHistory[static_cast<size_t>(position)] = emphasis.process(filtered);
    position = (position + 1) & (size - 1); filled = std::min(filled + 1, size);
    if (++hopCount < hop) return false;
    hopCount = 0; analyse(); return true;
}
float PitchDetector::correlation(int lag, int window, bool high) const noexcept {
    double cross = 0, energy = 1e-15;
    for (int i = 0; i < window; ++i) {
        const double a = at(i, high), b = at(i + lag, high);
        cross += a * b; energy += a * a + b * b;
    }
    return static_cast<float>(2 * cross / energy);
}


// Refine the chosen coarse candidate at the original sample rate. A short local
// window follows vibrato without quantisation error from decimated lag positions.
float PitchDetector::refine(float hz) const noexcept {
    const double period = nativeRate / hz;
    const int window = static_cast<int>(std::max(nativeRate * .004, period * 1.5));
    const int centre = static_cast<int>(std::lround(period));
    const int radius = decimation + 1;
    auto score = [&](int lag) {
        double cross = 0, energy = 1e-15, brightCross = 0, brightEnergy = 1e-15;
        for (int i = 0; i < window; ++i) {
            const auto first = static_cast<size_t>((nativePosition - 1 - i) & nativeMask);
            const auto second = static_cast<size_t>((nativePosition - 1 - i - lag) & nativeMask);
            const double a = nativeHistory[first], b = nativeHistory[second];
            const double c = nativeBrightHistory[first], d = nativeBrightHistory[second];
            cross += a * b; energy += a * a + b * b;
            brightCross += c * d; brightEnergy += c * c + d * d;
        }
        return .45 * 2 * cross / energy + .55 * 2 * brightCross / brightEnergy;
    };
    int best = centre; double peak = score(best);
    const int lo = std::max(2, centre - radius), hi = centre + radius;
    for (int lag = lo; lag <= hi; ++lag) {
        const double value = score(lag);
        if (value > peak) { peak = value; best = lag; }
    }
    if (best == lo || best == hi || peak < .6) return hz;
    const double a = score(best - 1), b = peak, c = score(best + 1);
    const double curvature = a - 2*b + c;
    const double delta = curvature < -1e-10 ? std::clamp(.5*(a-c)/curvature, -.5, .5) : 0;
    return static_cast<float>(std::clamp(nativeRate / (best + delta),
                                        static_cast<double>(settings.minHz), static_cast<double>(settings.maxHz)));
}

void PitchDetector::spectrum() noexcept {
    constexpr int n = 2048;
    const int window = std::min(filled, static_cast<int>(analysisRate * 0.024));
    real.fill(0); imag.fill(0);
    for (int i = 0; i < window; ++i)
        real[static_cast<size_t>(i)] = at(window - 1 - i) * static_cast<float>(0.5 - 0.5 * std::cos(2 * pi * i / (window - 1)));
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(real[static_cast<size_t>(i)], real[static_cast<size_t>(j)]);
    }
    for (int len = 2; len <= n; len <<= 1) {
        const float wr0 = static_cast<float>(std::cos(-2 * pi / len));
        const float wi0 = static_cast<float>(std::sin(-2 * pi / len));
        for (int i = 0; i < n; i += len) {
            float wr = 1, wi = 0;
            for (int j = 0; j < len / 2; ++j) {
                const auto a = static_cast<size_t>(i + j), b = static_cast<size_t>(i + j + len / 2);
                const float vr = real[b] * wr - imag[b] * wi, vi = real[b] * wi + imag[b] * wr;
                real[b] = real[a] - vr; imag[b] = imag[a] - vi; real[a] += vr; imag[a] += vi;
                const float next = wr * wr0 - wi * wi0; wi = wr * wi0 + wi * wr0; wr = next;
            }
        }
    }
    for (int i = 0; i <= n / 2; ++i)
        magnitude[static_cast<size_t>(i)] = std::sqrt(real[static_cast<size_t>(i)] * real[static_cast<size_t>(i)] + imag[static_cast<size_t>(i)] * imag[static_cast<size_t>(i)]);
}
float PitchDetector::harmonicSupport(float hz) const noexcept {
    auto mag = [this](double f) {
        const double bin = std::clamp(f * 2048 / analysisRate, 1.0, 1022.0);
        const auto i = static_cast<size_t>(bin); const auto t = static_cast<float>(bin - static_cast<double>(i));
        return magnitude[i] * (1 - t) + magnitude[i + 1] * t;
    };
    double support = 0, total = 0;
    for (int h = 1; h <= 8 && h * hz < 4600; ++h) {
        const double peak = mag(h * hz);
        const double valley = 0.5 * (mag((h - 0.45) * hz) + mag((h + 0.45) * hz));
        const double weight = 1 / std::sqrt(static_cast<double>(h));
        support += weight * (peak - valley) / (peak + valley + 0.00001);
        total += weight;
    }
    return static_cast<float>(support / std::max(0.01, total));
}
void PitchDetector::analyse() noexcept {
    const int lo = std::max(2, static_cast<int>(analysisRate / settings.maxHz));
    const int hi = std::min(500, static_cast<int>(std::ceil(analysisRate / settings.minHz)));
    const int window = std::clamp(static_cast<int>(analysisRate * 0.010), 80, 240);
    double energy = 0;
    for (int i = 0; i < window; ++i) energy += static_cast<double>(at(i)) * at(i);
    const auto rms = static_cast<float>(std::sqrt(energy / window));
    const auto gate = std::pow(10.0f, settings.gateDb / 20);
    float bestHz = 0, bestConfidence = 0, bestScore = -10;
    if (filled >= window + hi + 2 && rms >= gate) {
        if ((frameCount++ % 3) == 0) spectrum();
        for (int lag = lo - 1; lag <= hi + 1; ++lag) {
            const auto raw = correlation(lag, window, false);
            const auto bright = correlation(lag, window, true);
            nsdf[static_cast<size_t>(lag)] = 0.45f * raw + 0.55f * bright;
        }
        struct Candidate { float hz, confidence, score; };
        std::array<Candidate, 64> candidates {};
        int count = 0;
        for (int lag = lo; lag <= hi && count < 64; ++lag) {
            const float left = nsdf[static_cast<size_t>(lag - 1)], mid = nsdf[static_cast<size_t>(lag)], right = nsdf[static_cast<size_t>(lag + 1)];
            if (mid < 0.25f || mid < left || mid <= right) continue;
            const float denom = left - 2 * mid + right;
            const float delta = std::abs(denom) > 1e-8f ? std::clamp(0.5f * (left - right) / denom, -0.5f, 0.5f) : 0;
            const auto hz = static_cast<float>(analysisRate / (lag + delta));
            if (hz < settings.minHz || hz > settings.maxHz) continue;
            const float confidence = std::clamp(mid - 0.25f * (left - right) * delta, 0.0f, 1.0f);
            const float spectral = harmonicSupport(hz);
            float score = confidence + 0.16f * spectral;
            if (previousHz > 0 && missed < 3) {
                const auto distance = std::abs(12 * std::log2(hz / previousHz));
                score += 0.045f * std::max(0.0f, 1 - distance / 2);
            }
            candidates[static_cast<size_t>(count++)] = { hz, confidence, score };
        }
        // A longer-period repetition of the same waveform is not a new fundamental.
        for (int i = 0; i < count; ++i) {
            auto c = candidates[static_cast<size_t>(i)];
            for (int j = 0; j < count; ++j) {
                const auto& upper = candidates[static_cast<size_t>(j)];
                const auto multiple = upper.hz / c.hz;
                if (multiple > 1.8f && multiple < 4.2f && std::abs(multiple - std::round(multiple)) < 0.06f
                    && upper.confidence > c.confidence * 0.91f)
                    c.score -= 0.20f;
            }
            if (c.score > bestScore) { bestScore = c.score; bestHz = c.hz; bestConfidence = c.confidence; }
        }
    }
    result.confidence = bestConfidence;
    if (bestHz > 0 && bestConfidence >= settings.threshold) {
        const double jump = previousHz > 0 ? std::abs(12 * std::log2(bestHz / previousHz)) : 0;
        bool accept = true;
        if (jump > 2.5 && missed == 0) {
            if (pendingHz > 0 && std::abs(12 * std::log2(bestHz / pendingHz)) < 0.6) ++pendingCount;
            else pendingCount = 1;
            pendingHz = bestHz;
            accept = pendingCount >= 2;
        } else { pendingHz = 0; pendingCount = 0; }
        if (accept) previousHz = refine(bestHz);
        missed = 0;
        result.inputHz = previousHz;
        result.state = TrackingState::tracking;
    } else {
        ++missed;
        const auto holdFrames = static_cast<int>(settings.holdMs * analysisRate / (1000 * hop));
        if (previousHz > 0 && missed <= holdFrames && rms >= gate * 0.4f) {
            result.inputHz = previousHz; result.state = TrackingState::holding;
        } else {
            result.inputHz = 0; previousHz = 0; pendingHz = 0; pendingCount = 0;
            result.state = rms >= gate ? TrackingState::acquiring : TrackingState::silent;
        }
    }
}

void PeriodShifter::prepare(double sampleRate) {
    sr = sampleRate;
    int capacity = 1; while (capacity < sr * 0.1) capacity <<= 1;
    for (auto& buffer : buffers) buffer.assign(static_cast<size_t>(capacity), 0);
    mask = capacity - 1; reset();
}
void PeriodShifter::reset() noexcept {
    for (auto& buffer : buffers) std::fill(buffer.begin(), buffer.end(), 0);
    write = 0; activeChannels = 1; delay = otherDelay = fixedLatency(); blend = 1;
}
float PeriodShifter::read(double age, int channel) const noexcept {
    const double p = write - std::clamp(age, 3.0, static_cast<double>(mask - 4));
    const auto i = static_cast<int>(std::floor(p)); const double t = p - i;
    const auto sample = [this, channel](int k) { return static_cast<double>(buffers[static_cast<size_t>(channel)][static_cast<size_t>(k & mask)]); };
    const double a = sample(i - 1), b = sample(i), c = sample(i + 1), d = sample(i + 2);
    return static_cast<float>(b + 0.5 * t * (c - a + t * (2 * a - 5 * b + 4 * c - d + t * (3 * (b - c) + d - a))));
}

// Search around one period using only already recorded waveform samples. This
// reduces phase mismatch at splices as the voice's period and timbre evolve.
double PeriodShifter::matchPeriod(double age, double period, int direction) const noexcept {
    const double radius = std::min(period * .12, sr * .0007);
    const int window = static_cast<int>(std::clamp(period, 24.0, sr * .006));
    const int stride = std::max(1, static_cast<int>(sr / 24000));
    auto error = [&](double offset) {
        double difference = 0, energy = 1e-12;
        for (int i = 0; i < window; i += stride) {
            for (int channel = 0; channel < activeChannels; ++channel) {
                const double a = read(age + i, channel), b = read(age + direction * offset + i, channel);
                difference += (a-b)*(a-b); energy += a*a+b*b;
            }
        }
        const double deviation = (offset-period) / period;
        return difference / energy + .05 * deviation * deviation;
    };
    double best = period, bestError = error(period);
    for (double offset = period-radius; offset <= period+radius; offset += stride) {
        if (age + direction * offset < fixedLatency()) continue;
        const double value = error(offset);
        if (value < bestError) { best = offset; bestError = value; }
    }
    const double left = error(best - .5), right = error(best + .5);
    const double curvature = left - 2*bestError + right;
    if (curvature > 1e-10) best += std::clamp(.25*(left-right)/curvature, -.5, .5);
    return best;
}

float PeriodShifter::process(float input, double ratio, double period) noexcept {
    return processFrame({input, 0}, 1, ratio, period)[0];
}
std::array<float, 2> PeriodShifter::processFrame(std::array<float, 2> input, int channels, double ratio, double period) noexcept {
    activeChannels = std::clamp(channels, 1, 2);
    for (int c = 0; c < 2; ++c) buffers[static_cast<size_t>(c)][static_cast<size_t>(write)] = input[static_cast<size_t>(c)];
    ratio = std::clamp(ratio, minRatio, maxRatio);
    period = std::clamp(period, sr / 2000, sr / 70);
    const double speed = 1 - ratio;
    double fadeSamples = std::clamp(period * 0.75, 24.0, sr * 0.003);
    // A fast ratio change must never drive the old head into the write cursor.
    if (speed < -0.000001) fadeSamples = std::min(fadeSamples, std::max(1.0, (delay - 3) / -speed));
    const double reserve = std::abs(speed) * fadeSamples + 2;
    if (blend >= 1) {
        if (delay + speed < fixedLatency() + reserve && speed < -0.000001) {
            otherDelay = delay + matchPeriod(delay, period, 1); blend = 0; blendStep = 1 / fadeSamples;
        } else if (delay + speed > fixedLatency() + period + reserve && speed > 0.000001) {
            otherDelay = std::max(static_cast<double>(fixedLatency()), delay - matchPeriod(delay, period, -1)); blend = 0; blendStep = 1 / fadeSamples;
        }
    }
    // Re-evaluate safety during a fade: the ratio may change after it began.
    // If the younger destination becomes unsafe, reverse the fade without a
    // discontinuity (smoothstep(1-x) == 1-smoothstep(x)) and retire that head.
    if (blend < 1 && speed < -0.000001) {
        const double remaining = (1 - blend) / blendStep;
        if (otherDelay < delay && otherDelay + speed * remaining < 3) {
            std::swap(delay, otherDelay); blend = 1 - blend;
            if (blend >= 1) delay = otherDelay;
        }
        const double safeSteps = std::max(1.0, (delay - 3) / -speed);
        blendStep = std::max(blendStep, (1 - blend) / safeSteps);
    }
    std::array<float, 2> output {};
    const double weight = blend * blend * (3 - 2 * blend);
    for (int c = 0; c < activeChannels; ++c) {
        output[static_cast<size_t>(c)] = read(delay, c);
        if (blend < 1) output[static_cast<size_t>(c)] = static_cast<float>(output[static_cast<size_t>(c)] * (1 - weight) + read(otherDelay, c) * weight);
    }
    if (blend < 1) {
        otherDelay += speed; blend = std::min(1.0, blend + blendStep);
        delay += speed;
        if (blend >= 1) delay = otherDelay;
    } else delay += speed;
    delay = std::clamp(delay, 3.0, static_cast<double>(mask - 4));
    write = (write + 1) & mask;
    return output;
}

double PitchEngine::frequencyToMidi(double hz) noexcept { return 69 + 12 * std::log2(std::max(1.0, hz) / 440); }
double PitchEngine::midiToFrequency(double note) noexcept { return 440 * std::exp2((note - 69) / 12); }
void PitchEngine::prepare(double sampleRate) {
    sr = std::clamp(sampleRate, 8000.0, 192000.0);
    detector.prepare(sr); sideDetector.prepare(sr); shifter.prepare(sr); formant.prepare(sr);
    int capacity = 1; while (capacity < sr * .05 + 2) capacity <<= 1;
    for (auto& line : wetDelay) line.assign(static_cast<size_t>(capacity), 0);
    wetDelayMask = capacity - 1; driftCoeff = std::exp(-1 / (sr * .2)); levelCoeff = std::exp(-1 / (sr * .03));
    fadeCoeff = std::exp(-1 / (sr * 0.003)); gainCoeff = std::exp(-1 / (sr * 0.010));
    setSettings(settings); reset();
}
void PitchEngine::reset() noexcept {
    detector.reset(); sideDetector.reset(); missingMs = 0; failedLastFrame = false; shifter.reset(); for (auto& history : dryHistory) history.fill(0); dryPosition = 0;
    decisionNote = -1; candidateNote = -1; candidateFrames = 0;
    targetNote = -1; wantedRatio = ratio = 1; wetGain = 0; gain = 1; latest = {};
    formant.reset(); for (auto& line : wetDelay) std::fill(line.begin(), line.end(), 0.0f);
    wetDelayPosition = 0; levelIn = levelOut = 0; levelGain = 1; drift = driftTarget = 0; driftCountdown = 0; randomState = 0x9E3779B9u;
}
void PitchEngine::setSettings(Settings s) noexcept {
    auto finite = [](float x, float fallback) { return std::isfinite(x) ? x : fallback; };
    s.minHz = std::clamp(finite(s.minHz, 180), 80.0f, 600.0f);
    s.maxHz = std::clamp(finite(s.maxHz, 1200), s.minHz + 40, 1800.0f);
    s.threshold = std::clamp(finite(s.threshold, 0.55f), 0.2f, 0.95f);
    s.amount = std::clamp(finite(s.amount, 1), 0.0f, 1.0f);
    s.mix = std::clamp(finite(s.mix, 1), 0.0f, 1.0f);
    s.retuneMs = std::clamp(finite(s.retuneMs, 0), 0.0f, 100.0f);
    s.stabilityCents = std::clamp(finite(s.stabilityCents, 12), 0.0f, 40.0f);
    s.holdMs = std::clamp(finite(s.holdMs, 18), 0.0f, 60.0f);
    s.gateDb = std::clamp(finite(s.gateDb, -48), -80.0f, -12.0f);
    s.outputDb = std::clamp(finite(s.outputDb, 0), -24.0f, 12.0f);
    s.detectorSource = std::clamp(s.detectorSource, 0, 2);
    s.scale = std::clamp(s.scale, 0, 2); s.root = std::clamp(s.root, 0, 11); s.noteMask &= 0xFFFu;
    auto mask = [](const Settings& value) noexcept {
        if (value.noteMask != 0) return value.noteMask;
        unsigned result = 0;
        for (int c = 0; c < 12; ++c) {
            const int interval = (c-value.root+12)%12;
            if (value.scale == 0 || (((value.scale == 1 ? 0xAB5 : 0x5AD) >> interval) & 1)) result |= 1u << c;
        }
        return result;
    };
    if (mask(settings) != mask(s)) {
        // Reconsider newly permitted notes immediately. Otherwise boundary
        // hysteresis can retain a note from the old scale even after unlinking.
        // Equal pitch-class sets (relative major/minor) keep the held note.
        targetNote = -1; candidateNote = -1; candidateFrames = 0;
    }
    s.fallbackMs = std::clamp(finite(s.fallbackMs,0),0.0f,200.0f);
    s.maxShift = std::clamp(finite(s.maxShift,4.5f),1.0f,12.0f); shifter.setRange(s.maxShift);
    s.formantMode = std::clamp(s.formantMode, 0, 3);
    s.formantPreserve = std::clamp(finite(s.formantPreserve, 1), 0.0f, 1.0f);
    s.formantShift = std::clamp(finite(s.formantShift, 0), -12.0f, 12.0f);
    s.detuneCents = std::clamp(finite(s.detuneCents, 0), -100.0f, 100.0f);
    s.humanizeCents = std::clamp(finite(s.humanizeCents, 0), 0.0f, 100.0f);
    s.wetDelayMs = std::clamp(finite(s.wetDelayMs, 0), 0.0f, 50.0f);
    if (s.formantMode != settings.formantMode) {
        // The shifter's history holds the other (whitened or raw) signal.
        shifter.reset(); formant.reset();
    }
    formant.setMode(s.formantMode);
    formantWarp = std::exp2(s.formantShift / 12.0);
    wetDelaySamples = std::clamp(static_cast<int>(std::lround(sr * s.wetDelayMs / 1000)), 0, wetDelayMask - 1);
    settings = s; detector.setSettings(s); sideDetector.setSettings(s);
    outputGain = std::pow(10.0, settings.outputDb / 20.0);
    // Even hard tuning interpolates the control signal between detector updates.
    ratioCoeff = std::exp(-1 / (sr * std::max(1.0, static_cast<double>(s.retuneMs)) * .001));
    decisionCoeff = std::exp(-detector.hopSize() / (detector.rate() * (.010 + s.stabilityCents * .0015)));
}
bool PitchEngine::allowed(int note) const noexcept {
    if (settings.noteMask != 0) return ((settings.noteMask >> ((note % 12 + 12) % 12)) & 1u) != 0;
    if (settings.scale == 0) return true;
    constexpr int major = 0xAB5, minor = 0x5AD;
    const int pitchClass = ((note - settings.root) % 12 + 12) % 12;
    return (((settings.scale == 1 ? major : minor) >> pitchClass) & 1) != 0;
}
int PitchEngine::nearestNote(double note) const noexcept {
    int best = static_cast<int>(std::lround(note)); double distance = 1000;
    for (int n = static_cast<int>(std::floor(note)) - 12; n <= static_cast<int>(std::ceil(note)) + 12; ++n)
        if (allowed(n) && std::abs(note - n) < distance) { best = n; distance = std::abs(note - n); }
    return best;
}
void PitchEngine::updateTarget(Meter m) noexcept {
    latest.inputHz = m.inputHz; latest.confidence = m.confidence; latest.state = m.state;
    if (m.inputHz > 0) {
        const auto note = frequencyToMidi(m.inputHz);
        // Smooth only the note decision, not the entire correction. Smoothing
        // the latter heavily would leave the singer's vibrato in the output.
        if (decisionNote < 0 || std::abs(note - decisionNote) > 1.5) decisionNote = note;
        else decisionNote = note + decisionCoeff * (decisionNote - note);
        const int next = nearestNote(decisionNote);
        if (forcedNote >= 0 && forcedAbsolute) {
            targetNote = forcedNote; candidateNote = -1; candidateFrames = 0;
        } else if (forcedNote >= 0) {
            // Follow the external note immediately, keeping the chosen octave
            // until the voice is clearly nearer another one.
            const int pitchClass = forcedNote % 12;
            const bool keep = targetNote >= 0 && static_cast<int>(targetNote) % 12 == pitchClass && std::abs(decisionNote - targetNote) < 7.5;
            if (!keep) targetNote = pitchClass + 12 * std::lround((decisionNote - pitchClass) / 12);
            candidateNote = -1; candidateFrames = 0;
        } else if (targetNote < 0 || !allowed(static_cast<int>(targetNote))) {
            targetNote = next; candidateNote = -1; candidateFrames = 0;
        } else if (next != static_cast<int>(targetNote)
                   && std::abs(decisionNote - targetNote) > std::abs(decisionNote - next) + settings.stabilityCents * .02) {
            if (candidateNote == next) ++candidateFrames;
            else { candidateNote = next; candidateFrames = 1; }
            const double confirmationMs = std::abs(next - targetNote) > 1 ? 3 : std::max(3.0f, settings.stabilityCents);
            if (candidateFrames * detector.hopSize() * 1000 / detector.rate() >= confirmationMs) {
                targetNote = next; candidateNote = -1; candidateFrames = 0;
            }
        } else { candidateNote = -1; candidateFrames = 0; }
        latest.targetHz = static_cast<float>(midiToFrequency(targetNote));
        const double limit = settings.maxShift * 100.0;
        const auto cents = std::clamp((targetNote - note) * 100, -limit, limit) * settings.amount;
        wantedRatio = std::exp2(cents / 1200);
    } else { wantedRatio = 1; targetNote = decisionNote = -1; candidateNote = -1; candidateFrames = 0; latest.targetHz = 0; }
}
Meter PitchEngine::meter() const noexcept {
    auto result = latest;
    result.delayMs = shifter.currentDelayMs();
    result.correctionCents = settings.enabled && settings.amount > 0 && latest.inputHz > 0
        ? static_cast<float>(1200 * std::log2(ratio)) : 0;
    return result;
}
float PitchEngine::process(float input) noexcept {
    return processFrame({input, 0}, 1, input)[0];
}
void PitchEngine::processStereo(float& left, float& right) noexcept {
    const auto result = processFrame({left, right}, 2, settings.detectorSource == 1 ? left : settings.detectorSource == 2 ? right : .5f * (left + right));
    left = result[0]; right = result[1];
}
void PitchEngine::processExternal(float& left, float& right, int channels, float reference) noexcept {
    const auto result = processFrame({left, right}, channels, reference);
    left = result[0]; right = result[1];
}
void PitchEngine::processHybrid(float& left, float& right, int channels, float reference) noexcept {
    const auto result = processFrame({left,right},channels,reference,true);
    left=result[0]; right=result[1];
}
std::array<float, 2> PitchEngine::processFrame(std::array<float, 2> input, int channels, float reference, bool hybrid) noexcept {
    for (auto& sample : input) sample = std::isfinite(sample) ? std::clamp(sample, -16.0f, 16.0f) : 0;
    reference = std::isfinite(reference) ? std::clamp(reference, -16.0f, 16.0f) : 0;
    if (hybrid) {
        const float mainReference = channels == 1 ? input[0] : settings.detectorSource == 1 ? input[0]
            : settings.detectorSource == 2 ? input[1] : .5f*(input[0]+input[1]);
        const bool frame = detector.push(mainReference);
        sideDetector.push(reference); // Always warm: no reacquisition on fallback.
        if (frame) {
            const auto main = detector.meter();
            const bool detected = main.inputHz > 0 && main.state == TrackingState::tracking;
            if (detected) { missingMs=0; failedLastFrame=false; }
            else {
                missingMs=failedLastFrame ? std::min(1000.0,missingMs+1000.0*detector.hopSize()/detector.rate()) : 0;
                failedLastFrame=true;
            }
            latest.usingSidechain=!detected && missingMs>=settings.fallbackMs;
            updateTarget(latest.usingSidechain ? sideDetector.meter() : main);
        }
    } else {
        latest.usingSidechain=false;
        if (detector.push(reference)) updateTarget(detector.meter());
    }
    std::array<float, 2> dry {};
    for (int c = 0; c < channels; ++c) {
        auto& history = dryHistory[static_cast<size_t>(c)];
        history[static_cast<size_t>(dryPosition)] = input[static_cast<size_t>(c)];
        dry[static_cast<size_t>(c)] = history[static_cast<size_t>((dryPosition - latencySamples()) & 15)];
    }
    dryPosition = (dryPosition + 1) & 15;
    double wanted = wantedRatio;
    if (settings.detuneCents != 0 || settings.humanizeCents > 0) {
        // Humanize: a slow random wander (new target every ~250 ms, ~200 ms glide).
        if (--driftCountdown <= 0) {
            driftCountdown = static_cast<int>(sr * .25);
            randomState = randomState * 1664525u + 1013904223u;
            driftTarget = (randomState >> 8) * (2.0 / 16777216.0) - 1;
        }
        drift = driftTarget + driftCoeff * (drift - driftTarget);
        if (latest.inputHz > 0) wanted *= std::exp2((settings.detuneCents + settings.humanizeCents * drift) / 1200);
    }
    ratio = wanted + ratioCoeff * (ratio - wanted);
    const bool active = settings.enabled && settings.amount > 0 && latest.inputHz > 0;
    const double wetTarget = active ? settings.mix : 0;
    wetGain = wetTarget + fadeCoeff * (wetGain - wetTarget);
    const double period = sr / std::max(80.0f, latest.inputHz > 0 ? latest.inputHz : 440.0f);
    std::array<float, 2> wet;
    if (settings.formantMode != FormantFilter::off) {
        const float mono = channels == 1 ? input[0] : settings.detectorSource == 1 ? input[0]
            : settings.detectorSource == 2 ? input[1] : .5f * (input[0] + input[1]);
        const auto residual = formant.analyse(input, channels, mono, latest.inputHz);
        wet = shifter.processFrame(residual, channels, ratio, period);
        // Preserve 0 lets formants follow the pitch ratio, as without this stage.
        const double warp = std::pow(ratio, 1.0 - settings.formantPreserve) * formantWarp;
        wet = formant.synthesise(wet, channels, shifter.readAgeSamples(), warp);
        // Re-sampling a sharp envelope at new harmonics changes loudness (LPC
        // at high F0 most). Match the input level over ~30 ms, within +/-12 dB,
        // like the spectral-norm normalisation in iZotope's RT-SVC (DAFx23).
        double inPower = 0, outPower = 0;
        for (int c = 0; c < channels; ++c) {
            inPower += static_cast<double>(input[static_cast<size_t>(c)]) * input[static_cast<size_t>(c)];
            outPower += static_cast<double>(wet[static_cast<size_t>(c)]) * wet[static_cast<size_t>(c)];
        }
        levelIn = inPower + levelCoeff * (levelIn - inPower); levelOut = outPower + levelCoeff * (levelOut - outPower);
        const double match = std::clamp(std::sqrt((levelIn + 1e-12) / (levelOut + 1e-12)), .25, 4.0);
        levelGain = match + levelCoeff * (levelGain - match);
        for (int c = 0; c < channels; ++c) wet[static_cast<size_t>(c)] = static_cast<float>(wet[static_cast<size_t>(c)] * levelGain);
    } else wet = shifter.processFrame(input, channels, ratio, period);
    for (int c = 0; c < channels; ++c) {
        auto& line = wetDelay[static_cast<size_t>(c)];
        line[static_cast<size_t>(wetDelayPosition)] = wet[static_cast<size_t>(c)];
        if (wetDelaySamples > 0) wet[static_cast<size_t>(c)] = line[static_cast<size_t>((wetDelayPosition - wetDelaySamples) & wetDelayMask)];
    }
    wetDelayPosition = (wetDelayPosition + 1) & wetDelayMask;
    gain = outputGain + gainCoeff * (gain - outputGain);
    std::array<float, 2> output {};
    latest.inputPeak *= .9995f; latest.outputPeak *= .9995f;
    for (int c = 0; c < channels; ++c) {
        const auto index = static_cast<size_t>(c);
        output[index] = static_cast<float>((dry[index] * (1 - wetGain) + wet[index] * wetGain) * gain);
        latest.inputPeak = std::max(std::abs(input[index]), latest.inputPeak);
        latest.outputPeak = std::max(std::abs(output[index]), latest.outputPeak);
    }
    return output;
}
}
