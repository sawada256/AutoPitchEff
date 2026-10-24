#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "KeyAnalyser.h"
#include "ScaleLink.h"
class AutoScaleLinkProcessor final : public juce::AudioProcessor, private juce::Thread {
public:
    AutoScaleLinkProcessor();
    ~AutoScaleLinkProcessor() override;
    void prepareToPlay(double, int) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "AutoScaleLink"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Live Key Detection"; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    ape::KeyResult readResult() const noexcept;
    int appliedKey() const noexcept { return applied.load(); }
    int linkStatus() const noexcept { return status.load(); }
    uint64_t droppedSamples() const noexcept { return dropped.load(); }
    void approve(int key) noexcept { if (key >= 0 && key < 24) approval.store(key); }
    void resetAnalysis() noexcept { resetRequested.store(true); }
    juce::AudioProcessorValueTreeState parameters;
private:
    void run() override;
    void publishResult(const ape::KeyResult&) noexcept;
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    static constexpr size_t capacity = 65536;
    std::array<std::array<float, 2>, capacity> queue {};
    std::atomic<uint64_t> write {0}, read {0}, dropped {0}, lastAudio {0};
    std::atomic<int> applied {-1}, approval {-1}, status {0};
    std::atomic<bool> resetRequested {false};
    std::array<std::atomic<float>, 18> display {};
    std::array<std::atomic<float>*, 8> values {};
    std::array<std::array<ape::Biquad, 4>, 2> filters;
    double resampleStep = .25, resamplePhase = 0;
    ape::KeyAnalyser analyser;
    ape::ScaleLink link;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutoScaleLinkProcessor)
};
