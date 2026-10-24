#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/PitchEngine.h"
#include "dsp/MidiScale.h"
#include "key/ScaleLink.h"

class AutoPitchEffProcessor final : public juce::AudioProcessor, private juce::AsyncUpdater, private juce::AudioProcessorValueTreeState::Listener {
public:
    AutoPitchEffProcessor();
    ~AutoPitchEffProcessor() override;
    void prepareToPlay(double, int) override;
    void releaseResources() override {}
    void reset() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "AutoPitchEff"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.09; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "High Female / Hard Tune"; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    juce::AudioProcessorParameter* getBypassParameter() const override;
    ape::Meter readMeter() const noexcept;
    void resetPreset();
    int linkedKey() const noexcept { return acceptedKey.load(); }
    bool linkConnected() const noexcept { return connected.load(); }
    unsigned midiScaleMask() const noexcept { return midiMask.load(); }
    int midiScaleRoot() const noexcept { return midiRoot.load(); }
    int midiTargetNote() const noexcept { return targetNote.load(); }
    // MIDI note number of "C0" for the selected octave naming.
    int midiScaleBase() const noexcept { return values[22]->load() > .5f ? 12 : 24; }
    juce::AudioProcessorValueTreeState parameters;
private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    void process(juce::AudioBuffer<float>&, juce::MidiBuffer&, bool bypassed);
    ape::PitchEngine engine;
    static constexpr int parameterCount = 33;
    void parameterChanged(const juce::String&, float) override;
    void handleAsyncUpdate() override;
    std::atomic<int> alignmentSamples {0};
    std::atomic<double> preparedRate {48000};
    std::array<std::vector<float>, 2> alignmentBuffer;
    int alignmentPosition = 0, lastAlignment = -1;
    int lastDetectionMode = -1;
    ape::ScaleLink scaleLink;
    std::atomic<int> acceptedKey {-1};
    std::atomic<bool> connected {false};
    std::atomic<int> lastGroup {-1};
    void readMidiScale(const juce::MidiBuffer&, int channel, int excludedChannel, int base) noexcept;
    void readMidiTarget(const juce::MidiBuffer&, int channel) noexcept;
    ape::MidiTarget midiTarget;
    std::atomic<int> targetNote {-1};
    std::atomic<unsigned> midiMask {0};
    std::atomic<int> midiRoot {-1};
    unsigned lastNoteMask = 0;
    std::array<std::atomic<float>*, parameterCount> values {};
    std::array<std::atomic<float>, 10> meters {};
    std::array<float, parameterCount> lastSettings {};
    bool settingsValid = false;
    static_assert(std::atomic<float>::is_always_lock_free, "Meter publication must not lock the audio thread");
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutoPitchEffProcessor)
};
