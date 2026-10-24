#pragma once
#include "dsp/PitchEngine.h"
#include <array>

namespace ape {
struct KeyResult {
    int key = -1, alternate = -1;
    float confidence = 0, evidenceSeconds = 0, tuningCents = 0;
    bool ready = false;
    std::array<float, 12> chroma {};
};
// Worker-thread only. 12 kHz, causal analysis, no audio output.
class KeyAnalyser {
public:
    static constexpr int frameSize = 4096, hopSize = 512;
    static constexpr double sampleRate = 12000;
    KeyAnalyser();
    void reset();
    void configure(bool vocal, float memorySeconds, float gateDb);
    bool push(float left, float right);
    const KeyResult& result() const { return latest; }
    static unsigned scaleMask(int key) noexcept;
private:
    void analyse();
    void spectrum(int channel);
    void estimate(const std::array<float, 12>& frame, bool valid);
    static constexpr int lowest = 33, bins = 82, notes = 64;
    std::array<std::array<float, frameSize>, 2> history {};
    std::array<float, frameSize> re {}, im {}, window {};
    std::array<float, frameSize / 2 + 1> magnitude {}, power {};
    std::array<std::array<float, bins>, notes> templates {};
    std::array<float, notes> norm {};
    std::array<std::array<float, 12>, 24> profiles {};
    std::array<double, 24> posterior {};
    std::array<double, 12> average {};
    std::array<std::array<float, 12>, 3> recent {};
    PitchDetector voice;
    KeyResult latest;
    int position = 0, filled = 0, hop = 0, recentIndex = 0;
    bool vocalMode = false;
    float memory = 8, gate = -55;
    double evidence = 0, silence = 0, tune = 0;
    std::array<float, 12> vocalFrame {};
    float vocalWeight = 0;
    std::array<double,2> vocalEnergy {};
    int vocalChannel = 0;
};
}
