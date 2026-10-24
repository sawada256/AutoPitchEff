#include "PluginEditor.h"
#include <cmath>
#if JUCE_MAC
#include "NativeVisibility.h"
#endif

namespace {
const juce::Colour bg {0xff11171d}, panel {0xff1b242c}, text {0xffedf3f6}, muted {0xff99aab7}, mint {0xff83e0bf}, purple {0xffb8a6fb};
const char* pitchNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
const char* minorNames[] = {"A", "A#", "B", "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#"};
juce::String noteName(float hz) {
    if (hz <= 0) return "--";
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const auto n = static_cast<int>(std::lround(ape::PitchEngine::frequencyToMidi(hz)));
    return juce::String(names[(n % 12 + 12) % 12]) + juce::String(n / 12 - 1);
}
}
void AutoPitchEffEditor::Look::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float value, float start, float end, juce::Slider&) {
    const auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h)).reduced(9);
    const auto radius = std::min(bounds.getWidth(), bounds.getHeight()) * .5f;
    const auto centre = bounds.getCentre();
    juce::Path base, active;
    base.addCentredArc(centre.x, centre.y, radius, radius, 0, start, end, true);
    active.addCentredArc(centre.x, centre.y, radius, radius, 0, start, start + value * (end - start), true);
    g.setColour(juce::Colour(0xff35434e)); g.strokePath(base, juce::PathStrokeType(4));
    g.setColour(mint); g.strokePath(active, juce::PathStrokeType(4));
    const float angle = start + value * (end - start);
    g.drawLine(centre.x + std::sin(angle) * radius * .50f, centre.y - std::cos(angle) * radius * .50f,
               centre.x + std::sin(angle) * radius * .80f, centre.y - std::cos(angle) * radius * .80f, 2);
}
AutoPitchEffEditor::AutoPitchEffEditor(AutoPitchEffProcessor& p) : AudioProcessorEditor(p), owner(p) {
    look.setColour(juce::Slider::textBoxTextColourId, text);
    look.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    look.setColour(juce::Slider::textBoxBackgroundColourId, panel);
    look.setColour(juce::ComboBox::backgroundColourId, panel);
    look.setColour(juce::ComboBox::textColourId, text);
    look.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff35434e));
    look.setColour(juce::PopupMenu::backgroundColourId, panel);
    look.setColour(juce::PopupMenu::textColourId, text);
    look.setColour(juce::TextButton::buttonColourId, panel);
    look.setColour(juce::TextButton::textColourOffId, text);
    look.setColour(juce::ToggleButton::textColourId, text);
    look.setColour(juce::ToggleButton::tickColourId, mint);
    setLookAndFeel(&look);
    setOpaque(true);
    inputHistory.fill(-1); targetHistory.fill(-1);
    inputCurve.preallocateSpace(4096); targetCurve.preallocateSpace(4096);
    const char* ids[] = {"retune", "amount", "stability", "mix", "output", "minHz", "maxHz", "threshold", "gate", "hold"};
    const char* names[] = {"Retune", "Correction", "Note Stability", "Dry / Wet", "Output", "Range Low", "Range High", "Threshold", "Detection Gate", "Pitch Hold"};
    const char* suffixes[] = {" ms", " %", " ct", " %", " dB", " Hz", " Hz", "", " dB", " ms"};
    const char* tips[] = {
        "0 ms gives hard tuning. This is correction speed, not total audio latency.",
        "100% removes detected pitch deviations. 0% leaves pitch unchanged.",
        "Controls note-boundary hysteresis, note-decision smoothing and confirmation time. Increase for flatter held notes; reduce for faster semitone changes.",
        "Use 100% for the strongest hard-tune sound. Dry and shifted audio can phase at intermediate settings.",
        "Output gain. Reduce if the output peak turns red.",
        "Lowest expected singing pitch. 180 Hz is the starting point for high female vocals.",
        "Highest expected singing pitch. Keep this higher than Range Low.",
        "Minimum periodicity. Lower accepts more noisy input; higher rejects more instrument bleed. This is not a trained voice classifier.",
        "Detection-only gate. Quiet consonants and breaths still pass through the audio path.",
        "Keep the last pitch through brief tracking failures. Long holds may tune instrument bleed."
    };
    for (size_t i = 0; i < controls.size(); ++i) {
        auto& c = controls[i];
        c.slider.setBufferedToImage(true); c.label.setBufferedToImage(true);
        c.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        c.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 112, 22);
        c.slider.setTextValueSuffix(suffixes[i]); c.slider.setTooltip(tips[i]);
        c.label.setText(names[i], juce::dontSendNotification); c.label.setJustificationType(juce::Justification::centred);
        c.label.setColour(juce::Label::textColourId, muted); c.label.setFont(juce::FontOptions(13));
        addAndMakeVisible(c.slider); addAndMakeVisible(c.label);
        c.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.parameters, ids[i], c.slider);
    }
    detectorSource.addItemList({"Sum (L+R)/2", "Left", "Right"}, 1);
    detectorLabel.setText("DETECT", juce::dontSendNotification);
    detectorLabel.setFont(smallFont); detectorLabel.setColour(juce::Label::textColourId, muted);
    addAndMakeVisible(detectorLabel); addAndMakeVisible(detectorSource);
    detectorSource.setTooltip("Stereo only: one detected pitch and linked correction for both channels. Sum matches Waves' detection source. Use Left or Right if opposite polarity cancels the sum. Ignored for mono.");
    detectorAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.parameters, "detectorSource", detectorSource);
    key.addItemList({"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}, 1);
    scale.addItemList({"Chromatic", "Major", "Minor"}, 1);
    keyLabel.setText("KEY", juce::dontSendNotification); scaleLabel.setText("SCALE", juce::dontSendNotification);
    for (auto* label : {&keyLabel, &scaleLabel}) { label->setColour(juce::Label::textColourId, muted); label->setFont(juce::FontOptions(11)); addAndMakeVisible(label); }
    for (auto* c : std::initializer_list<juce::Component*>{&key, &scale, &bypass, &preset}) addAndMakeVisible(c);
    keyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.parameters, "root", key);
    scaleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.parameters, "scale", scale);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.parameters, "bypass", bypass);
    preset.onClick = [&p] { p.resetPreset(); };
    preset.setTooltip("Reset tuning/scale controls to the high female preset. Keep pitch input and alignment delay.");
    scale.setTooltip("Chromatic automatically uses all twelve notes. No MIDI or melody input is required.");
    scale.onChange = [this] { key.setEnabled(scale.getSelectedId() != 1); };
    key.setEnabled(scale.getSelectedId() != 1);
    for (int i = 1; i <= 8; ++i) linkGroup.addItem(juce::String(i),i);
    linkGroupLabel.setText("GROUP",juce::dontSendNotification); linkGroupLabel.setColour(juce::Label::textColourId,muted); linkGroupLabel.setFont(smallFont);
    addAndMakeVisible(receiveScale); addAndMakeVisible(linkGroup); addAndMakeVisible(linkGroupLabel);
    receiveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.parameters,"receiveScale",receiveScale);
    groupAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.parameters,"linkGroup",linkGroup);
    receiveScale.setTooltip("Use AutoScaleLink's last approved/automatic scale. Turn off to restore manual Key/Scale. If sender stops, last received scale is held.");
    linkGroup.setTooltip("Match the sender's group. Different sessions should use different groups.");
    pitchInput.addItemList({"Pitch: Main", "Pitch: Sidechain"},1);
    addAndMakeVisible(pitchInput); addAndMakeVisible(unlockAlignment); addAndMakeVisible(alignment);
    pitchInputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.parameters,"pitchInput",pitchInput);
    alignment.setSliderStyle(juce::Slider::LinearHorizontal);
    alignment.setTextBoxStyle(juce::Slider::TextBoxRight,false,80,24);
    alignment.setTextValueSuffix(" ms"); alignment.setEnabled(false);
    alignmentAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.parameters,"alignmentMs",alignment);
    unlockAlignment.onClick = [this] { alignment.setEnabled(unlockAlignment.getToggleState()); };
    alignment.setTooltip("Advanced: delay MAIN audio to match a delayed vocal-extraction sidechain. Reported to host, plus 8 samples. Change while playback is stopped; host compensation may add its own routing delay.");
    pitchInput.setTooltip("Sidechain detects pitch from the external input and corrects main audio. Enable and route the host sidechain first. A missing/silent sidechain leaves main uncorrected.");
    addAndMakeVisible(hybrid); addAndMakeVisible(fallback); addAndMakeVisible(fallbackLabel);
    fallbackLabel.setText("Fallback after",juce::dontSendNotification); fallbackLabel.setColour(juce::Label::textColourId,muted);
    fallback.setSliderStyle(juce::Slider::LinearHorizontal); fallback.setTextBoxStyle(juce::Slider::TextBoxRight,false,80,24); fallback.setTextValueSuffix(" ms");
    hybridAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.parameters,"hybridDetection",hybrid);
    fallbackAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.parameters,"fallbackMs",fallback);
    auto refreshHybrid=[this] { pitchInput.setEnabled(!hybrid.getToggleState()); fallback.setEnabled(hybrid.getToggleState()); };
    hybrid.onClick=refreshHybrid; refreshHybrid();
    hybrid.setTooltip("Main-first detection on aligned main audio, with a continuously tracked sidechain fallback. Main resumes at the next successful detection frame.");
    fallback.setTooltip("Continuous failed MAIN detection before using sidechain, 0-200 ms. 0 switches on the first failed analysis frame (about 1.5 ms hop), not before the detector can measure pitch. Pitch Hold is not a successful detection.");
    midiChannel.addItemList({"Any channel"},1);
    for (int c = 1; c <= 16; ++c) midiChannel.addItem("MIDI Ch " + juce::String(c),c+1);
    midiOctave.addItemList({"C3 = note 60", "C4 = note 60"},1);
    for (auto* c : std::initializer_list<juce::Component*>{&midiKey,&midiHelp,&midiChannel,&midiOctave}) addAndMakeVisible(c);
    midiKeyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.parameters,"midiKey",midiKey);
    midiChannelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.parameters,"midiKeyChannel",midiChannel);
    midiOctaveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.parameters,"midiOctave",midiOctave);
    midiKey.setTooltip("Change the scale in real time from MIDI notes. Hover ? for the note map.");
    midiChannel.setTooltip("Only note-ons on this channel change the scale.");
    midiOctave.setTooltip("Octave naming used by your DAW. Cubase, Studio One and Logic name note 60 C3; others name it C4.");
    midiOctave.onChange = [this] { refreshMidiHelp(); };
    for (int c = 1; c <= 16; ++c) midiTargetChannel.addItem("MIDI Ch " + juce::String(c),c);
    midiTargetOctave.addItemList({"Nearest", "Absolute"},1);
    for (auto* c : std::initializer_list<juce::Component*>{&midiTarget,&midiTargetHelp,&midiTargetChannel,&midiTargetOctave}) addAndMakeVisible(c);
    midiTargetOctaveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.parameters,"midiTargetOctave",midiTargetOctave);
    midiTargetOctave.setTooltip("Nearest octave: the note name in the octave closest to the voice. Absolute: the exact note played.");
    midiTargetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.parameters,"midiTarget",midiTarget);
    midiTargetChannelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.parameters,"midiTargetChannel",midiTargetChannel);
    midiTarget.setTooltip("Correct to the held MIDI note instead of the scale. Hover ? for details.");
    midiTargetChannel.setTooltip("Only notes on this channel set the target.");
    midiTargetHelp.setTooltip(
        "MIDI Target: while a note is held on the selected channel (default Ch 1), the voice is corrected to that note, "
        "ignoring Key/Scale, AutoScaleLink and MIDI Key.\n\n"
        "Nearest octave: the note's pitch name is used in the octave nearest the voice, so you can play the melody in any octave.\n"
        "Absolute: the exact note played is the target (play it at the sung pitch).\n"
        "Either way the shift is limited by Max pitch shift (below); a farther absolute note pulls the voice only that far toward it.\n\n"
        "With several notes held, the newest one wins; releasing it returns to the previous held note. "
        "When no note is held, normal scale correction resumes.\n\n"
        "If MIDI Key listens to Any channel, notes on the target channel are not used as key notes.");
    maxShiftLabel.setText("Max pitch shift",juce::dontSendNotification); maxShiftLabel.setColour(juce::Label::textColourId,muted);
    maxShift.setSliderStyle(juce::Slider::LinearHorizontal); maxShift.setTextBoxStyle(juce::Slider::TextBoxRight,false,80,24); maxShift.setTextValueSuffix(" st");
    addAndMakeVisible(maxShiftLabel); addAndMakeVisible(maxShift);
    maxShiftAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.parameters,"maxShift",maxShift);
    maxShift.setTooltip("Largest pitch shift, 1-12 semitones (default 4.5). Raise it for MIDI Target harmonies or chorus parts. "
                        "Pitch stays accurate, but this shifter moves formants with the pitch: large upward shifts sound thinner/chipmunk-like and downward ones darker, "
                        "with more frequent splices on unsteady voices. Scale correction never needs more than about 2 semitones, so the default keeps the most natural sound.");
    formantLabel.setText("Formant",juce::dontSendNotification); formantLabel.setColour(juce::Label::textColourId,muted);
    formantMode.addItemList({"Off", "LPC", "True Envelope", "Auto (by pitch)"},1);
    for (auto* c : std::initializer_list<juce::Component*>{&formantLabel,&formantMode,&formantHelp}) addAndMakeVisible(c);
    formantModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.parameters,"formantMode",formantMode);
    formantMode.setTooltip("Keep the singer's formants (vocal-tract resonances) while shifting pitch. No added latency. Change while stopped: switching resets the shifter.");
    formantHelp.setTooltip(
        "Formant algorithms (all add no latency):\n"
        "LPC: linear-prediction envelope. Most accurate for F0 below ~500 Hz and the lightest on CPU.\n"
        "True Envelope: iterative cepstral envelope that rides on the harmonic peaks. Best for high voices (F0 above ~500 Hz); about 3x the CPU of LPC.\n"
        "Auto: LPC below 500 Hz, True Envelope above.\n\n"
        "Preserve: 100% keeps the original formants; 0% lets them follow the pitch like Off.\n"
        "Formant Shift: moves the formants independently (+ smaller/brighter, - larger/darker). Use different values per harmony to separate voices.\n\n"
        "Chorus tips: one instance per harmony with MIDI Target, Dry/Wet 100%, Retune 20-60 ms for a sung (not robotic) feel, "
        "Detune +/-5-12 ct and Humanize 3-8 ct for natural spread, Double Delay 10-30 ms to loosen timing.");
    const char* rowIds[] = {"formantPreserve","formantShift","detune","humanize","wetDelay"};
    const char* rowNames[] = {"Preserve","Formant shift","Detune","Humanize","Double delay"};
    const char* rowSuffixes[] = {" %"," st"," ct"," ct"," ms"};
    const char* rowTips[] = {
        "How much of the original formants to keep while shifting. 100% = natural voice, 0% = formants follow the pitch (as with Formant Off).",
        "Moves the formants by semitones independently of pitch. + sounds smaller/brighter, - larger/darker. Needs a Formant algorithm.",
        "Constant pitch offset in cents on corrected audio. Small opposite values on two harmony tracks widen a chorus.",
        "Slow random pitch wander in cents (new target about every 250 ms), so stacked voices do not phase-lock.",
        "Delays only the tuned (wet) signal, like a double singing slightly late. Not reported to the host."};
    for (size_t i = 0; i < chorusRows.size(); ++i) {
        auto& row = chorusRows[i];
        row.label.setText(rowNames[i],juce::dontSendNotification); row.label.setColour(juce::Label::textColourId,muted);
        row.slider.setSliderStyle(juce::Slider::LinearHorizontal); row.slider.setTextBoxStyle(juce::Slider::TextBoxRight,false,72,24);
        row.slider.setTextValueSuffix(rowSuffixes[i]); row.slider.setTooltip(rowTips[i]);
        addAndMakeVisible(row.label); addAndMakeVisible(row.slider);
        row.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.parameters,rowIds[i],row.slider);
    }
    setSize(1280, 750); refreshMeter(); startTimerHz(20);
    refreshMidiHelp();

}
AutoPitchEffEditor::~AutoPitchEffEditor() { stopTimer(); setLookAndFeel(nullptr); }
void AutoPitchEffEditor::refreshMidiHelp() {
    const int c0 = midiOctave.getSelectedId() == 2 ? 12 : 24;
    auto n = [](int number) { return juce::String(number); };
    midiHelp.setTooltip(
        "MIDI Key: change the correction scale with MIDI note-ons (route a MIDI track to this plugin).\n\n"
        "C0-B0 (notes " + n(c0) + "-" + n(c0+11) + "): select a major key. C0 = C major / A minor, D0 = D major / B minor. "
        "Send the next key note at the modulation point.\n\n"
        "C1-B1 (notes " + n(c0+12) + "-" + n(c0+23) + "): add that pitch class to the allowed notes.\n\n"
        "C2 (note " + n(c0+24) + "): clear all allowed notes. For an unusual scale send C2, then each wanted note in C1-B1.\n\n"
        "A key note equals C2 followed by every note of that major scale in C1-B1, so they combine: C0 with G#1 allows C major plus G#.\n\n"
        "Notes outside C0-C2 are ignored. Until a key or added note arrives (or after C2), the manual Key/Scale or AutoScaleLink scale is used. "
        "Simultaneous notes apply the key/C2 first, then the additions. Choose the channel and your DAW's octave naming on the right.");
}
void AutoPitchEffEditor::resized() {
    // Right column: MIDI control, then formant / chorus.
    midiKey.setBounds(910,48,96,28); midiHelp.setBounds(1008,48,26,28);
    midiChannel.setBounds(1040,48,106,28); midiOctave.setBounds(1150,48,104,28);
    midiTarget.setBounds(910,104,96,28); midiTargetHelp.setBounds(1008,104,26,28);
    midiTargetChannel.setBounds(1040,104,106,28); midiTargetOctave.setBounds(1150,104,104,28);
    maxShiftLabel.setBounds(906,162,106,28); maxShift.setBounds(1012,162,242,28);
    formantLabel.setBounds(906,248,106,28); formantMode.setBounds(1012,248,210,28); formantHelp.setBounds(1228,248,26,28);
    for (size_t i = 0; i < chorusRows.size(); ++i) {
        const int y = 292 + static_cast<int>(i) * 42;
        chorusRows[i].label.setBounds(906,y,106,28); chorusRows[i].slider.setBounds(1012,y,242,28);
    }
    hybrid.setBounds(26,710,230,28); fallbackLabel.setBounds(270,710,145,28); fallback.setBounds(425,710,445,28);
    pitchInput.setBounds(26,657,190,28); unlockAlignment.setBounds(235,657,220,28); alignment.setBounds(470,657,400,28);
    receiveScale.setBounds(26,610,125,28); linkGroupLabel.setBounds(159,610,48,28); linkGroup.setBounds(210,610,70,28);
    detectorLabel.setBounds(310, 22, 56, 30); detectorSource.setBounds(370, 22, 208, 30);
    preset.setBounds(600, 22, 196, 30); bypass.setBounds(810, 22, 82, 30);
    keyLabel.setBounds(26, 259, 35, 26); key.setBounds(64, 259, 78, 28);
    scaleLabel.setBounds(158, 259, 48, 26); scale.setBounds(210, 259, 140, 28);
    for (size_t i = 0; i < controls.size(); ++i) {
        const int col = static_cast<int>(i % 5), row = static_cast<int>(i / 5);
        controls[i].label.setBounds(27 + col * 175, 310 + row * 148, 150, 24);
        controls[i].slider.setBounds(27 + col * 175, 336 + row * 148, 150, 111);
    }
}
void AutoPitchEffEditor::timerCallback() {
    // Poll visibility slowly when a host hides the editor without destroying it.
    // Parent visibility and minimisation can change without a callback to us.
    bool visible = isShowing();
#if JUCE_MAC
    // VST3 embedding crosses native view trees: JUCE isShowing() cannot see
    // a hidden/minimised/occluded host window or its hidden NSView ancestors.
    if (visible) { auto* peer = getPeer(); visible = peer && apeIsNativeViewVisible(peer->getNativeHandle()); }
#endif
    if (!visible) { if (getTimerInterval() != 250) startTimer(250); return; }
    if (getTimerInterval() != 50) startTimerHz(20);
    refreshMeter();
    // Knobs, labels and the surrounding host must not be invalidated by meters.
    repaint(26, 90, 848, 150);
    repaint(388, 257, 449, 28);
    repaint(300,610,430,28);
    repaint(910,78,344,20);
    repaint(910,134,344,20);
}
void AutoPitchEffEditor::refreshMeter() {
    meter = owner.readMeter();
    pitchInput.setEnabled(!hybrid.getToggleState()); fallback.setEnabled(hybrid.getToggleState());
    const bool formantOn = formantMode.getSelectedId() > 1;
    chorusRows[0].slider.setEnabled(formantOn); chorusRows[1].slider.setEnabled(formantOn);
    const bool linked = (receiveScale.getToggleState() && owner.linkedKey() >= 0) || (midiKey.getToggleState() && owner.midiScaleMask() != 0) || owner.midiTargetNote() >= 0;
    scale.setEnabled(!linked); key.setEnabled(!linked && scale.getSelectedId() != 1);
    inputHistory[static_cast<size_t>(historyPosition)] = meter.inputHz > 0 ? static_cast<float>(ape::PitchEngine::frequencyToMidi(meter.inputHz)) : -1;
    targetHistory[static_cast<size_t>(historyPosition)] = meter.targetHz > 0 ? static_cast<float>(ape::PitchEngine::frequencyToMidi(meter.targetHz)) : -1;
    historyPosition = (historyPosition + 1) % static_cast<int>(inputHistory.size());
    rebuildCurves();
}
void AutoPitchEffEditor::rebuildCurves() {
    const double centre = meter.targetHz > 0 ? ape::PitchEngine::frequencyToMidi(meter.targetHz) : 69;
    auto build = [&](juce::Path& path, const auto& history) {
        path.clear(); bool started = false;
        for (size_t i = 0; i < history.size(); ++i) {
            const auto note = history[(static_cast<size_t>(historyPosition) + i) % history.size()];
            if (note < 0) { started = false; continue; }
            const float x = 368 + 478 * static_cast<float>(i) / static_cast<float>(history.size() - 1);
            const float y = 166 - static_cast<float>(std::clamp(note - centre, -6.0, 6.0)) * 8;
            if (started) path.lineTo(x, y); else path.startNewSubPath(x, y);
            started = true;
        }
    };
    build(inputCurve, inputHistory); build(targetCurve, targetHistory);
}
void AutoPitchEffEditor::paint(juce::Graphics& g) {
    g.fillAll(bg);
    if (g.clipRegionIntersects({0, 0, 900, 90})) {
        g.setColour(text); g.setFont(titleFont); g.drawText("AutoPitchEff", 26, 18, 250, 34, juce::Justification::centredLeft);
        g.setColour(muted); g.setFont(normalFont); g.drawText("LIVE VOCAL  /  HARD TUNE", 28, 54, 350, 20, juce::Justification::centredLeft);
    }
    if (g.clipRegionIntersects({26, 90, 848, 150})) {
        g.setColour(panel); g.fillRoundedRectangle(26, 90, 848, 150, 12);
        auto readout = [&](int x, const char* label, float hz, juce::Colour colour) {
            g.setFont(smallFont); g.setColour(muted); g.drawText(label, x, 104, 130, 20, juce::Justification::centredLeft);
            g.setFont(noteFont); g.setColour(colour); g.drawText(noteName(hz), x, 127, 130, 54, juce::Justification::centredLeft);
            g.setFont(valueFont); g.setColour(muted); g.drawText(hz > 0 ? juce::String(hz, 1) + " Hz" : "Waiting for voice", x, 188, 140, 22, juce::Justification::centredLeft);
        };
        readout(46, hybrid.getToggleState() ? (meter.usingSidechain ? "SC (HYBRID)" : "MAIN (HYBRID)") : pitchInput.getSelectedId() == 2 ? "SIDECHAIN" : "INPUT", meter.inputHz, mint); readout(204, "TARGET", meter.targetHz, purple);
        const juce::Rectangle<float> graph(368, 116, 478, 100);
        g.setColour(juce::Colour(0xff34414b));
        for (int i = 0; i <= 4; ++i) g.drawHorizontalLine(static_cast<int>(graph.getY() + i * 25), graph.getX(), graph.getRight());
        g.setColour(purple.withAlpha(.65f)); g.strokePath(targetCurve, juce::PathStrokeType(2));
        g.setColour(mint); g.strokePath(inputCurve, juce::PathStrokeType(1.5f));
        g.setFont(smallFont);
        g.setColour(meter.outputPeak >= .99f ? juce::Colour(0xffff8989) : muted);
        g.drawText("OUT " + juce::String(juce::Decibels::gainToDecibels(meter.outputPeak, -90.0f), 1) + " dBFS",
                   640, 218, 206, 20, juce::Justification::centredRight);
    }
    if (g.clipRegionIntersects({388, 257, 449, 28})) {
        const char* states[] = {"NO SIGNAL", "ACQUIRING", "TRACKING", "HOLDING"};
        g.setFont(normalFont); g.setColour(meter.state == ape::TrackingState::tracking ? mint : muted);
        g.drawText(states[std::clamp(static_cast<int>(meter.state), 0, 3)], 388, 257, 116, 28, juce::Justification::centredLeft);
        g.setColour(muted);
        g.drawText("Confidence " + juce::String(static_cast<int>(std::lround(meter.confidence * 100))) + "%", 502, 257, 155, 28, juce::Justification::centredLeft);
        g.drawText("Shift " + juce::String(static_cast<int>(std::lround(meter.correctionCents))) + " ct", 677, 257, 160, 28, juce::Justification::centredLeft);
    }
    if (g.clipRegionIntersects({26, 452, 848, 1})) {
        g.setColour(juce::Colour(0xff2b3943)); g.drawHorizontalLine(452, 26, 874);
    }
    if (g.clipRegionIntersects({300, 610, 430, 28})) {
        g.setFont(smallFont); g.setColour(muted);
        juce::String status = "Manual Key / Scale";
        if (receiveScale.getToggleState()) {
            const int linked = owner.linkedKey();
            if (linked >= 0) {
                static const char* names[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
                status = juce::String(names[linked%12]) + (linked < 12 ? " major" : " minor") + (owner.linkConnected() ? "  /  linked" : "  /  held (sender stopped)");
            } else status = owner.linkConnected() ? "Linked / waiting for applied scale" : "Waiting for AutoScaleLink / manual scale";
        }
        g.drawText(status,300,610,430,28,juce::Justification::centredLeft);
    }
    if (g.clipRegionIntersects({910, 78, 344, 20})) {
        g.setFont(smallFont); g.setColour(muted);
        juce::String status = "MIDI key off";
        if (midiKey.getToggleState()) {
            const unsigned mask = owner.midiScaleMask();
            const int root = owner.midiScaleRoot();
            if (mask == 0) status = "MIDI: waiting for key / notes (manual scale)";
            else {
                juce::StringArray extra;
                const unsigned base = root >= 0 ? ape::MidiScale::major(root) : 0;
                for (int c = 0; c < 12; ++c) if (((mask & ~base) >> c) & 1u) extra.add(pitchNames[c]);
                status = "MIDI: " + (root >= 0 ? juce::String(pitchNames[root]) + " major / " + minorNames[root] + " minor" + (extra.isEmpty() ? "" : " + ") : juce::String())
                       + extra.joinIntoString(" ");
            }
            g.setColour(mask != 0 ? mint : muted);
        }
        g.drawText(status,910,78,344,20,juce::Justification::centredLeft);
    }
    if (g.clipRegionIntersects({910, 134, 344, 20})) {
        g.setFont(smallFont); g.setColour(muted);
        juce::String status = "MIDI target off";
        if (midiTarget.getToggleState()) {
            const int note = owner.midiTargetNote();
            status = note >= 0 ? "Target from MIDI: " + juce::String(pitchNames[note % 12]) + " (note " + juce::String(note) + ")" : "MIDI target: no note held (scale correction)";
            g.setColour(note >= 0 ? mint : muted);
        }
        g.drawText(status,910,134,344,20,juce::Justification::centredLeft);
    }
    if (g.clipRegionIntersects({890, 0, 390, 750})) {
        g.setColour(juce::Colour(0xff2b3943)); g.drawVerticalLine(892, 22, 728);
        g.setFont(smallFont); g.setColour(muted);
        g.drawText("MIDI", 910, 22, 200, 20, juce::Justification::centredLeft);
        g.drawText("FORMANT  /  CHORUS", 910, 222, 240, 20, juce::Justification::centredLeft);
    }
    if (g.clipRegionIntersects({740, 616, 130, 20})) {
        g.setFont(smallFont);
        g.setColour(muted);
        g.drawText("v" JucePlugin_VersionString, 740, 616, 130, 20, juce::Justification::centredRight);
    }
}
