#include "key/KeyProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <thread>
#include <chrono>
#include <cmath>
#include <iostream>
int main(int argc,char** argv) {
    juce::ScopedJuceInitialiser_GUI initialise;
    AutoScaleLinkProcessor p; p.prepareToPlay(48000,256);
    juce::AudioBuffer<float> b(2,256); juce::MidiBuffer midi;
    const int chords[8][3]={{0,4,7},{5,9,12},{7,11,14},{0,4,7},{9,12,16},{2,5,9},{7,11,14},{0,4,7}};
    const auto start=std::chrono::steady_clock::now();
    for (int block=0;block<1500;++block) {
        for (int i=0;i<256;++i) {
            const double t=(block*256+i)/48000.0; double s=0;
            for (int n:chords[static_cast<int>(t)%8]) {
                const double hz=ape::PitchEngine::midiToFrequency(50+n);
                for (int h=1;h<=6;++h) s+=.045*std::sin(2*ape::pi*hz*h*t+.13*h)/std::pow(h,1.5);
            }
            b.setSample(0,i,static_cast<float>(s)); b.setSample(1,i,static_cast<float>(-s));
        }
        p.processBlock(b,midi);
        std::this_thread::sleep_until(start+std::chrono::microseconds(static_cast<long long>((block+1)*256*1000000.0/48000)));
    }
    const auto result=p.readResult();
    if (p.appliedKey()!=-1 || result.key<0 || result.evidenceSeconds<1) return 1;
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    bool clicked=false;
    for (int i=0;i<editor->getNumChildComponents();++i)
        if (auto* button=dynamic_cast<juce::TextButton*>(editor->getChildComponent(i)))
            if (button->getButtonText()=="Apply candidate" && button->isEnabled()) { button->onClick(); clicked=true; }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (!clicked || p.appliedKey()!=result.key) return 2;
    juce::MemoryBlock saved; p.getStateInformation(saved);
    if (argc==2) {
        editor.reset(); editor.reset(p.createEditor());
        auto image=editor->createComponentSnapshot(editor->getLocalBounds());
        auto stream=juce::File::getCurrentWorkingDirectory().getChildFile(juce::String::fromUTF8(argv[1])).createOutputStream();
        if (!stream || !stream->openedOk()) return 5;
        stream->setPosition(0); stream->truncate();
        juce::PNGImageFormat png; if (!png.writeImageToStream(image,*stream)) return 6;
    }
    p.approve(9); std::this_thread::sleep_for(std::chrono::milliseconds(50));
    p.setStateInformation(saved.getData(),static_cast<int>(saved.getSize()));
    if (p.appliedKey()!=result.key) return 3;
    if (p.droppedSamples()!=0) return 4;
    editor.reset(); p.releaseResources();
    std::cout<<"Approval UI handler, no unapproved application, state restore and no queue overflow passed\n";
}
