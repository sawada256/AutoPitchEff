#include "KeyAnalyser.h"
#include <cmath>
#include <numeric>

namespace ape {
unsigned KeyAnalyser::scaleMask(int key) noexcept {
    if (key < 0 || key >= 24) return 0;
    constexpr int major[] = {0, 2, 4, 5, 7, 9, 11}, minor[] = {0, 2, 3, 5, 7, 8, 10};
    unsigned mask = 0;
    for (int n : key < 12 ? major : minor) mask |= 1u << ((key % 12 + n) % 12);
    return mask;
}
KeyAnalyser::KeyAnalyser() {
    for (int i = 0; i < frameSize; ++i) window[static_cast<size_t>(i)] = static_cast<float>(.5 - .5 * std::cos(2 * pi * i / (frameSize - 1)));
    for (int n = 0; n < notes; ++n) {
        for (int h = 1; h <= 6; ++h) {
            const double bin = n + 12 * std::log2(h);
            const int lo = static_cast<int>(std::floor(bin));
            const float part = static_cast<float>(bin - lo), weight = static_cast<float>(std::pow(h, -1.125));
            if (lo < bins) templates[static_cast<size_t>(n)][static_cast<size_t>(lo)] += weight * (1 - part);
            if (lo + 1 < bins) templates[static_cast<size_t>(n)][static_cast<size_t>(lo + 1)] += weight * part;
        }
        for (float x : templates[static_cast<size_t>(n)]) norm[static_cast<size_t>(n)] += x * x;
    }
    // Krumhansl/Temperley-inspired profiles, adapted to the natural-minor
    // scale supported by the tuner: do not expect a raised leading tone.
    constexpr double ks[2][12] = {{6.35,2.23,3.48,2.33,4.38,4.09,2.52,5.19,2.39,3.66,2.29,2.88},
                                  {6.33,2.68,3.52,5.38,2.60,3.53,2.54,4.75,3.98,2.69,3.34,1.8}};
    constexpr double tp[2][12] = {{5,2,3.5,2,4.5,4,2,4.5,2,3.5,1.5,4},
                                  {5,2,3.5,4.5,2,4,2,4.5,3.5,2,3.5,1.5}};
    for (int key = 0; key < 24; ++key) {
        auto& p = profiles[static_cast<size_t>(key)];
        for (int c = 0; c < 12; ++c) { const int i = (c - key % 12 + 12) % 12; p[static_cast<size_t>(c)] = static_cast<float>(.65 * ks[key / 12][i] + .35 * tp[key / 12][i]); }
        const double mean = std::accumulate(p.begin(), p.end(), 0.0) / 12;
        double energy = 0; for (auto& x : p) { x -= static_cast<float>(mean); energy += x * x; }
        for (auto& x : p) x /= static_cast<float>(std::sqrt(energy));
    }
    voice.prepare(sampleRate);
    Settings s; s.minHz = 150; s.maxHz = 1400; s.threshold = .65f; s.gateDb = gate; voice.setSettings(s);
    reset();
}
void KeyAnalyser::reset() {
    for (auto& h : history) h.fill(0);
    average.fill(0); posterior.fill(1.0 / 24); for (auto& r : recent) r.fill(0);
    vocalFrame.fill(0); vocalWeight = 0; voice.reset(); latest = {};
    vocalEnergy.fill(0); vocalChannel = 0;
    position = filled = hop = recentIndex = 0; evidence = silence = tune = 0;
}
void KeyAnalyser::configure(bool vocal, float seconds, float gateDb) {
    if (vocal != vocalMode) { vocalMode = vocal; reset(); }
    memory = std::clamp(seconds, 3.0f, 30.0f); gate = std::clamp(gateDb, -80.0f, -20.0f);
    Settings s; s.minHz = 150; s.maxHz = 1400; s.threshold = .65f; s.gateDb = gate; voice.setSettings(s);
}
bool KeyAnalyser::push(float left, float right) {
    if (!std::isfinite(left)) left = 0; if (!std::isfinite(right)) right = 0;
    history[0][static_cast<size_t>(position)] = left; history[1][static_cast<size_t>(position)] = right;
    position = (position + 1) % frameSize; filled = std::min(frameSize, filled + 1);
    if (vocalMode) {
        vocalEnergy[0] += .002*(left*left-vocalEnergy[0]);
        vocalEnergy[1] += .002*(right*right-vocalEnergy[1]);
        if (vocalEnergy[static_cast<size_t>(1-vocalChannel)] > 2*vocalEnergy[static_cast<size_t>(vocalChannel)]) {
            vocalChannel = 1-vocalChannel; voice.reset();
        }
    }
    if (vocalMode && voice.push(vocalChannel == 0 ? left : right)) {
        const auto m = voice.meter();
        if (m.state == TrackingState::tracking && m.inputHz > 0 && m.confidence >= .65f) {
            const double note = PitchEngine::frequencyToMidi(m.inputHz);
            const int n = static_cast<int>(std::lround(note));
            vocalFrame[static_cast<size_t>((n % 12 + 12) % 12)] += m.confidence;
            vocalWeight += m.confidence;
        }
    }
    if (++hop < hopSize) return false;
    hop = 0; if (filled < frameSize) return false;
    analyse(); return true;
}
void KeyAnalyser::spectrum(int channel) {
    for (int i = 0; i < frameSize; ++i) { re[static_cast<size_t>(i)] = history[static_cast<size_t>(channel)][static_cast<size_t>((position + i) % frameSize)] * window[static_cast<size_t>(i)]; im[static_cast<size_t>(i)] = 0; }
    for (int i = 1, j = 0; i < frameSize; ++i) {
        int bit = frameSize >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit;
        if (i < j) { std::swap(re[static_cast<size_t>(i)], re[static_cast<size_t>(j)]); std::swap(im[static_cast<size_t>(i)], im[static_cast<size_t>(j)]); }
    }
    for (int len = 2; len <= frameSize; len <<= 1) {
        const float cr = static_cast<float>(std::cos(-2 * pi / len)), ci = static_cast<float>(std::sin(-2 * pi / len));
        for (int start = 0; start < frameSize; start += len) {
            float wr = 1, wi = 0;
            for (int j = 0; j < len / 2; ++j) {
                const size_t a = static_cast<size_t>(start + j), b = a + static_cast<size_t>(len / 2);
                const float vr = wr * re[b] - wi * im[b], vi = wr * im[b] + wi * re[b];
                re[b] = re[a] - vr; im[b] = im[a] - vi; re[a] += vr; im[a] += vi;
                const float nr = wr * cr - wi * ci; wi = wr * ci + wi * cr; wr = nr;
            }
        }
    }
    for (size_t i = 0; i < power.size(); ++i) power[i] += re[i] * re[i] + im[i] * im[i];
}
void KeyAnalyser::analyse() {
    std::array<float, 12> chroma {};
    if (vocalMode) {
        if (vocalWeight > 0) for (size_t i = 0; i < chroma.size(); ++i) chroma[i] = vocalFrame[i] / vocalWeight;
        estimate(chroma, vocalWeight > 0); vocalFrame.fill(0); vocalWeight = 0; return;
    }
    double energy = 0;
    for (size_t i = 0; i < history[0].size(); ++i) energy += history[0][i] * history[0][i] + history[1][i] * history[1][i];
    if (energy / (2 * frameSize) < std::pow(10.0, gate / 10.0)) { estimate(chroma, false); return; }
    power.fill(0); spectrum(0); spectrum(1);
    double sum = 0, logs = 0; int count = 0;
    for (size_t i = 1; i < magnitude.size(); ++i) {
        magnitude[i] = std::sqrt(power[i]);
        if (i >= 20 && i < 1500) { sum += power[i] + 1e-20; logs += std::log(power[i] + 1e-20); ++count; }
    }
    const double flatness = std::exp(logs / count) / (sum / count);
    if (flatness > .30) { estimate(chroma, false); return; }
    struct Peak { double note; float weight; };
    std::array<Peak, 256> peaks {}; int peakCount = 0;
    const float maxMagnitude = *std::max_element(magnitude.begin(), magnitude.end());
    double tuneX = 0, tuneY = 0;
    for (int i = 19; i < 1535 && peakCount < static_cast<int>(peaks.size()); ++i) {
        const size_t b = static_cast<size_t>(i);
        if (magnitude[b] < maxMagnitude * .015f || magnitude[b] <= magnitude[b-1] || magnitude[b] < magnitude[b+1]) continue;
        const double a = std::log(magnitude[b-1] + 1e-12f), m = std::log(magnitude[b] + 1e-12f), c = std::log(magnitude[b+1] + 1e-12f);
        const double delta = std::clamp(.5 * (a - c) / (a - 2*m + c), -.5, .5);
        const double note = PitchEngine::frequencyToMidi((i + delta) * sampleRate / frameSize);
        const float weight = std::pow(magnitude[b] / std::max(maxMagnitude, 1e-9f), .75f);
        peaks[static_cast<size_t>(peakCount++)] = {note, weight};
        tuneX += weight * std::cos(2*pi*note); tuneY += weight * std::sin(2*pi*note);
    }
    if (peakCount < 1) { estimate(chroma, false); return; }
    const double frameTune = std::atan2(tuneY, tuneX) / (2*pi);
    tune = .98 * tune + .02 * std::clamp(frameTune, -.4, .4);
    std::array<float, bins> pitch {}, residual {};
    for (int i = 0; i < peakCount; ++i) {
        const auto p = peaks[static_cast<size_t>(i)]; const double n = p.note - tune - lowest;
        const int lo = static_cast<int>(std::floor(n)); const float part = static_cast<float>(n - lo);
        if (lo >= 0 && lo < bins) pitch[static_cast<size_t>(lo)] += p.weight * (1-part);
        if (lo+1 >= 0 && lo+1 < bins) pitch[static_cast<size_t>(lo+1)] += p.weight * part;
    }
    residual = pitch;
    std::array<float, notes> activations {};
    // Non-negative least squares: octave-resolved harmonic templates reduce
    // the false fifth/third contributions of upper partials to the chroma.
    for (int pass = 0; pass < 8; ++pass) for (int n = 0; n < notes; ++n) {
        const size_t k = static_cast<size_t>(n); float dot = 0;
        for (size_t b = 0; b < residual.size(); ++b) dot += templates[k][b] * residual[b];
        const float next = std::max(0.0f, activations[k] + dot / norm[k]); const float change = next - activations[k];
        activations[k] = next;
        for (size_t b = 0; b < residual.size(); ++b) residual[b] -= change * templates[k][b];
    }
    const float maximum = *std::max_element(activations.begin(), activations.end());
    for (int n = 0; n < notes; ++n) if (activations[static_cast<size_t>(n)] > maximum * .08f)
        chroma[static_cast<size_t>((lowest + n) % 12)] += activations[static_cast<size_t>(n)];
    const float total = std::accumulate(chroma.begin(), chroma.end(), 0.0f);
    if (total > 1e-6f) for (auto& x : chroma) x /= total;
    estimate(chroma, total > 1e-6f);
}
void KeyAnalyser::estimate(const std::array<float, 12>& frame, bool valid) {
    constexpr double dt = hopSize / sampleRate;
    latest.ready = false;
    if (!valid) {
        silence += dt; latest.confidence = 0;
        if (silence > memory) { average.fill(0); evidence = 0; posterior.fill(1.0/24); latest = {}; }
        return;
    }
    silence = 0; evidence = std::min(evidence + dt, 60.0);
    recent[static_cast<size_t>(recentIndex)] = frame; recentIndex = (recentIndex + 1) % 3;
    const double alpha = 1 - std::exp(-dt / memory);
    for (size_t c = 0; c < average.size(); ++c) {
        std::array<float, 3> x {recent[0][c], recent[1][c], recent[2][c]}; std::sort(x.begin(), x.end());
        average[c] += alpha * (x[1] - average[c]);
    }
    const double total = std::accumulate(average.begin(), average.end(), 0.0);
    if (total < 1e-8) return;
    double normChroma = 0; int distinct = 0;
    for (size_t c = 0; c < average.size(); ++c) {
        latest.chroma[c] = static_cast<float>(average[c] / total);
        normChroma += std::pow(latest.chroma[c] - 1.0/12, 2);
        if (latest.chroma[c] > .045f) ++distinct;
    }
    std::array<double, 24> scores {}, correlations {}, next {};
    for (int key = 0; key < 24; ++key) {
        double corr = 0, coverage = 0;
        for (int c = 0; c < 12; ++c) {
            corr += (latest.chroma[static_cast<size_t>(c)] - 1.0/12) * profiles[static_cast<size_t>(key)][static_cast<size_t>(c)];
            if (scaleMask(key) & (1u << c)) coverage += latest.chroma[static_cast<size_t>(c)];
        }
        corr /= std::max(1e-8, std::sqrt(normChroma));
        correlations[static_cast<size_t>(key)] = corr;
        scores[static_cast<size_t>(key)] = .65 * corr + .35 * (2*coverage-1);
        // Hand-designed causal 24-state filter, not a trained HMM.
        const double prior = .985 * posterior[static_cast<size_t>(key)] + .015/24;
        next[static_cast<size_t>(key)] = prior * std::exp(3.0 * scores[static_cast<size_t>(key)]);
    }
    const double sum = std::accumulate(next.begin(), next.end(), 0.0);
    for (size_t k = 0; k < posterior.size(); ++k) posterior[k] = next[k] / sum;
    const int best = static_cast<int>(std::max_element(posterior.begin(), posterior.end()) - posterior.begin());
    int second = best == 0 ? 1 : 0, otherScale = -1;
    for (int k = 0; k < 24; ++k) {
        if (k != best && posterior[static_cast<size_t>(k)] > posterior[static_cast<size_t>(second)]) second = k;
        if (scaleMask(k) != scaleMask(best) && (otherScale < 0 || scores[static_cast<size_t>(k)] > scores[static_cast<size_t>(otherScale)])) otherScale = k;
    }
    const double gap = std::max(0.0, scores[static_cast<size_t>(best)] - scores[static_cast<size_t>(otherScale)]);
    const double quality = std::clamp((correlations[static_cast<size_t>(best)] - .25) / .6, 0.0, 1.0);
    const double separation = std::clamp(gap / .16, 0.0, 1.0);
    const double diversity = std::clamp((distinct - 2) / 3.0, 0.0, 1.0);
    latest.key = best; latest.alternate = second;
    latest.confidence = static_cast<float>((.35*quality + .65*separation) * diversity * std::min(1.0, evidence / 3));
    latest.evidenceSeconds = static_cast<float>(evidence); latest.tuningCents = static_cast<float>(tune * 100);
    latest.ready = evidence >= 3 && distinct >= 4 && gap > .025 && latest.confidence >= .4f;
}
}
