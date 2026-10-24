#include "KeyEditor.h"
#if JUCE_MAC
#include "NativeVisibility.h"
#endif
namespace {
const juce::Colour bg {0xff11171d}, panel {0xff1b242c}, text {0xffedf3f6}, muted {0xff99aab7}, mint {0xff83e0bf};
juce::String keyName(int key) {
    static const char* names[] = {"C","C#","D","Eb","E","F","F#","G","Ab","A","Bb","B"};
    return key >= 0 && key < 24 ? juce::String(names[key%12]) + (key < 12 ? " major" : " minor") : "Waiting";
}
}
AutoScaleLinkEditor::AutoScaleLinkEditor(AutoScaleLinkProcessor& p) : AudioProcessorEditor(p), owner(p) {
    look.setColour(juce::ComboBox::backgroundColourId,panel); look.setColour(juce::ComboBox::textColourId,text);
    look.setColour(juce::PopupMenu::backgroundColourId,panel); look.setColour(juce::PopupMenu::textColourId,text);
    look.setColour(juce::TextButton::buttonColourId,panel); look.setColour(juce::TextButton::textColourOffId,text);
    look.setColour(juce::ToggleButton::textColourId,text); look.setColour(juce::ToggleButton::tickColourId,mint);
    look.setColour(juce::Slider::textBoxTextColourId,text); look.setColour(juce::Slider::trackColourId,mint);
    setLookAndFeel(&look); setOpaque(true);
    mode.addItemList({"Approve","Automatic"},1); source.addItemList({"Mix / Instruments","Vocal (150-1400 Hz)"},1);
    for (int i = 1; i <= 8; ++i) group.addItem(juce::String(i),i);
    modeLabel.setText("APPLY MODE",juce::dontSendNotification); sourceLabel.setText("ANALYSE",juce::dontSendNotification); groupLabel.setText("LINK GROUP",juce::dontSendNotification);
    for (auto* label : {&modeLabel,&sourceLabel,&groupLabel}) { label->setColour(juce::Label::textColourId,muted); label->setFont(small); addAndMakeVisible(label); }
    for (auto* c : std::initializer_list<juce::Component*>{&mode,&source,&group,&transmit,&apply,&reset}) { c->setBufferedToImage(true); addAndMakeVisible(c); }
    modeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.parameters,"mode",mode);
    sourceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.parameters,"analysis",source);
    groupAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.parameters,"group",group);
    transmitAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.parameters,"transmit",transmit);
    const char* ids[] = {"memory","confidence","confirm","gate"};
    const char* names[] = {"Analysis memory","Auto confidence","Auto confirmation","Analysis gate"};
    const char* suffix[] = {" s"," %"," s"," dB"};
    const char* tips[] = {"Past-only evidence smoothing. Longer is steadier but follows modulation later.","Heuristic score, not probability of correctness. Only used in Automatic mode.","Continuous stable evidence before Automatic applies a scale.","Quiet audio is excluded from analysis. The audio output is never gated."};
    for (size_t i = 0; i < sliders.size(); ++i) {
        sliders[i].setSliderStyle(juce::Slider::LinearHorizontal); sliders[i].setTextBoxStyle(juce::Slider::TextBoxRight,false,82,24);
        sliders[i].setTextValueSuffix(suffix[i]); sliders[i].setTooltip(tips[i]); sliders[i].setBufferedToImage(true);
        labels[i].setText(names[i],juce::dontSendNotification); labels[i].setColour(juce::Label::textColourId,muted); labels[i].setFont(small);
        addAndMakeVisible(sliders[i]); addAndMakeVisible(labels[i]);
        sliderAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.parameters,ids[i],sliders[i]);
    }
    for (int c=0;c<12;++c) {
        auto& bar=pitchBars[static_cast<size_t>(c)];
        bar.setColour(juce::TextButton::buttonColourId,juce::Colours::transparentBlack);
        bar.setColour(juce::TextButton::buttonOnColourId,juce::Colours::transparentBlack);
        bar.setColour(juce::ComboBox::outlineColourId,juce::Colours::transparentBlack);
        bar.setName("Approve root " + keyName(c).upToFirstOccurrenceOf(" major",false,false));
        bar.setMouseCursor(juce::MouseCursor::PointingHandCursor);
        addAndMakeVisible(bar);
        bar.onClick=[this,c] {
            if (result.key>=0 && result.evidenceSeconds>=1)
                owner.approve(c+(result.key>=12 ? 12 : 0));
        };
    }
    apply.onClick = [this] { owner.approve(result.key); };
    reset.onClick = [this] { owner.resetAnalysis(); };
    apply.setTooltip("Apply the displayed candidate once. Use Approve mode to prevent subsequent automatic changes.");
    reset.setTooltip("Clear observation history; keep the last applied scale.");
    group.setTooltip("Match AutoPitchEff's group. One sender per group, across this macOS user account.");
    source.setTooltip("Mix is preferred for a whole song or accompaniment. Vocal observes voiced melody only; key may remain ambiguous.");
    result = owner.readResult(); sent = owner.appliedKey(); status = owner.linkStatus();
    refreshApprovalControls();
    setSize(800,590); startTimerHz(10);
}
AutoScaleLinkEditor::~AutoScaleLinkEditor() { stopTimer(); setLookAndFeel(nullptr); }
void AutoScaleLinkEditor::refreshApprovalControls() {
    const bool ready=result.key>=0 && result.evidenceSeconds>=1;
    apply.setEnabled(ready);
    const int nextMode=ready ? (result.key>=12 ? 1 : 0) : -1;
    if (nextMode==barMode) return;
    barMode=nextMode;
    for(int c=0;c<12;++c) {
        auto& bar=pitchBars[static_cast<size_t>(c)];
        bar.setEnabled(ready);
        bar.setTooltip(ready ? "Approve " + keyName(c+(result.key>=12 ? 12 : 0)) + ". Uses the displayed candidate's major/minor mode. Automatic mode may subsequently change it."
                             : "Wait for a candidate before approving a root. Bars show pitch-class energy, not key probabilities.");
    }
}
void AutoScaleLinkEditor::resized() {
    for(int c=0;c<12;++c) pitchBars[static_cast<size_t>(c)].setBounds(389+30*c,176,26,118);
    modeLabel.setBounds(24,76,110,22); mode.setBounds(24,102,160,30);
    sourceLabel.setBounds(208,76,190,22); source.setBounds(208,102,250,30);
    groupLabel.setBounds(484,76,108,22); group.setBounds(484,102,76,30); transmit.setBounds(602,102,165,30);
    apply.setBounds(24,325,218,34); reset.setBounds(558,325,218,34);
    for (size_t i = 0; i < sliders.size(); ++i) {
        const int x = 24 + static_cast<int>(i%2)*390, y = 385+static_cast<int>(i/2)*70;
        labels[i].setBounds(x,y,330,20); sliders[i].setBounds(x,y+23,360,30);
    }
}
void AutoScaleLinkEditor::timerCallback() {
    bool visible = isShowing();
#if JUCE_MAC
    if (visible) { auto* peer = getPeer(); visible = peer && apeIsNativeViewVisible(peer->getNativeHandle()); }
#endif
    if (!visible) { if (getTimerInterval()!=250) startTimer(250); return; }
    if (getTimerInterval()!=100) startTimerHz(10);
    result = owner.readResult(); sent = owner.appliedKey(); status = owner.linkStatus();
    refreshApprovalControls();
    repaint(24,152,752,155); repaint(250,325,296,34); repaint(24,540,752,30);
}
void AutoScaleLinkEditor::paint(juce::Graphics& g) {
    g.fillAll(bg); g.setColour(text); g.setFont(juce::FontOptions(26,juce::Font::bold));
    g.drawText("AutoScaleLink",24,20,300,38,juce::Justification::centredLeft);
    g.setFont(small); g.setColour(muted); g.drawText("LIVE KEY ANALYSIS  /  ZERO AUDIO DELAY",370,24,406,30,juce::Justification::centredRight);
    g.setColour(panel); g.fillRoundedRectangle(24,152,752,155,10);
    g.setFont(small); g.setColour(muted); g.drawText("CANDIDATE",44,166,250,20,juce::Justification::centredLeft);
    g.setColour(mint); g.setFont(large); g.drawText(keyName(result.key),44,189,300,48,juce::Justification::centredLeft);
    g.setFont(small); g.setColour(muted);
    g.drawText("Alternate: "+keyName(result.alternate),44,242,320,22,juce::Justification::centredLeft);
    g.drawText("Score "+juce::String(static_cast<int>(std::lround(result.confidence*100)))+"%  |  Evidence "+juce::String(result.evidenceSeconds,1)+" s",44,268,320,20,juce::Justification::centredLeft);
    static const char* names[] = {"C","C#","D","Eb","E","F","F#","G","Ab","A","Bb","B"};
    const float maximum = std::max(.001f,*std::max_element(result.chroma.begin(),result.chroma.end()));
    for (int c = 0; c < 12; ++c) {
        const float x = 392.0f+30.0f*static_cast<float>(c), height = 82*result.chroma[static_cast<size_t>(c)]/maximum;
        g.setColour(sent >= 0 && sent%12 == c ? juce::Colour(0xffb7a4ff) : mint.withAlpha(.7f)); g.fillRect(x,264-height,20.0f,height);
        g.setColour(sent >= 0 && sent%12 == c ? juce::Colour(0xffb7a4ff) : muted); g.drawText(names[c],static_cast<int>(x)-3,271,26,20,juce::Justification::centred);
    }
    g.setColour(text); g.drawText("Applied: "+keyName(sent),250,325,296,34,juce::Justification::centred);
    const char* statuses[] = {"Not sending / waiting for audio","Sending to AutoPitchEff","Group occupied: use another group","Shared memory unavailable"};
    g.setColour(status >= 2 ? juce::Colour(0xffffb088) : muted);
    g.drawText(statuses[std::clamp(status,0,3)],24,540,550,24,juce::Justification::centredLeft);
    g.setColour(muted); g.drawText("v" JucePlugin_VersionString,650,540,126,24,juce::Justification::centredRight);
}
