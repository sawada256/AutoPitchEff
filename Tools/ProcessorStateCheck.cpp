#include "PluginProcessor.h"
#include <iostream>
int main() {
    juce::ScopedJuceInitialiser_GUI initialise;
    AutoPitchEffProcessor processor;
    auto legacy = processor.parameters.copyState();
    legacy.removeChild(legacy.getChildWithProperty("id", "detectorSource"), nullptr);
    legacy.removeChild(legacy.getChildWithProperty("id", "receiveScale"), nullptr);
    legacy.removeChild(legacy.getChildWithProperty("id", "linkGroup"), nullptr);
    legacy.removeChild(legacy.getChildWithProperty("id", "pitchInput"), nullptr);
    legacy.removeChild(legacy.getChildWithProperty("id", "alignmentMs"), nullptr);
    legacy.removeChild(legacy.getChildWithProperty("id", "hybridDetection"),nullptr);
    legacy.removeChild(legacy.getChildWithProperty("id", "fallbackMs"),nullptr);
    for (const char* id : {"midiKey","midiKeyChannel","midiOctave","midiTarget","midiTargetChannel","midiTargetOctave","maxShift","formantMode","formantPreserve","formantShift","detune","humanize","wetDelay"}) legacy.removeChild(legacy.getChildWithProperty("id", id),nullptr);
    auto xml = legacy.createXml(); juce::MemoryBlock blob;
    juce::AudioProcessor::copyXmlToBinary(*xml, blob);
    auto* source = processor.parameters.getParameter("detectorSource");
    source->setValueNotifyingHost(1); // Right, then restore a pre-stereo session.
    processor.parameters.getParameter("receiveScale")->setValueNotifyingHost(1);
    processor.parameters.getParameter("linkGroup")->setValueNotifyingHost(1);
    processor.parameters.getParameter("pitchInput")->setValueNotifyingHost(1);
    processor.parameters.getParameter("alignmentMs")->setValueNotifyingHost(.5f);
    processor.parameters.getParameter("hybridDetection")->setValueNotifyingHost(1);
    processor.parameters.getParameter("fallbackMs")->setValueNotifyingHost(1);
    processor.parameters.getParameter("maxShift")->setValueNotifyingHost(1); // 12 st, then restore a pre-maxShift session.
    processor.parameters.getParameter("formantPreserve")->setValueNotifyingHost(0);
    processor.parameters.getParameter("formantMode")->setValueNotifyingHost(1);
    processor.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
    if(processor.parameters.getRawParameterValue("hybridDetection")->load()!=0 || processor.parameters.getRawParameterValue("fallbackMs")->load()!=0) return 19;
    if(std::abs(processor.parameters.getRawParameterValue("maxShift")->load()-4.5f)>1e-6f) return 33;
    if(processor.parameters.getRawParameterValue("formantPreserve")->load()!=100 || processor.parameters.getRawParameterValue("formantMode")->load()!=0) return 34;
    if(processor.parameters.getRawParameterValue("midiKey")->load()!=0 || processor.parameters.getRawParameterValue("midiKeyChannel")->load()!=2) return 27;
    if (processor.parameters.getRawParameterValue("pitchInput")->load()!=0 || processor.parameters.getRawParameterValue("alignmentMs")->load()!=0) return 12;
    if (processor.parameters.getRawParameterValue("detectorSource")->load() != 0) {
        std::cerr << "Legacy state did not default to summed detection\n"; return 1;
    }
    if (processor.parameters.getRawParameterValue("receiveScale")->load()!=0 ||
        processor.parameters.getRawParameterValue("linkGroup")->load()!=1) return 2;
    source->setValueNotifyingHost(1);
    processor.getStateInformation(blob);
    source->setValueNotifyingHost(0);
    processor.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
    if (processor.parameters.getRawParameterValue("detectorSource")->load() != 2) return 1;
    // External detection must follow the clean reference, not main's different tone.
    auto buses = processor.getBusesLayout();
    buses.inputBuses.set(0,juce::AudioChannelSet::stereo());
    buses.outputBuses.set(0,juce::AudioChannelSet::stereo());
    buses.inputBuses.set(1,juce::AudioChannelSet::mono());
    if (!processor.setBusesLayout(buses)) return 3;
    processor.parameters.getParameter("pitchInput")->setValueNotifyingHost(1);
    processor.prepareToPlay(48000,64);
    juce::AudioBuffer<float> audio(3,64); juce::MidiBuffer midi;
    for (int block=0; block<1500; ++block) {
        for (int i=0;i<64;++i) {
            const double t=(block*64+i)/48000.0;
            audio.setSample(0,i,static_cast<float>(.3*std::sin(2*ape::pi*700*t)));
            audio.setSample(1,i,audio.getSample(0,i)*.5f);
            audio.setSample(2,i,static_cast<float>(.4*std::sin(2*ape::pi*452*t)));
        }
        processor.processBlock(audio,midi);
        for (int i=0;i<64;++i) if (std::abs(audio.getSample(1,i)-.5f*audio.getSample(0,i))>1e-5f) return 4;
    }
    if (std::abs(processor.readMeter().inputHz-452)>2) return 5;
    // Missing reference is silence, and stale tracking expires.
    for (int block=0;block<300;++block) {
        audio.clear();
        for(int i=0;i<64;++i) { audio.setSample(0,i,.3f); audio.setSample(1,i,.15f); }
        processor.processBlock(audio,midi);
    }
    if (std::abs(audio.getSample(0,63)-.3f)>1e-5f) return 13;
    if (processor.readMeter().inputHz != 0) return 6;
    // Dry and host-bypassed paths carry exactly the reported fixed delay.
    auto* alignment = processor.parameters.getParameter("alignmentMs");
    if (alignment->isAutomatable()) return 7;
    processor.parameters.getParameter("pitchInput")->setValueNotifyingHost(0);
    for (double rate : {44100.0,48000.0,96000.0}) for (float ms : {0.0f,17.3f,50.0f}) {
        alignment->setValueNotifyingHost(alignment->convertTo0to1(ms));
        processor.prepareToPlay(rate,64);
        const int expected=8+static_cast<int>(std::lround(rate*ms/1000));
        if (processor.getLatencySamples()!=expected) return 8;
        int observed=-1;
        for (int block=0;block<(expected+128)/64+1;++block) {
            audio.clear(); if (block==0) audio.setSample(0,0,1);
            processor.processBlockBypassed(audio,midi);
            for (int i=0;i<64;++i) if (std::abs(audio.getSample(0,i))>.5f) observed=block*64+i;
        }
        if (observed!=expected) { std::cerr << "Delay mismatch "<<observed<<" / "<<expected<<"\n"; return 9; }
    }
    processor.getStateInformation(blob);
    alignment->setValueNotifyingHost(0);
    processor.setStateInformation(blob.getData(),static_cast<int>(blob.getSize()));
    if (processor.parameters.getRawParameterValue("alignmentMs")->load()!=50) return 10;
    processor.parameters.getParameter("pitchInput")->setValueNotifyingHost(1);
    processor.resetPreset();
    if (processor.parameters.getRawParameterValue("alignmentMs")->load()!=50 || processor.parameters.getRawParameterValue("pitchInput")->load()!=1) return 11;
    // Pre-processing alignment regression: compare against an oracle fed
    // explicitly time-aligned main/reference signals, with no plugin delay.
    // A post-processing output delay would fail across these note transitions.
    for (double rate : {44100.0,48000.0,96000.0}) for (int blockSize : {64,127}) for(bool hybridMode : {false,true}) {
        AutoPitchEffProcessor delayedMain, alignedOracle;
        for (auto* p : {&delayedMain,&alignedOracle}) {
            if (!p->setBusesLayout(buses)) return 14;
            p->parameters.getParameter("pitchInput")->setValueNotifyingHost(1);
            p->parameters.getParameter("hybridDetection")->setValueNotifyingHost(hybridMode ? 1 : 0);
        }
        auto* delayParameter=delayedMain.parameters.getParameter("alignmentMs");
        delayParameter->setValueNotifyingHost(delayParameter->convertTo0to1(30));
        delayedMain.prepareToPlay(rate,blockSize); alignedOracle.prepareToPlay(rate,blockSize);
        const int offset=static_cast<int>(std::lround(rate*.03));
        if (delayedMain.getLatencySamples()!=offset+8) return 15;
        const int length=static_cast<int>(rate);
        std::vector<float> vocal(static_cast<size_t>(length));
        double phase=0;
        for(int n=0;n<length;++n) {
            const int segment=static_cast<int>(n/(rate*.075))%4;
            const double hz=segment==0 ? 452 : segment==1 ? 535 : segment==2 ? 603 : 481;
            phase+=2*ape::pi*hz/rate;
            vocal[static_cast<size_t>(n)]=static_cast<float>(.3*std::sin(phase)+.06*std::sin(2*phase));
        }
        juce::AudioBuffer<float> actual(3,blockSize), expected(3,blockSize);
        for(int start=0;start<length;start+=blockSize) {
            for(int i=0;i<blockSize;++i) {
                const int n=start+i;
                const float main=n<length ? vocal[static_cast<size_t>(n)] : 0;
                const float reference=n>=offset && n-offset<length ? vocal[static_cast<size_t>(n-offset)] : 0;
                actual.setSample(0,i,main); actual.setSample(1,i,.5f*main); actual.setSample(2,i,reference);
                expected.setSample(0,i,reference); expected.setSample(1,i,.5f*reference); expected.setSample(2,i,reference);
            }
            delayedMain.processBlock(actual,midi); alignedOracle.processBlock(expected,midi);
            for(int c=0;c<2;++c) for(int i=0;i<blockSize;++i) {
                if(actual.getSample(c,i)!=expected.getSample(c,i)) {
                    std::cerr<<"Pre-processing alignment mismatch at "<<rate<<" Hz, sample "<<start+i<<"\n";
                    return 16;
                }
            }
        }
        if(delayedMain.readMeter().inputHz<=0 || std::abs(delayedMain.readMeter().correctionCents)<1) return 17;
    }
    processor.parameters.getParameter("hybridDetection")->setValueNotifyingHost(1);
    auto* fallback=processor.parameters.getParameter("fallbackMs");
    fallback->setValueNotifyingHost(fallback->convertTo0to1(25));
    processor.getStateInformation(blob);
    processor.parameters.getParameter("hybridDetection")->setValueNotifyingHost(0);
    fallback->setValueNotifyingHost(0);
    processor.setStateInformation(blob.getData(),static_cast<int>(blob.getSize()));
    if(processor.parameters.getRawParameterValue("hybridDetection")->load()!=1 || processor.parameters.getRawParameterValue("fallbackMs")->load()!=25) return 18;
    processor.resetPreset();
    if(processor.parameters.getRawParameterValue("hybridDetection")->load()!=1 || processor.parameters.getRawParameterValue("fallbackMs")->load()!=25) return 20;
    // MIDI key control: G#1 before C0 at the same position still gives C major + G#.
    {
        AutoPitchEffProcessor midiProcessor;
        midiProcessor.parameters.getParameter("midiKey")->setValueNotifyingHost(1);
        midiProcessor.prepareToPlay(48000,64);
        juce::AudioBuffer<float> silence(1,64); juce::MidiBuffer notes;
        notes.addEvent(juce::MidiMessage::noteOn(2,44,.8f),5);
        notes.addEvent(juce::MidiMessage::noteOn(2,24,.8f),5);
        notes.addEvent(juce::MidiMessage::noteOn(1,26,.8f),9); // wrong channel
        silence.clear(); midiProcessor.processBlock(silence,notes);
        if (midiProcessor.midiScaleMask()!=(ape::MidiScale::major(0)|(1u<<8)) || midiProcessor.midiScaleRoot()!=0) return 21;
        notes.clear(); notes.addEvent(juce::MidiMessage::noteOn(2,48,.8f),0); notes.addEvent(juce::MidiMessage::noteOn(2,37,.8f),1);
        silence.clear(); midiProcessor.processBlock(silence,notes);
        if (midiProcessor.midiScaleMask()!=(1u<<1) || midiProcessor.midiScaleRoot()!=-1) return 22;
        midiProcessor.getStateInformation(blob);
        AutoPitchEffProcessor restored; restored.setStateInformation(blob.getData(),static_cast<int>(blob.getSize()));
        if (restored.midiScaleMask()!=(1u<<1) || restored.parameters.getRawParameterValue("midiKey")->load()!=1) return 23;
        auto* octave=midiProcessor.parameters.getParameter("midiOctave"); octave->setValueNotifyingHost(1); // C4 = 60: C0 = 12
        notes.clear(); notes.addEvent(juce::MidiMessage::noteOn(2,14,.8f),0);
        silence.clear(); midiProcessor.processBlock(silence,notes);
        if (midiProcessor.midiScaleMask()!=ape::MidiScale::major(2)) return 24;
        midiProcessor.parameters.getParameter("midiKey")->setValueNotifyingHost(0);
        notes.clear(); notes.addEvent(juce::MidiMessage::noteOn(2,24,.8f),0);
        silence.clear(); midiProcessor.processBlock(silence,notes);
        if (midiProcessor.midiScaleMask()!=ape::MidiScale::major(2)) return 25;
        midiProcessor.resetPreset();
        if (midiProcessor.midiScaleMask()!=0 || midiProcessor.parameters.getRawParameterValue("midiKeyChannel")->load()!=2) return 26;
    }
    {
        AutoPitchEffProcessor targetProcessor;
        targetProcessor.parameters.getParameter("midiTarget")->setValueNotifyingHost(1);
        auto* keyChannel=targetProcessor.parameters.getParameter("midiKeyChannel");
        targetProcessor.parameters.getParameter("midiKey")->setValueNotifyingHost(1);
        keyChannel->setValueNotifyingHost(0); // Any channel
        targetProcessor.prepareToPlay(48000,64);
        juce::AudioBuffer<float> silence(1,64); juce::MidiBuffer notes;
        notes.addEvent(juce::MidiMessage::noteOn(1,26,.8f),0); notes.addEvent(juce::MidiMessage::noteOn(1,69,.8f),3);
        notes.addEvent(juce::MidiMessage::noteOn(2,62,.8f),4); // other channel: not a target
        silence.clear(); targetProcessor.processBlock(silence,notes);
        if (targetProcessor.midiTargetNote()!=69 || targetProcessor.midiScaleMask()!=0) return 28;
        notes.clear(); notes.addEvent(juce::MidiMessage::noteOff(1,69),0);
        silence.clear(); targetProcessor.processBlock(silence,notes);
        if (targetProcessor.midiTargetNote()!=26) return 29;
        notes.clear(); notes.addEvent(juce::MidiMessage::allNotesOff(1),0);
        silence.clear(); targetProcessor.processBlock(silence,notes);
        if (targetProcessor.midiTargetNote()!=-1) return 30;
        targetProcessor.resetPreset();
        if (targetProcessor.parameters.getRawParameterValue("midiTarget")->load()!=1) return 31;
        targetProcessor.parameters.getParameter("midiTargetOctave")->setValueNotifyingHost(1);
        targetProcessor.getStateInformation(blob);
        AutoPitchEffProcessor restored; restored.setStateInformation(blob.getData(),static_cast<int>(blob.getSize()));
        if (restored.parameters.getRawParameterValue("midiTargetOctave")->load()!=1) return 32;
    }
    std::cout << "MIDI key control passed\n";
    std::cout << "30 ms pre-processing alignment matches explicitly aligned oracle across note transitions\n";
    std::cout << "Sidechain detection, stereo linking, exact delay and restore passed\n";
    std::cout << "Legacy and stereo state restore passed\n";
    return 0;
}
