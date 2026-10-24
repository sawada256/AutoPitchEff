#include "dsp/PitchEngine.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <chrono>
#include <fstream>
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: ape-render input.wav output.wav [--min Hz] [--max Hz] [--threshold 0.55] [--gate -48] [--retune ms] [--stability ct] [--hold ms] [--mix 1] [--amount 1] [--bypass] [--detect sum|left|right]\nWrites output.wav.csv with linked detection telemetry. Mono/stereo channel layout is preserved.\n";
        return 2;
    }
    ape::Settings settings;
    try {
        for (int i = 3; i < argc; ++i) {
            const std::string key(argv[i]);
            if (key == "--bypass") { settings.enabled = false; continue; }
            if (key == "--detect") {
                if (++i >= argc) throw std::runtime_error("Missing detection source");
                const std::string source(argv[i]);
                if (source != "sum" && source != "left" && source != "right") throw std::runtime_error("Invalid detection source");
                settings.detectorSource = source == "left" ? 1 : source == "right" ? 2 : 0;
                continue;
            }
            if (i + 1 >= argc) throw std::runtime_error("Missing option value");
            const float value = std::stof(argv[++i]);
            if (!std::isfinite(value)) throw std::runtime_error("Non-finite option value");
            if (key == "--min") settings.minHz = value;
            else if (key == "--max") settings.maxHz = value;
            else if (key == "--threshold") settings.threshold = value;
            else if (key == "--gate") settings.gateDb = value;
            else if (key == "--retune") settings.retuneMs = value;
            else if (key == "--stability") settings.stabilityCents = value;
            else if (key == "--hold") settings.holdMs = value;
            else if (key == "--mix") settings.mix = value;
            else if (key == "--amount") settings.amount = value;
            else throw std::runtime_error("Unknown option: " + key);
        }
        juce::AudioFormatManager formats; formats.registerBasicFormats();
        const auto input = juce::File::getCurrentWorkingDirectory().getChildFile(juce::String::fromUTF8(argv[1]));
        const auto output = juce::File::getCurrentWorkingDirectory().getChildFile(juce::String::fromUTF8(argv[2]));
        if (input == output) throw std::runtime_error("Input and output must be different files");
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(input));
        if (!reader) throw std::runtime_error("Cannot read input audio");
        if (reader->numChannels < 1 || reader->numChannels > 2) throw std::runtime_error("Only mono and stereo inputs are supported");
        if (reader->sampleRate < 8000 || reader->sampleRate > 192000) throw std::runtime_error("Unsupported sample rate");
        std::unique_ptr<juce::FileOutputStream> stream(output.createOutputStream());
        if (!stream || !stream->openedOk()) throw std::runtime_error("Cannot open output");
        stream->setPosition(0); stream->truncate();
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.get(), reader->sampleRate, reader->numChannels, 24, {}, 0));
        if (!writer) throw std::runtime_error("Cannot create WAV writer");
        stream.release();
        std::ofstream csv(output.getFullPathName().toStdString() + ".csv");
        if (!csv) throw std::runtime_error("Cannot create telemetry CSV");
        csv << "time_s,input_hz,target_hz,confidence,shift_cents,read_delay_ms,state\n";
        ape::PitchEngine engine; engine.prepare(reader->sampleRate); engine.setSettings(settings);
        juce::AudioBuffer<float> buffer(static_cast<int>(reader->numChannels), 64), rendered(static_cast<int>(reader->numChannels), 64);
        double seconds = 0, maxBlock = 0; juce::int64 blocks = 0;
        for (juce::int64 pos = 0; pos < reader->lengthInSamples; pos += 64) {
            const int n = static_cast<int>(std::min<juce::int64>(64, reader->lengthInSamples - pos));
            if (!reader->read(&buffer, 0, n, pos, true, true)) throw std::runtime_error("Audio read failed");
            const auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < n; ++i) {
                float left = buffer.getSample(0, i);
                if (buffer.getNumChannels() == 2) {
                    float right = buffer.getSample(1, i); engine.processStereo(left, right);
                    rendered.setSample(0, i, left); rendered.setSample(1, i, right);
                } else rendered.setSample(0, i, engine.process(left));
            }
            const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            seconds += elapsed; maxBlock = std::max(maxBlock, elapsed); ++blocks;
            if (!writer->writeFromAudioSampleBuffer(rendered, 0, n)) throw std::runtime_error("WAV write failed");
            const auto m = engine.meter();
            csv << static_cast<double>(pos + n - 1) / reader->sampleRate << ',' << m.inputHz << ',' << m.targetHz << ','
                << m.confidence << ',' << m.correctionCents << ',' << m.delayMs << ',' << static_cast<int>(m.state) << '\n';
        }
        std::cout << "Rendered " << output.getFullPathName() << "\nDSP RTF=" << seconds / (static_cast<double>(reader->lengthInSamples) / reader->sampleRate)
                  << " mean block ms=" << seconds * 1000 / static_cast<double>(blocks) << " max block ms=" << maxBlock * 1000 << '\n';
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
