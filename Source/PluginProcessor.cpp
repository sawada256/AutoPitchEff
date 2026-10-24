#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace {
int delaySamples(double rate, float ms) noexcept {
    const float safeMs = std::isfinite(ms) ? std::clamp(ms,0.0f,50.0f) : 0.0f;
    return static_cast<int>(std::lround(rate * safeMs / 1000.0));
}
constexpr const char* ids[] = {"minHz", "maxHz", "retune", "amount", "stability", "threshold", "gate", "hold", "mix", "output", "root", "scale", "bypass", "detectorSource", "receiveScale", "linkGroup", "pitchInput", "alignmentMs", "hybridDetection", "fallbackMs", "midiKey", "midiKeyChannel", "midiOctave", "midiTarget", "midiTargetChannel", "midiTargetOctave", "maxShift", "formantMode", "formantPreserve", "formantShift", "detune", "humanize", "wetDelay"};
}
juce::AudioProcessorValueTreeState::ParameterLayout AutoPitchEffProcessor::layout() {
    juce::AudioProcessorValueTreeState::ParameterLayout p;
    auto add = [&p](const char* id, const char* name, float min, float max, float interval, float value, const char* unit) {
        p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id, 1}, name,
            juce::NormalisableRange<float>(min, max, interval), value,
            juce::AudioParameterFloatAttributes().withLabel(unit)));
    };
    add("minHz", "Range Low", 80, 600, 1, 180, "Hz");
    add("maxHz", "Range High", 400, 1800, 1, 1200, "Hz");
    add("retune", "Retune Speed", 0, 100, 0.1f, 0, "ms");
    add("amount", "Correction", 0, 100, 1, 100, "%");
    add("stability", "Note Stability", 0, 40, 1, 12, "ct");
    add("threshold", "Tracking Threshold", 0.2f, 0.95f, 0.01f, 0.55f, "");
    add("gate", "Detection Gate", -80, -12, 1, -48, "dB");
    add("hold", "Pitch Hold", 0, 60, 1, 18, "ms");
    add("mix", "Dry / Wet", 0, 100, 1, 100, "%");
    add("output", "Output", -24, 12, 0.1f, 0, "dB");
    p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"root", 1}, "Key", juce::StringArray{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}, 0));
    p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"scale", 1}, "Scale", juce::StringArray{"Chromatic", "Major", "Minor"}, 0));
    p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"bypass", 1}, "Bypass", false));
    p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"detectorSource", 1}, "Stereo Detection", juce::StringArray{"Sum (L+R)/2", "Left", "Right"}, 0));
    p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"receiveScale",1}, "Receive Scale",false));
    p.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{"linkGroup",1}, "Link Group",1,8,1));
    p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"pitchInput",1}, "Pitch Input", juce::StringArray{"Main", "Sidechain"},0));
    p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"alignmentMs",1}, "Main Alignment Delay",
        juce::NormalisableRange<float>(0,50,0.1f),0,
        juce::AudioParameterFloatAttributes().withLabel("ms").withAutomatable(false)));
    p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"hybridDetection",1},"Hybrid Detection",false));
    add("fallbackMs","Hybrid Fallback Wait",0,200,1,0,"ms");
    add("maxShift","Max Pitch Shift",1,12,.5f,4.5f,"st");
    p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"formantMode",1},"Formant",juce::StringArray{"Off","LPC","True Envelope","Auto"},0,
        juce::AudioParameterChoiceAttributes().withAutomatable(false)));
    add("formantPreserve","Formant Preserve",0,100,1,100,"%");
    add("formantShift","Formant Shift",-12,12,.1f,0,"st");
    add("detune","Detune",-50,50,.1f,0,"ct");
    add("humanize","Humanize",0,50,.1f,0,"ct");
    add("wetDelay","Double Delay",0,50,.1f,0,"ms");
    p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"midiKey",1},"MIDI Key Control",false));
    juce::StringArray channels {"Any"}; for (int c = 1; c <= 16; ++c) channels.add("Ch " + juce::String(c));
    p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"midiKeyChannel",1},"MIDI Key Channel",channels,2));
    p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"midiTarget",1},"MIDI Target Notes",false));
    channels.remove(0);
    p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"midiTargetChannel",1},"MIDI Target Channel",channels,0));
    p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"midiTargetOctave",1},"MIDI Target Octave",juce::StringArray{"Nearest Octave", "Absolute"},0));
    p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"midiOctave",1},"MIDI Octave Naming",juce::StringArray{"C3 = 60", "C4 = 60"},0));
    return p;
}
AutoPitchEffProcessor::AutoPitchEffProcessor()
    : AudioProcessor(BusesProperties().withInput("Vocal", juce::AudioChannelSet::mono(), true)
                                    .withOutput("Tuned Vocal", juce::AudioChannelSet::mono(), true)
                                    .withInput("Pitch Sidechain", juce::AudioChannelSet::stereo(), false)),
      parameters(*this, nullptr, "AutoPitchEffState", layout()) {
    for (int i = 0; i < parameterCount; ++i) values[static_cast<size_t>(i)] = parameters.getRawParameterValue(ids[i]);
    parameters.addParameterListener("alignmentMs",this);
    setLatencySamples(ape::PeriodShifter::fixedLatency());
}
AutoPitchEffProcessor::~AutoPitchEffProcessor() {
    parameters.removeParameterListener("alignmentMs",this); cancelPendingUpdate();
}
void AutoPitchEffProcessor::parameterChanged(const juce::String&, float) { triggerAsyncUpdate(); }
void AutoPitchEffProcessor::handleAsyncUpdate() {
    const int samples = delaySamples(preparedRate.load(),values[17]->load());
    setLatencySamples(ape::PeriodShifter::fixedLatency() + samples);
    alignmentSamples.store(samples);
}
void AutoPitchEffProcessor::prepareToPlay(double sr, int) {
    preparedRate.store(sr);
    for (auto& ring : alignmentBuffer) ring.assign(static_cast<size_t>(std::ceil(sr * .05)) + 1,0);
    const int samples = delaySamples(sr,values[17]->load());
    alignmentSamples.store(samples); setLatencySamples(ape::PeriodShifter::fixedLatency() + samples);
    engine.prepare(sr); reset();
}
void AutoPitchEffProcessor::reset() {
    alignmentPosition = 0; lastAlignment = -1; lastDetectionMode = -1; midiTarget.clear(); targetNote.store(-1);
    for (auto& ring : alignmentBuffer) std::fill(ring.begin(),ring.end(),0);
    settingsValid = false; engine.reset(); for (auto& m : meters) m.store(0, std::memory_order_relaxed);
}
bool AutoPitchEffProcessor::isBusesLayoutSupported(const BusesLayout& b) const {
    const auto in = b.getMainInputChannelSet(), out = b.getMainOutputChannelSet();
    if (b.inputBuses.size() > 1) {
        const auto sc = b.getChannelSet(true,1);
        if (!sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo()) return false;
    }
    return in == out && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}
void AutoPitchEffProcessor::readMidiTarget(const juce::MidiBuffer& midi, int channel) noexcept {
    for (const auto event : midi) {
        const auto message = event.getMessage();
        if (message.getChannel() != channel) continue;
        if (message.isNoteOn()) midiTarget.noteOn(message.getNoteNumber());
        else if (message.isNoteOff()) midiTarget.noteOff(message.getNoteNumber());
        else if (message.isAllNotesOff() || message.isAllSoundOff()) midiTarget.clear();
    }
}
void AutoPitchEffProcessor::readMidiScale(const juce::MidiBuffer& midi, int channel, int excludedChannel, int base) noexcept {
    // Collect note-ons, then apply key/reset before adds at the same position
    // so "C0 + G#1" means C major plus G# regardless of event order.
    std::array<std::pair<int,int>,256> notes; int count = 0;
    for (const auto event : midi) {
        const auto message = event.getMessage();
        if (!message.isNoteOn() || (channel > 0 && message.getChannel() != channel) || message.getChannel() == excludedChannel) continue;
        const int note = message.getNoteNumber();
        if (ape::MidiScale::kind(note,base) == ape::MidiScale::ignored || count == static_cast<int>(notes.size())) continue;
        auto item = std::make_pair(event.samplePosition * 2 + ape::MidiScale::priority(note,base), note);
        int i = count++;
        for (; i > 0 && notes[static_cast<size_t>(i-1)].first > item.first; --i) notes[static_cast<size_t>(i)] = notes[static_cast<size_t>(i-1)];
        notes[static_cast<size_t>(i)] = item;
    }
    if (count == 0) return;
    ape::MidiScale state; state.mask = midiMask.load(std::memory_order_relaxed); state.root = midiRoot.load(std::memory_order_relaxed);
    for (int i = 0; i < count; ++i) state.handle(notes[static_cast<size_t>(i)].second,base);
    midiMask.store(state.mask,std::memory_order_relaxed); midiRoot.store(state.root,std::memory_order_relaxed);
}
void AutoPitchEffProcessor::process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, bool bypassed) {
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumChannels() == 0) return;
    std::array<float, parameterCount> current;
    for (size_t i = 0; i < current.size(); ++i) current[i] = values[i]->load(std::memory_order_relaxed);
    const int group = std::clamp(static_cast<int>(current[15])-1,0,7);
    const int previousGroup = lastGroup.exchange(group,std::memory_order_relaxed);
    if (previousGroup >= 0 && previousGroup != group) acceptedKey.store(-1);
    if (current[14] > .5f) {
        const auto incoming = scaleLink.read(group,ape::ScaleLink::nowNanos());
        connected.store(incoming.connected,std::memory_order_relaxed);
        if (incoming.key >= 0) acceptedKey.store(incoming.key,std::memory_order_relaxed);
        const int key = acceptedKey.load(std::memory_order_relaxed);
        if (key >= 0 && key < 24) { current[10] = static_cast<float>(key%12); current[11] = key < 12 ? 1.0f : 2.0f; }
    } else connected.store(false,std::memory_order_relaxed);
    unsigned noteMask = 0;
    const bool targetMode = current[23] > .5f;
    const int targetChannel = static_cast<int>(current[24]) + 1;
    if (targetMode) readMidiTarget(midi,targetChannel);
    else midiTarget.clear();
    targetNote.store(midiTarget.current(),std::memory_order_relaxed);
    engine.setForcedNote(midiTarget.current(),current[25] > .5f);
    if (current[20] > .5f) {
        // With "Any channel", target-note MIDI must not also change the scale.
        readMidiScale(midi,static_cast<int>(current[21]),targetMode ? targetChannel : 0,current[22] > .5f ? 12 : 24);
        noteMask = midiMask.load(std::memory_order_relaxed);
    }
    if (bypassed) current[12] = 1;
    if (!settingsValid || current != lastSettings || noteMask != lastNoteMask) {
        auto value = [&current](int i) { return current[static_cast<size_t>(i)]; };
        ape::Settings s;
        s.minHz = value(0); s.maxHz = value(1); s.retuneMs = value(2); s.amount = value(3) / 100;
        s.stabilityCents = value(4); s.threshold = value(5); s.gateDb = value(6); s.holdMs = value(7);
        s.mix = value(8) / 100; s.outputDb = value(9); s.root = static_cast<int>(value(10)); s.scale = static_cast<int>(value(11));
        s.enabled = !bypassed && value(12) < 0.5f;
        s.fallbackMs = value(19); s.maxShift = value(26);
        s.formantMode = static_cast<int>(value(27)); s.formantPreserve = value(28) / 100; s.formantShift = value(29);
        s.detuneCents = value(30); s.humanizeCents = value(31); s.wetDelayMs = value(32);
        s.detectorSource = static_cast<int>(value(13));
        s.noteMask = noteMask;
        engine.setSettings(s);
        lastSettings = current; lastNoteMask = noteMask; settingsValid = true;
    }
    auto main = getBusBuffer(buffer,true,0);
    auto side = getBusBuffer(buffer,true,1);
    const bool hybrid = current[18] > .5f;
    const int detectionMode = hybrid ? 2 : current[16] > .5f ? 1 : 0;
    const bool external = detectionMode != 0;
    const int delay = std::clamp(alignmentSamples.load(),0,static_cast<int>(alignmentBuffer[0].size())-1);
    if (detectionMode != lastDetectionMode || delay != lastAlignment) {
        engine.reset();
        for (auto& ring : alignmentBuffer) std::fill(ring.begin(),ring.end(),0);
        alignmentPosition = 0; lastDetectionMode = detectionMode; lastAlignment = delay;
    }
    const int channels = main.getNumChannels();
    const int ringSize = static_cast<int>(alignmentBuffer[0].size());
    for (int i = 0; i < main.getNumSamples(); ++i) {
        // Read detection before modifying main. An absent sidechain deliberately
        // supplies silence rather than silently reverting to the noisy main input.
        float reference = 0;
        if (external && side.getNumChannels() > 0) {
            reference = side.getSample(0,i);
            if (side.getNumChannels() > 1) reference = static_cast<int>(current[13]) == 1 ? reference : static_cast<int>(current[13]) == 2
                ? side.getSample(1,i) : .5f * (reference + side.getSample(1,i));
        }
        float samples[2] {};
        for (int c = 0; c < channels; ++c) {
            auto& ring = alignmentBuffer[static_cast<size_t>(c)];
            const float sample = main.getSample(c,i);
            ring[static_cast<size_t>(alignmentPosition)] = std::isfinite(sample) ? sample : 0;
            samples[c] = ring[static_cast<size_t>((alignmentPosition - delay + ringSize) % ringSize)];
        }
        alignmentPosition = (alignmentPosition + 1) % ringSize;
        if (hybrid) engine.processHybrid(samples[0],samples[1],channels,reference);
        else if (external) engine.processExternal(samples[0],samples[1],channels,reference);
        else if (channels == 2) engine.processStereo(samples[0],samples[1]);
        else samples[0] = engine.process(samples[0]);
        for (int c = 0; c < channels; ++c) main.setSample(c,i,samples[c]);
    }
    const auto m = engine.meter();
    const float data[] = {m.inputHz, m.targetHz, m.confidence, m.correctionCents, m.inputPeak, m.outputPeak,
                          m.delayMs + static_cast<float>(delay * 1000.0 / preparedRate.load()), static_cast<float>(m.state), current[12] < .5f ? 1.0f : 0.0f, m.usingSidechain ? 1.0f : 0.0f};
    for (size_t i = 0; i < meters.size(); ++i) meters[i].store(data[i], std::memory_order_relaxed);
}
void AutoPitchEffProcessor::processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer& m) { process(b, m, false); }
void AutoPitchEffProcessor::processBlockBypassed(juce::AudioBuffer<float>& b, juce::MidiBuffer& m) { process(b, m, true); }
ape::Meter AutoPitchEffProcessor::readMeter() const noexcept {
    ape::Meter m;
    m.inputHz = meters[0].load(std::memory_order_relaxed); m.targetHz = meters[1].load(std::memory_order_relaxed); m.confidence = meters[2].load(std::memory_order_relaxed);
    m.correctionCents = meters[3].load(std::memory_order_relaxed); m.inputPeak = meters[4].load(std::memory_order_relaxed); m.outputPeak = meters[5].load(std::memory_order_relaxed);
    m.delayMs = meters[6].load(std::memory_order_relaxed); m.state = static_cast<ape::TrackingState>(static_cast<int>(meters[7].load(std::memory_order_relaxed))); m.usingSidechain = meters[9].load() > .5f; return m;
}
juce::AudioProcessorParameter* AutoPitchEffProcessor::getBypassParameter() const { return parameters.getParameter("bypass"); }
void AutoPitchEffProcessor::getStateInformation(juce::MemoryBlock& block) {
    auto state = parameters.copyState(); state.setProperty("schemaVersion", 6, nullptr);
    state.setProperty("linkedKey",acceptedKey.load(),nullptr);
    state.setProperty("midiNoteMask",static_cast<int>(midiMask.load()),nullptr);
    state.setProperty("midiKeyRoot",midiRoot.load(),nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, block);
}
void AutoPitchEffProcessor::setStateInformation(const void* data, int bytes) {
    if (auto xml = getXmlFromBinary(data, bytes))
        if (xml->hasTagName(parameters.state.getType())) {
            auto state = juce::ValueTree::fromXml(*xml);
            if (!state.getChildWithProperty("id", "detectorSource").isValid()) {
                juce::ValueTree source("PARAM"); source.setProperty("id", "detectorSource", nullptr);
                source.setProperty("value", 0.0f, nullptr); state.appendChild(source, nullptr);
            }
            for (const char* id : {"receiveScale","linkGroup","pitchInput","alignmentMs","hybridDetection","fallbackMs","midiKey","midiKeyChannel","midiOctave",
                                   "midiTarget","midiTargetChannel","midiTargetOctave","maxShift","formantMode","formantPreserve","formantShift","detune","humanize","wetDelay"})
                if (!state.getChildWithProperty("id",id).isValid()) {
                    // Older sessions lack this parameter: use its default.
                    auto* parameter = parameters.getParameter(id);
                    juce::ValueTree child("PARAM"); child.setProperty("id",id,nullptr);
                    child.setProperty("value",parameter->convertFrom0to1(parameter->getDefaultValue()),nullptr);
                    state.appendChild(child,nullptr);
                }
            acceptedKey.store(std::clamp(static_cast<int>(state.getProperty("linkedKey",-1)),-1,23));
            midiMask.store(static_cast<unsigned>(static_cast<int>(state.getProperty("midiNoteMask",0))) & 0xFFFu);
            midiRoot.store(midiMask.load() != 0 ? std::clamp(static_cast<int>(state.getProperty("midiKeyRoot",-1)),-1,11) : -1);
            connected.store(false); // Reconnect on the next audio block.
            lastGroup = -1;
            parameters.replaceState(state);
        }
}
void AutoPitchEffProcessor::resetPreset() {
    acceptedKey.store(-1); midiMask.store(0); midiRoot.store(-1);
    for (auto* parameter : getParameters()) {
        // A tuning preset must not accidentally change live routing or PDC.
        if (parameter == parameters.getParameter("pitchInput") || parameter == parameters.getParameter("alignmentMs") || parameter == parameters.getParameter("hybridDetection") || parameter == parameters.getParameter("fallbackMs")
            || parameter == parameters.getParameter("midiKey") || parameter == parameters.getParameter("midiKeyChannel") || parameter == parameters.getParameter("midiOctave")
            || parameter == parameters.getParameter("midiTarget") || parameter == parameters.getParameter("midiTargetChannel")
            || parameter == parameters.getParameter("midiTargetOctave")) continue;
        parameter->beginChangeGesture(); parameter->setValueNotifyingHost(parameter->getDefaultValue()); parameter->endChangeGesture();
    }
}
juce::AudioProcessorEditor* AutoPitchEffProcessor::createEditor() { return new AutoPitchEffEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AutoPitchEffProcessor(); }
