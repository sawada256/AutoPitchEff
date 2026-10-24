#pragma once
#include "KeyProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
class AutoScaleLinkEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit AutoScaleLinkEditor(AutoScaleLinkProcessor&);
    ~AutoScaleLinkEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    void refreshApprovalControls();
    std::array<juce::TextButton,12> pitchBars;
    AutoScaleLinkProcessor& owner;
    juce::LookAndFeel_V4 look;
    juce::ComboBox mode, source, group;
    juce::Label modeLabel, sourceLabel, groupLabel;
    juce::ToggleButton transmit {"Send Scale"};
    juce::TextButton apply {"Apply candidate"}, reset {"Reset analysis"};
    std::array<juce::Slider,4> sliders;
    std::array<juce::Label,4> labels;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>,4> sliderAttachments;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment, sourceAttachment, groupAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> transmitAttachment;
    juce::TooltipWindow tooltips {this,600};
    ape::KeyResult result;
    int sent = -1, status = 0, barMode = -2;
    const juce::Font small {juce::FontOptions(13)}, large {juce::FontOptions(36,juce::Font::bold)};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutoScaleLinkEditor)
};
