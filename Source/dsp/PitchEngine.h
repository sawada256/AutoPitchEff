#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include "dsp/Formant.h"

namespace ape {
constexpr double pi = 3.14159265358979323846;
enum class TrackingState { silent, acquiring, tracking, holding };

struct Settings {
    float minHz = 180, maxHz = 1200;
    float retuneMs = 0, amount = 1, stabilityCents = 12;
    float threshold = 0.55f, gateDb = -48, holdMs = 18;
    float mix = 1, outputDb = 0, fallbackMs = 0;
    float maxShift = 4.5f; // semitones, 1-12. Larger shifts splice more often and move formants further.
    int formantMode = 0;       // FormantFilter::Mode: off, LPC, True Envelope, Auto
    float formantPreserve = 1; // 0: formants follow the pitch shift, 1: original formants kept
    float formantShift = 0;    // semitones applied to the formants after preservation
    float detuneCents = 0, humanizeCents = 0, wetDelayMs = 0; // chorus/double controls
    int detectorSource = 0; // 0: stereo sum, 1: left, 2: right (ignored for mono)
    int root = 0, scale = 0; // chromatic, major, minor
    unsigned noteMask = 0; // bit c allows pitch class c (C = bit 0); non-zero overrides root/scale
    bool enabled = true;
};
struct Meter {
    float inputHz = 0, targetHz = 0, confidence = 0, correctionCents = 0;
    float inputPeak = 0, outputPeak = 0, delayMs = 0;
    TrackingState state = TrackingState::silent;
    bool usingSidechain = false;
};

class Biquad {
public:
    void lowpass(double rate, double hz);
    void highpass(double rate, double hz);
    float process(float x) noexcept;
    void reset() noexcept { z1 = z2 = 0; }
private:
    void configure(double rate, double hz, bool high);
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
};

// All analysis is past-only. No inference threads, lookahead, or callback allocation.
class PitchDetector {
public:
    void prepare(double sampleRate);
    void reset() noexcept;
    void setSettings(const Settings& s);
    bool push(float x) noexcept;
    const Meter& meter() const noexcept { return result; }
    double rate() const noexcept { return analysisRate; }
    int hopSize() const noexcept { return hop; }
private:
    static constexpr int size = 2048;
    float at(int age, bool high = false) const noexcept;
    void analyse() noexcept;
    float correlation(int lag, int window, bool high) const noexcept;
    float harmonicSupport(float hz) const noexcept;
    void spectrum() noexcept;
    float refine(float hz) const noexcept;
    std::array<float, size> history {}, brightHistory {};
    std::array<float, 2048> real {}, imag {};
    std::array<float, 1025> magnitude {};
    std::array<float, 512> nsdf {};
    Biquad hp, lp1, lp2, emphasis, nativeEmphasis;
    std::vector<float> nativeHistory, nativeBrightHistory;
    int nativePosition = 0, nativeMask = 0;
    double nativeRate = 48000;
    int decimation = 3, decimationCount = 0, position = 0, filled = 0;
    int hop = 24, hopCount = 0, frameCount = 0, missed = 0;
    double analysisRate = 16000;
    float previousHz = 0, pendingHz = 0;
    int pendingCount = 0;
    Settings settings;
    Meter result;
};

// A resampling read head is spliced by one estimated period. This has a
// time-varying delay; fixedLatency() describes only the fixed interpolation guard.
class PeriodShifter {
public:
    void prepare(double sampleRate);
    void reset() noexcept;
    float process(float input, double ratio, double period) noexcept;
    std::array<float, 2> processFrame(std::array<float, 2> input, int channels, double ratio, double period) noexcept;
    double minimumReadAgeSamples() const noexcept { return blend < 1 ? std::min(delay, otherDelay) : delay; }
    // Read age weighted like the output crossfade, for aligning envelope frames.
    double readAgeSamples() const noexcept { return blend < 1 ? delay + (otherDelay - delay) * blend * blend * (3 - 2 * blend) : delay; }
    float currentDelayMs() const noexcept {
        const double oldest = blend < 1 && otherDelay > delay ? otherDelay : delay;
        return static_cast<float>(oldest * 1000 / sr);
    }
    static constexpr int fixedLatency() noexcept { return 8; }
    // Ratio clamp with half a semitone of headroom above the correction limit.
    void setRange(double semitones) noexcept { maxRatio = std::exp2((semitones + .5) / 12); minRatio = 1 / maxRatio; }
private:
    float read(double age, int channel = 0) const noexcept;
    double matchPeriod(double age, double period, int direction) const noexcept;
    std::array<std::vector<float>, 2> buffers;
    int activeChannels = 1;
    int write = 0, mask = 0;
    double sr = 48000, delay = 8, otherDelay = 8, blend = 1, blendStep = 1;
    double minRatio = .75, maxRatio = 1.34;
};

class PitchEngine {
public:
    void prepare(double sampleRate);
    void reset() noexcept;
    void setSettings(Settings s) noexcept;
    // External target (e.g. a held MIDI note), or -1 for scale correction.
    // By default the pitch class is used in the octave nearest the voice;
    // absolute uses the exact note (shift is still limited to 4.5 semitones).
    void setForcedNote(int note, bool absolute = false) noexcept { forcedNote = note; forcedAbsolute = absolute; }
    float process(float sample) noexcept;
    void processStereo(float& left, float& right) noexcept;
    void processExternal(float& left, float& right, int channels, float reference) noexcept;
    void processHybrid(float& left, float& right, int channels, float reference) noexcept;
    Meter meter() const noexcept;
    int latencySamples() const noexcept { return PeriodShifter::fixedLatency(); }
    static double frequencyToMidi(double hz) noexcept;
    static double midiToFrequency(double note) noexcept;
private:
    void updateTarget(Meter detected) noexcept;
    std::array<float, 2> processFrame(std::array<float, 2> input, int channels, float reference, bool hybrid = false) noexcept;
    int nearestNote(double note) const noexcept;
    bool allowed(int note) const noexcept;
    Settings settings;
    PitchDetector detector, sideDetector;
    double missingMs = 0;
    bool failedLastFrame = false;
    PeriodShifter shifter;
    FormantFilter formant;
    std::array<std::vector<float>, 2> wetDelay;
    int wetDelayPosition = 0, wetDelayMask = 0, wetDelaySamples = 0;
    double drift = 0, driftTarget = 0, driftCoeff = 0, formantWarp = 1;
    double levelIn = 0, levelOut = 0, levelGain = 1, levelCoeff = 0;
    int driftCountdown = 0;
    uint32_t randomState = 0x9E3779B9u;
    std::array<std::array<float, 16>, 2> dryHistory {};
    int dryPosition = 0;
    double sr = 48000, targetNote = -1, wantedRatio = 1, ratio = 1, wetGain = 0, gain = 1;
    double ratioCoeff = 0, fadeCoeff = 0, gainCoeff = 0, outputGain = 1;
    double decisionNote = -1, decisionCoeff = 0;
    int candidateNote = -1, candidateFrames = 0, forcedNote = -1;
    bool forcedAbsolute = false;
    Meter latest;
};
}
