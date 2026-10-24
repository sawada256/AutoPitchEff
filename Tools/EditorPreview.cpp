#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    juce::ScopedJuceInitialiser_GUI initialise;
    AutoPitchEffProcessor processor;
    processor.prepareToPlay(48000, 64);
    juce::AudioBuffer<float> buffer(1, 64); juce::MidiBuffer midi;
    for (int k = 0; k < 1000; ++k) {
        for (int i = 0; i < 64; ++i) buffer.setSample(0, i, .2f * static_cast<float>(std::sin(2 * ape::pi * 452 * (k * 64 + i) / 48000)));
        processor.processBlock(buffer, midi);
    }
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    juce::Thread::sleep(180);
    juce::Timer::callPendingTimersSynchronously();
    auto image = editor->createComponentSnapshot(editor->getLocalBounds());
    const auto path = juce::File::getCurrentWorkingDirectory().getChildFile(juce::String::fromUTF8(argv[1]));
    auto stream = path.createOutputStream();
    if (!stream || !stream->openedOk()) return 1;
    stream->setPosition(0); stream->truncate();
    juce::PNGImageFormat png;
    return png.writeImageToStream(image, *stream) ? 0 : 1;
}
