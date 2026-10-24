#include "KeyProcessor.h"
#include "KeyEditor.h"
#include <cmath>
namespace { constexpr const char* ids[] = {"mode", "analysis", "memory", "confidence", "confirm", "group", "transmit", "gate"}; }
juce::AudioProcessorValueTreeState::ParameterLayout AutoScaleLinkProcessor::layout() {
    juce::AudioProcessorValueTreeState::ParameterLayout p;
    p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"mode",1}, "Apply Mode", juce::StringArray{"Approve", "Automatic"}, 0));
    p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"analysis",1}, "Analysis Source", juce::StringArray{"Mix / Instruments", "Vocal (150-1400 Hz)"}, 0));
    auto add = [&p](const char* id, const char* name, float low, float high, float step, float initial) {
        p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id,1}, name, juce::NormalisableRange<float>(low,high,step),initial));
    };
    add("memory","Analysis Memory (s)",3,30,.5f,8);
    add("confidence","Auto Confidence (%)",40,95,1,60);
    add("confirm","Auto Confirm (s)",.5f,10,.1f,2);
    p.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{"group",1}, "Link Group",1,8,1));
    p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"transmit",1}, "Send Scale",true));
    add("gate","Analysis Gate (dB)",-80,-20,1,-55);
    return p;
}
AutoScaleLinkProcessor::AutoScaleLinkProcessor()
    : AudioProcessor(BusesProperties().withInput("Analysis",juce::AudioChannelSet::stereo(),true).withOutput("Passthrough",juce::AudioChannelSet::stereo(),true)),
      Thread("AutoScaleLink Analysis"), parameters(*this,nullptr,"AutoScaleLinkState",layout()) {
    for (size_t i = 0; i < values.size(); ++i) values[i] = parameters.getRawParameterValue(ids[i]);
    setLatencySamples(0); publishResult({});
}
AutoScaleLinkProcessor::~AutoScaleLinkProcessor() { releaseResources(); }
bool AutoScaleLinkProcessor::isBusesLayoutSupported(const BusesLayout& b) const {
    const auto in = b.getMainInputChannelSet();
    return in == b.getMainOutputChannelSet() && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}
void AutoScaleLinkProcessor::prepareToPlay(double sr, int) {
    releaseResources();
    sr = std::isfinite(sr) && sr >= 16000 ? sr : 48000;
    for (auto& channel : filters) for (auto& f : channel) { f.lowpass(sr,4500); f.reset(); }
    resampleStep = ape::KeyAnalyser::sampleRate / sr; resamplePhase = 0;
    write.store(0); read.store(0); dropped.store(0); lastAudio.store(0);
    analyser.reset(); publishResult({}); resetRequested.store(false);
    startThread();
}
void AutoScaleLinkProcessor::releaseResources() { stopThread(-1); link.release(); status.store(0); }
void AutoScaleLinkProcessor::processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    if (b.getNumChannels() == 0 || b.getNumSamples() == 0) return;
    // Sample-exact passthrough. Only the private analysis copy is sanitised.
    const auto* left = b.getReadPointer(0);
    const auto* right = b.getNumChannels() >= 2 ? b.getReadPointer(1) : left;
    auto head = write.load(std::memory_order_relaxed);
    const auto tail = read.load(std::memory_order_acquire);
    uint64_t lost = 0;
    for (int i = 0; i < b.getNumSamples(); ++i) {
        float l = std::isfinite(left[i]) ? std::clamp(left[i],-8.0f,8.0f) : 0;
        float r = std::isfinite(right[i]) ? std::clamp(right[i],-8.0f,8.0f) : 0;
        for (auto& f : filters[0]) l = f.process(l);
        for (auto& f : filters[1]) r = f.process(r);
        resamplePhase += resampleStep;
        if (resamplePhase >= 1) {
            resamplePhase -= 1;
            if (head-tail < capacity) queue[static_cast<size_t>(head++ % capacity)] = {l,r}; else ++lost;
        }
    }
    write.store(head,std::memory_order_release);
    if (lost) dropped.fetch_add(lost,std::memory_order_relaxed);
    lastAudio.store(ape::ScaleLink::nowNanos(),std::memory_order_release);
}
void AutoScaleLinkProcessor::processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) { lastAudio.store(0,std::memory_order_release); }
void AutoScaleLinkProcessor::publishResult(const ape::KeyResult& r) noexcept {
    display[0].store(static_cast<float>(r.key)); display[1].store(static_cast<float>(r.alternate));
    display[2].store(r.confidence); display[3].store(r.evidenceSeconds); display[4].store(r.tuningCents); display[5].store(r.ready ? 1.0f : 0.0f);
    for (size_t i = 0; i < 12; ++i) display[i+6].store(r.chroma[i]);
}
ape::KeyResult AutoScaleLinkProcessor::readResult() const noexcept {
    ape::KeyResult r;
    r.key = static_cast<int>(display[0].load()); r.alternate = static_cast<int>(display[1].load());
    r.confidence = display[2].load(); r.evidenceSeconds = display[3].load(); r.tuningCents = display[4].load(); r.ready = display[5].load() > .5f;
    for (size_t i = 0; i < 12; ++i) r.chroma[i] = display[i+6].load();
    return r;
}
void AutoScaleLinkProcessor::run() {
    std::array<float,8> previous {}; previous.fill(-999);
    int stableKey = -1; double stableSeconds = 0;
    uint64_t previousDrops = 0;
    bool wasActive = false;
    while (!threadShouldExit()) {
        std::array<float,8> settings;
        for (size_t i = 0; i < settings.size(); ++i) settings[i] = values[i]->load();
        if (settings[1] != previous[1] || settings[2] != previous[2] || settings[7] != previous[7])
            analyser.configure(settings[1] > .5f,settings[2],settings[7]);
        if (settings[0] != previous[0] || settings[3] != previous[3] || settings[4] != previous[4] || settings[1] != previous[1]) { stableKey = -1; stableSeconds = 0; }
        previous = settings;
        auto tail = read.load(std::memory_order_relaxed);
        const auto head = write.load(std::memory_order_acquire);
        const auto lost = dropped.load();
        const auto audioNow = ape::ScaleLink::nowNanos(), stamp = lastAudio.load(std::memory_order_acquire);
        const bool active = stamp && audioNow >= stamp && audioNow-stamp < 1000000000ULL;
        if (resetRequested.exchange(false) || lost != previousDrops || head-tail > 12000 || (wasActive && !active)) {
            // A stalled worker discards old analysis; audio is never delayed.
            analyser.reset(); publishResult({}); stableKey = -1; stableSeconds = 0;
            tail = head; read.store(tail,std::memory_order_release); previousDrops = lost;
        }
        wasActive = active;
        const auto end = std::min(head,tail+1024);
        for (; tail < end && !threadShouldExit(); ++tail) {
            const auto frame = queue[static_cast<size_t>(tail % capacity)];
            if (!analyser.push(frame[0],frame[1])) continue;
            const auto result = analyser.result(); publishResult(result);
            if (settings[0] > .5f && result.ready && result.confidence >= settings[3]/100) {
                if (stableKey < 0 || ape::KeyAnalyser::scaleMask(stableKey) != ape::KeyAnalyser::scaleMask(result.key)) { stableKey = result.key; stableSeconds = 0; }
                stableSeconds += ape::KeyAnalyser::hopSize / ape::KeyAnalyser::sampleRate;
                if (stableSeconds >= settings[4]) applied.store(result.key);
            } else { stableKey = -1; stableSeconds = 0; }
        }
        read.store(tail,std::memory_order_release);
        const int approved = approval.exchange(-1); if (approved >= 0 && approved < 24) applied.store(approved);
        const auto now = ape::ScaleLink::nowNanos();
        if (settings[6] > .5f && active)
            status.store(!link.available() ? 3 : link.publish(static_cast<int>(settings[5])-1,applied.load(),now) ? 1 : 2);
        else { link.release(); status.store(0); }
        if (tail == head) wait(10);
    }
    link.release(); status.store(0);
}
void AutoScaleLinkProcessor::getStateInformation(juce::MemoryBlock& b) {
    auto state = parameters.copyState(); state.setProperty("appliedKey",applied.load(),nullptr);
    state.setProperty("schemaVersion",1,nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml,b);
}
void AutoScaleLinkProcessor::setStateInformation(const void* data, int bytes) {
    if (auto xml = getXmlFromBinary(data,bytes)) if (xml->hasTagName(parameters.state.getType())) {
        const auto state = juce::ValueTree::fromXml(*xml);
        applied.store(std::clamp(static_cast<int>(state.getProperty("appliedKey",-1)),-1,23));
        approval.store(-1); resetRequested.store(true); parameters.replaceState(state);
    }
}
juce::AudioProcessorEditor* AutoScaleLinkProcessor::createEditor() { return new AutoScaleLinkEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AutoScaleLinkProcessor(); }
