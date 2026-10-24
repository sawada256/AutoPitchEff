#include <juce_audio_utils/juce_audio_utils.h>
#include <chrono>
#include <thread>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include "key/KeyAnalyser.h"
#include "GuiStress.h"
using Processor = juce::AudioPluginInstance;
static void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
static std::unique_ptr<Processor> load(const juce::String& path) {
    juce::VST3PluginFormat format; juce::OwnedArray<juce::PluginDescription> descriptions;
    format.findAllTypesForFile(descriptions,path); check(descriptions.size()==1,"VST3 discovery failed");
    juce::String error; auto result = format.createInstanceFromDescription(*descriptions[0],48000,256,error);
    if (!result) throw std::runtime_error(error.toStdString());
    return result;
}
static juce::AudioProcessorParameter* parameter(Processor& p,const juce::String& name) {
    for (auto* v : p.getParameters()) if (v->getName(80)==name) return v;
    throw std::runtime_error("Missing parameter: "+name.toStdString());
}
static void set(Processor& p,const juce::String& name,float normal) { parameter(p,name)->setValueNotifyingHost(normal); }
static std::unique_ptr<juce::XmlElement> state(Processor& p) {
    juce::MemoryBlock b; p.getStateInformation(b); auto xml = juce::AudioProcessor::getXmlFromBinary(b.getData(),static_cast<int>(b.getSize()));
    check(xml!=nullptr,"Host state XML unavailable");
    auto* component=xml->getChildByName("IComponent"); check(component!=nullptr,"Component state unavailable");
    juce::MemoryBlock raw; check(raw.fromBase64Encoding(component->getAllSubText()),"Component decoding failed");
    auto inner=juce::AudioProcessor::getXmlFromBinary(raw.getData(),static_cast<int>(raw.getSize()));
    check(inner!=nullptr,"Processor state XML unavailable"); return inner;
}
static void restore(Processor& p,juce::XmlElement& xml) {
    juce::MemoryBlock original; p.getStateInformation(original);
    auto outer=juce::AudioProcessor::getXmlFromBinary(original.getData(),static_cast<int>(original.getSize()));
    check(outer!=nullptr,"Host restore state unavailable");
    auto* component=outer->getChildByName("IComponent"); check(component!=nullptr,"Restore component unavailable");
    juce::MemoryBlock raw; check(raw.fromBase64Encoding(component->getAllSubText()),"Restore decoding failed");
    check(raw.getSize()>9,"Short component state");
    const size_t xmlEnd=9+juce::ByteOrder::littleEndianInt(static_cast<const char*>(raw.getData())+4);
    check(xmlEnd<=raw.getSize(),"Invalid component XML length");
    juce::MemoryBlock replacement; juce::AudioProcessor::copyXmlToBinary(xml,replacement);
    replacement.append(static_cast<const char*>(raw.getData())+xmlEnd,raw.getSize()-xmlEnd);
    component->deleteAllChildElements(); component->addTextElement(replacement.toBase64Encoding());
    juce::MemoryBlock b; juce::AudioProcessor::copyXmlToBinary(*outer,b);
    p.setStateInformation(b.getData(),static_cast<int>(b.getSize()));
}
static void apply(Processor& p,int key) { auto xml = state(p); xml->setAttribute("appliedKey",key); restore(p,*xml); }
static int applied(Processor& p) { return state(p)->getIntAttribute("appliedKey",-1); }
static int received(Processor& p) { return state(p)->getIntAttribute("linkedKey",-1); }
static void stereo(Processor& p) {
    auto layout = p.getBusesLayout(); layout.inputBuses.set(0,juce::AudioChannelSet::stereo()); layout.outputBuses.set(0,juce::AudioChannelSet::stereo());
    check(p.setBusesLayout(layout),"Stereo layout rejected");
    p.setRateAndBufferSizeDetails(48000,256); p.prepareToPlay(48000,256);
}
static float fixture(int index,int key) {
    const int chords[8][3] = {{0,4,7},{5,9,12},{7,11,14},{0,4,7},{9,12,16},{2,5,9},{7,11,14},{0,4,7}};
    const double t = index/48000.0; double s = 0;
    for (int note : chords[static_cast<int>(t)%8]) {
        const double hz = ape::PitchEngine::midiToFrequency(48+key+note);
        for (int h = 1; h<=6; ++h) s += .045*std::sin(2*ape::pi*hz*h*t+.13*h)/std::pow(h,1.5);
    }
    return static_cast<float>(s*std::min(1.0,(t-std::floor(t))*30));
}
static float feed(Processor& detector,Processor* receiver,double seconds,int key=0,bool silence=false) {
    juce::AudioBuffer<float> b(2,256), tuned(2,256); juce::MidiBuffer midi;
    ape::PitchDetector outputPitch; outputPitch.prepare(48000);
    ape::Settings tracking; tracking.minHz=300; tracking.maxHz=600; tracking.threshold=.7f; outputPitch.setSettings(tracking);
    double outputHz=0; int tracked=0;
    const auto start = std::chrono::steady_clock::now();
    const int blocks = static_cast<int>(seconds*48000/256);
    double maxCallback = 0;
    for (int block = 0; block<blocks; ++block) {
        for (int i=0;i<256;++i) { const auto s=silence ? 0.0f : fixture(block*256+i,key); b.setSample(0,i,s); b.setSample(1,i,-s); }
        const auto original = b.getSample(0,100);
        const auto before = std::chrono::steady_clock::now(); detector.processBlock(b,midi);
        maxCallback = std::max(maxCallback,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-before).count());
        check(b.getSample(0,100)==original && b.getSample(1,100)==-original,"Detector changed audio");
        if (receiver) {
            for (int i=0;i<256;++i) { const float s=.15f*static_cast<float>(std::sin(2*ape::pi*452*(block*256+i)/48000)); tuned.setSample(0,i,s); tuned.setSample(1,i,s*.3f); }
            receiver->processBlock(tuned,midi);
            for (int i=0;i<256;++i) {
                const auto s=tuned.getSample(0,i);
                check(std::isfinite(s) && std::abs(tuned.getSample(1,i)-.3f*s)<3e-6f,"Linked tuning changed stereo image");
                if (block>=blocks-180 && outputPitch.push(s)) {
                    const auto m=outputPitch.meter();
                    if (m.state==ape::TrackingState::tracking) { outputHz+=m.inputHz; ++tracked; }
                }
            }
        }
        // Keep worker analysis in real time and allow host parameter dispatch.
        std::this_thread::sleep_until(start+std::chrono::microseconds(static_cast<long long>((block+1)*256*1000000.0/48000)));
        if (block%64==0) juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
    }
    std::cout << "feed seconds="<<seconds<<" max_audio_callback_ms="<<maxCallback<<'\n';
    return tracked ? static_cast<float>(outputHz/tracked) : 0;
}
int main(int argc,char** argv) {
    juce::ScopedJuceInitialiser_GUI initialise;
    try {
        check(argc>=3,"Usage: ape-scale-pair-check detector.vst3 tuner.vst3 [--gui-stress|--ipc-send|--ipc-receive]");
        const juce::String option = argc>3 ? argv[3] : "";
        check(option.isEmpty() || option=="--gui-stress" || option=="--ipc-send" || option=="--ipc-receive" || option=="--alternate-group","Unknown option");
        if (option=="--ipc-receive") {
            auto receiver=load(juce::String::fromUTF8(argv[2])); set(*receiver,"Receive Scale",1); set(*receiver,"Link Group",5.0f/7); stereo(*receiver);
            juce::AudioBuffer<float> b(2,256); juce::MidiBuffer midi;
            for (int i=0;i<500 && received(*receiver)!=19;++i) { b.clear(); receiver->processBlock(b,midi); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
            check(received(*receiver)==19,"Cross-process key not received"); std::cout<<"Cross-process VST3 received key 19\n"; receiver->releaseResources(); return 0;
        }
        auto detector=load(juce::String::fromUTF8(argv[1]));
        if (option=="--gui-stress") { stereo(*detector); return guiStress(*detector); }
        const float pairGroup = option=="--alternate-group" ? 4.0f/7 : 6.0f/7;
        set(*detector,"Link Group",option=="--ipc-send" ? 5.0f/7 : pairGroup);
        stereo(*detector);
        check(detector->getLatencySamples()==0,"Detector latency must be zero");
        if (option=="--ipc-send") { apply(*detector,19); feed(*detector,nullptr,8); detector->releaseResources(); return 0; }
        auto receiver=load(juce::String::fromUTF8(argv[2])); set(*receiver,"Receive Scale",1); set(*receiver,"Link Group",pairGroup); stereo(*receiver);
        // Approve mode never sends an unapproved candidate.
        feed(*detector,receiver.get(),8,2);
        check(applied(*detector)==-1 && received(*receiver)==-1,"Approve mode changed scale without approval");
        apply(*detector,2); feed(*detector,receiver.get(),.3,2);
        check(ape::KeyAnalyser::scaleMask(received(*receiver))==ape::KeyAnalyser::scaleMask(2),"Approved scale did not reach tuner");
        auto saved=state(*detector); check(saved->getIntAttribute("appliedKey",-1)==2,"Applied key not saved");
        // UI creation/destruction must not drive inference.
        {
            std::unique_ptr<juce::AudioProcessorEditor> editor(detector->createEditorIfNeeded());
            check(editor && editor->getWidth()==800 && editor->getHeight()==590,"Detector editor invalid");
            juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
        }
        set(*detector,"Apply Mode",1); set(*detector,"Analysis Memory (s)",0);
        const float tunedHz=feed(*detector,receiver.get(),24,6);
        std::cout<<"automatic applied="<<applied(*detector)<<" received="<<received(*receiver)<<'\n';
        check(ape::KeyAnalyser::scaleMask(applied(*detector))==ape::KeyAnalyser::scaleMask(6),"Auto mode did not follow modulation");
        check(received(*receiver)==applied(*detector),"Auto key did not reach tuner");
        std::cout<<"F# major corrected output Hz="<<tunedHz<<'\n';
        check(std::abs(tunedHz-466.1638f)<3,"Received scale did not change corrected audio");
        set(*receiver,"Receive Scale",0);
        const float manualHz=feed(*detector,receiver.get(),1,6);
        check(std::abs(manualHz-440.0f)<3,"Receive OFF did not restore manual chromatic correction");
        set(*receiver,"Receive Scale",1);
        const int held=received(*receiver);
        set(*detector,"Send Scale",0); feed(*detector,receiver.get(),.3,0,true);
        check(received(*receiver)==held,"Disconnected sender erased accepted key");
        restore(*detector,*saved); feed(*detector,receiver.get(),.3,2);
        check(applied(*detector)==2 && received(*receiver)==2,"Saved approved scale not restored");
        set(*receiver,"Link Group",2.0f/7); feed(*detector,receiver.get(),.1,2);
        check(received(*receiver)==-1,"Group switch leaked previous key");
        detector->releaseResources(); receiver->releaseResources();
        std::cout<<"Actual VST3 pair: passthrough, approval, automatic modulation, stereo, state, disconnect and group switch passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
