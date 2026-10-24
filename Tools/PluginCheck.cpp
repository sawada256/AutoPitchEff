#include <juce_audio_utils/juce_audio_utils.h>
#include <iostream>
#include <cmath>
#include <stdexcept>
#include "GuiStress.h"

static void require(bool okay, const char* message) { if (!okay) throw std::runtime_error(message); }
int main(int argc, char** argv) {
    if (argc != 2 && argc != 3) { std::cerr << "Usage: ape-plugin-check /absolute/AutoPitchEff.vst3 [--gui-stress|--gui-stress-stereo]\n"; return 2; }
    juce::ScopedJuceInitialiser_GUI juce;
    try {
        juce::VST3PluginFormat format;
        juce::OwnedArray<juce::PluginDescription> descriptions;
        format.findAllTypesForFile(descriptions, juce::String::fromUTF8(argv[1]));
        require(descriptions.size() == 1, "VST3 discovery failed");
        juce::String error;
        auto instance = format.createInstanceFromDescription(*descriptions[0], 48000, 64, error);
        if (!instance) throw std::runtime_error(error.toStdString());
        if (argc == 3) {
            const std::string option(argv[2]);
            if (option == "--gui-stress-stereo" || option == "--gui-stress-hybrid") {
                auto buses = instance->getBusesLayout();
                buses.inputBuses.set(0, juce::AudioChannelSet::stereo());
                buses.outputBuses.set(0, juce::AudioChannelSet::stereo());
                if(option == "--gui-stress-hybrid") buses.inputBuses.set(1,juce::AudioChannelSet::stereo());
                require(instance->setBusesLayout(buses), "Stereo layout rejected for GUI test");
                if(option == "--gui-stress-hybrid") {
                    bool found=false;
                    for(auto* p : instance->getParameters()) if(p->getName(80)=="Hybrid Detection") { p->setValueNotifyingHost(1); found=true; }
                    require(found,"Missing Hybrid parameter for stress test");
                }
            } else if (option != "--gui-stress") throw std::runtime_error("Unknown option");
            return guiStress(*instance);
        }
        require(instance->getTotalNumInputChannels() == 1, "Unexpected input layout");
        require(instance->getTotalNumOutputChannels() == 1, "Unexpected output layout");
        require(instance->acceptsMidi() && !instance->producesMidi(), "Missing optional MIDI key input");
        instance->prepareToPlay(48000, 64);
        auto& parameters = instance->getParameters();
        require(parameters.size() >= 16, "Missing parameters");
        juce::MemoryBlock saved;
        auto* changed = parameters[0];
        const auto original = changed->getValue();
        instance->getStateInformation(saved);
        changed->setValueNotifyingHost(.75f);
        instance->setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
        require(std::abs(changed->getValue() - original) < .001f, "State roundtrip failed");
        juce::MidiBuffer midi;
        double energy = 0; int samples = 0;
        for (double sr : {44100.0, 48000.0, 96000.0}) {
            for (int blockSize : {1, 32, 64, 127, 256, 1024}) {
                instance->releaseResources(); instance->setRateAndBufferSizeDetails(sr, blockSize); instance->prepareToPlay(sr, blockSize);
                juce::AudioBuffer<float> buffer(1, blockSize);
                for (int block = 0; block < std::max(8, static_cast<int>(sr * .15) / blockSize); ++block) {
                    for (int i = 0; i < blockSize; ++i) buffer.setSample(0, i, .2f * static_cast<float>(std::sin(2 * 3.141592653589793 * 452 * samples++ / sr)));
                    instance->processBlock(buffer, midi);
                    for (int i = 0; i < blockSize; ++i) { const auto s = buffer.getSample(0, i); require(std::isfinite(s) && std::abs(s) < 2, "Invalid VST3 output"); energy += s * s; }
                }
                std::cout << "VST3 process sr=" << sr << " block=" << blockSize << " passed\n";
            }
        }
        instance->releaseResources();
        auto stereoLayout = instance->getBusesLayout();
        stereoLayout.inputBuses.set(0, juce::AudioChannelSet::stereo());
        stereoLayout.outputBuses.set(0, juce::AudioChannelSet::stereo());
        require(instance->setBusesLayout(stereoLayout), "Stereo layout rejected");
        instance->setRateAndBufferSizeDetails(48000, 64); instance->prepareToPlay(48000, 64);
        juce::AudioBuffer<float> stereoBuffer(2, 64);
        double rightEnergy = 0;
        for (int block = 0; block < 750; ++block) {
            for (int i = 0; i < 64; ++i) {
                const auto value = .2f * static_cast<float>(std::sin(2 * 3.141592653589793 * 452 * (block * 64 + i) / 48000));
                stereoBuffer.setSample(0, i, value); stereoBuffer.setSample(1, i, value * .3f);
            }
            instance->processBlock(stereoBuffer, midi);
            for (int i = 0; i < 64; ++i) {
                const auto left = stereoBuffer.getSample(0, i), right = stereoBuffer.getSample(1, i);
                require(std::isfinite(left) && std::isfinite(right), "Invalid stereo output");
                require(std::abs(right - left * .3f) < 2e-6f, "Stereo panning changed");
                rightEnergy += right * right;
            }
        }
        require(rightEnergy > 1, "Stereo right channel missing");
        std::cout << "VST3 linked stereo processing and panning passed\n";
        require(energy > 1, "No output audio");
        require(instance->getLatencySamples() == 8, "Incorrect fixed latency report");
        auto findParameter = [&](const juce::String& name) {
            for (auto* p : parameters) if (p->getName(100)==name) return p;
            throw std::runtime_error("Missing parameter: " + name.toStdString());
        };
        auto* pitchInput = findParameter("Pitch Input");
        auto* alignment = findParameter("Main Alignment Delay");
        auto* detection = findParameter("Stereo Detection");
        require(!alignment->isAutomatable(),"Alignment must not be automated");
        require(instance->getBusCount(true)==2,"Missing sidechain bus");
        pitchInput->setValueNotifyingHost(1);
        for (int mainChannels : {1,2}) for (int sideChannels : {1,2}) {
            instance->releaseResources();
            auto layout = instance->getBusesLayout();
            layout.inputBuses.set(0,mainChannels==1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
            layout.outputBuses.set(0,layout.inputBuses[0]);
            layout.inputBuses.set(1,sideChannels==1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
            require(instance->setBusesLayout(layout),"Sidechain layout rejected");
            detection->setValueNotifyingHost(1); // Right for stereo sidechain; ignored for mono.
            instance->setRateAndBufferSizeDetails(48000,64); instance->prepareToPlay(48000,64);
            juce::AudioBuffer<float> audio(mainChannels+sideChannels,64);
            float previous=0; int crossings=0;
            for (int block=0;block<1500;++block) {
                audio.clear();
                for (int i=0;i<64;++i) {
                    const double t=(block*64+i)/48000.0;
                    audio.setSample(0,i,static_cast<float>(.3*std::sin(2*3.141592653589793*452*t)));
                    if (mainChannels==2) audio.setSample(1,i,.3f*audio.getSample(0,i));
                    audio.setSample(mainChannels+sideChannels-1,i,static_cast<float>(.4*std::sin(2*3.141592653589793*440*t)));
                }
                instance->processBlock(audio,midi);
                for (int i=0;i<64;++i) {
                    const float value=audio.getSample(0,i);
                    if (block>=750 && previous<=0 && value>0) ++crossings;
                    previous=value;
                    if (mainChannels==2) require(std::abs(audio.getSample(1,i)-.3f*value)<2e-6f,"Sidechain stereo link failed");
                }
            }
            // Reference is already in tune, so main 452 Hz must remain 452 Hz.
            // Main detection would instead correct it to 440 Hz.
            require(std::abs(crossings-452)<3,"VST3 did not detect external reference");
            std::cout << "VST3 main="<<mainChannels<<" sidechain="<<sideChannels<<" external pitch passed\n";
        }
        auto* hybrid=findParameter("Hybrid Detection");
        auto* fallback=findParameter("Hybrid Fallback Wait");
        hybrid->setValueNotifyingHost(1); fallback->setValueNotifyingHost(0);
        detection->setValueNotifyingHost(0); // Sum, cancelled during middle phase.
        instance->prepareToPlay(48000,64);
        juce::AudioBuffer<float> hybridAudio(4,64);
        for(int phase=0;phase<3;++phase) {
            int crossings=0; float previous=0;
            for(int block=0;block<1125;++block) {
                for(int i=0;i<64;++i) {
                    const double t=(phase*1125*64+block*64+i)/48000.0;
                    const float main=static_cast<float>(.3*std::sin(2*3.141592653589793*452*t));
                    hybridAudio.setSample(0,i,main); hybridAudio.setSample(1,i,phase==1 ? -main : main);
                    hybridAudio.setSample(2,i,static_cast<float>(.4*std::sin(2*3.141592653589793*430*t))); hybridAudio.setSample(3,i,0);
                }
                instance->processBlock(hybridAudio,midi);
                for(int i=0;i<64;++i) {
                    const float value=hybridAudio.getSample(0,i);
                    if(block>=375 && previous<=0 && value>0) ++crossings;
                    previous=value;
                }
            }
            const double expected=phase==1 ? 452.0*440.0/430.0 : 440.0;
            require(std::abs(crossings-expected)<3,"Actual VST3 Hybrid source routing failed");
        }
        hybrid->setValueNotifyingHost(0);
        std::cout << "VST3 Hybrid main preference, cancelled-main fallback and recovery passed\n";
        alignment->setValueNotifyingHost(.5f); // 25 ms at 48 kHz.
        juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
        require(instance->getLatencySamples()==1208,"Live host latency notification failed");
        instance->reset();
        juce::AudioBuffer<float> delayed(4,64);
        int impulse=-1;
        for (int block=0;block<24;++block) {
            delayed.clear(); if(block==0) delayed.setSample(0,0,1);
            instance->processBlockBypassed(delayed,midi);
            for(int i=0;i<64;++i) if(std::abs(delayed.getSample(0,i))>.5f) impulse=block*64+i;
        }
        require(impulse==1208,"VST3 bypass delay does not match report");
        std::cout << "VST3 live PDC notification and bypass alignment passed\n";
        {
            std::unique_ptr<juce::AudioProcessorEditor> editor(instance->createEditorIfNeeded());
            require(editor != nullptr, "Editor creation failed");
            juce::MessageManager::getInstance()->runDispatchLoopUntil(120);
            require(editor->getWidth() == 1280 && editor->getHeight() == 750, "Incorrect editor bounds");
        }
        instance->releaseResources();
        std::cout << "VST3 discovery, state restore, processing, latency and editor checks passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
