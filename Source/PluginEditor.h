#pragma once
#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

class AutoPitchEffEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit AutoPitchEffEditor(AutoPitchEffProcessor&);
    ~AutoPitchEffEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    struct Look final : juce::LookAndFeel_V4 {
        void drawRotarySlider(juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
    } look;
    struct Control {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    void timerCallback() override;
    void refreshMeter();
    void rebuildCurves();
    AutoPitchEffProcessor& owner;
    std::array<Control, 10> controls;
    juce::ToggleButton hybrid {"Hybrid (Main first)"};
    juce::Slider fallback;
    juce::Label fallbackLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> hybridAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> fallbackAttachment;
    juce::ToggleButton unlockAlignment {"Unlock alignment delay"};
    juce::Slider alignment;
    juce::ComboBox pitchInput;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> alignmentAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> pitchInputAttachment;
    juce::ComboBox key, scale, detectorSource, linkGroup;
    juce::Label keyLabel, scaleLabel, detectorLabel, linkGroupLabel;
    juce::ToggleButton receiveScale {"Receive Scale"};
    juce::ToggleButton midiKey {"MIDI Key"};
    juce::TextButton midiHelp {"?"};
    juce::ComboBox midiChannel, midiOctave;
    juce::ToggleButton midiTarget {"MIDI Target"};
    juce::TextButton midiTargetHelp {"?"};
    juce::ComboBox midiTargetChannel, midiTargetOctave;
    juce::Slider maxShift;
    juce::Label maxShiftLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> maxShiftAttachment;
    juce::Label formantLabel;
    juce::ComboBox formantMode;
    juce::TextButton formantHelp {"?"};
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> formantModeAttachment;
    struct Row {
        juce::Label label;
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    std::array<Row, 5> chorusRows; // formant preserve, formant shift, detune, humanize, double delay
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> midiTargetAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> midiTargetChannelAttachment, midiTargetOctaveAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> midiKeyAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> midiChannelAttachment, midiOctaveAttachment;
    void refreshMidiHelp();
    juce::ToggleButton bypass {"Bypass"};
    juce::TextButton preset {"High Female / Hard Tune"};
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyAttachment, scaleAttachment, detectorAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> receiveAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> groupAttachment;
    juce::TooltipWindow tooltips {this, 600};
    ape::Meter meter;
    std::array<float, 220> inputHistory {}, targetHistory {};
    int historyPosition = 0;
    juce::Path inputCurve, targetCurve;
    const juce::Font titleFont {juce::FontOptions(26, juce::Font::bold)};
    const juce::Font noteFont {juce::FontOptions(43, juce::Font::bold)};
    const juce::Font smallFont {juce::FontOptions(11)}, normalFont {juce::FontOptions(12)}, valueFont {juce::FontOptions(13)};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutoPitchEffEditor)
};
