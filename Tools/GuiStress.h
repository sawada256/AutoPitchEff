#pragma once
#include <thread>
#include <chrono>
#include <ctime>
#include <algorithm>

#if JUCE_MAC
void prepareNativeBench();
void showNativeBench(void*);
void logNativeBench(void*);
#endif
static constexpr double apePi = 3.14159265358979323846;

// Separate audio thread and real native editor, without opening an audio device.
// Wall-time includes OS scheduling; this is not a Cubase or hardware latency test.
static double threadCpu() {
    timespec t {}; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}
class Heartbeat final : private juce::Timer {
public:
    std::vector<double> gaps;
    void begin() { gaps.clear(); gaps.reserve(5000); previous = juce::Time::getMillisecondCounterHiRes(); startTimer(5); }
    void end() { stopTimer(); }
private:
    void timerCallback() override {
        const auto now = juce::Time::getMillisecondCounterHiRes(); gaps.push_back(now-previous); previous = now;
    }
    double previous = 0;
};
static int guiStress(juce::AudioPluginInstance& instance) {
#if JUCE_MAC
    prepareNativeBench();
#endif
    instance.setRateAndBufferSizeDetails(48000, 64); instance.prepareToPlay(48000, 64);
    std::atomic<bool> stop {false}, invalid {false};
    double audioCpu = 0; std::vector<double> blocks; blocks.reserve(40000);
    std::thread audio([&] {
        juce::ScopedNoDenormals denormals;
        juce::AudioBuffer<float> buffer(instance.getTotalNumInputChannels(), 64); juce::MidiBuffer midi;
        double phase = 0; int frame = 0;
        const auto cpuStart = threadCpu();
        auto deadline = std::chrono::steady_clock::now();
        while (!stop.load()) {
            for (int i = 0; i < 64; ++i) {
                const double f = 452 * std::exp2(.02 * std::sin(frame++ * 2*apePi*5/48000));
                phase += 2*apePi*f/48000;
                const float value = float(.2*std::sin(phase)+.1*std::sin(2*phase));
                buffer.setSample(0, i, value);
                if (buffer.getNumChannels() > 1) buffer.setSample(1, i, value * .3f);
                for(int c=2;c<buffer.getNumChannels();++c) buffer.setSample(c,i,value*(c%2 == 0 ? 1.0f : .3f));
            }
            const auto start = std::chrono::steady_clock::now();
            instance.processBlock(buffer, midi);
            blocks.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-start).count());
            for (int c = 0; c < buffer.getNumChannels(); ++c)
                for (int i = 0; i < 64; ++i) if (!std::isfinite(buffer.getSample(c, i))) invalid.store(true);
            deadline += std::chrono::nanoseconds(1333333);
            std::this_thread::sleep_until(deadline);
        }
        audioCpu = threadCpu()-cpuStart;
    });
    struct JoinOnExit {
        std::atomic<bool>& stopped; std::thread& worker;
        ~JoinOnExit() { stopped.store(true); if (worker.joinable()) worker.join(); }
    } joinOnExit {stop, audio};
    auto phaseTest = [](const char* name, void* nativeHandle = nullptr) {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(1000);
#if JUCE_MAC
        if (nativeHandle) logNativeBench(nativeHandle);
#endif
        Heartbeat heartbeat; heartbeat.begin();
        const auto start = threadCpu();
        juce::MessageManager::getInstance()->runDispatchLoopUntil(6000);
        const auto cpu = threadCpu()-start;
        heartbeat.end(); std::sort(heartbeat.gaps.begin(), heartbeat.gaps.end());
        const auto n = heartbeat.gaps.size();
        std::cout << name << " main_cpu_ms=" << cpu*1000 << " heartbeat_p99_ms="
                  << (n ? heartbeat.gaps[size_t(n*.99)] : -1) << " heartbeat_max_ms="
                  << (n ? heartbeat.gaps.back() : -1) << std::endl;
    };
    phaseTest("closed");
    {
        juce::DocumentWindow window(instance.getName()+" performance test", juce::Colours::black, 0);
        window.setUsingNativeTitleBar(true);
        window.setContentOwned(instance.createEditorIfNeeded(), true);
        window.centreWithSize(window.getWidth(), window.getHeight()); window.setVisible(true);
        juce::Process::makeForegroundProcess(); window.toFront(true);
#if JUCE_MAC
        showNativeBench(window.getPeer()->getNativeHandle());
#endif
        phaseTest("visible", window.getPeer()->getNativeHandle());
        window.setVisible(false);
        phaseTest("hidden", window.getPeer()->getNativeHandle());
        window.clearContentComponent();
    }
    stop.store(true); audio.join(); instance.releaseResources();
    std::sort(blocks.begin(), blocks.end());
    std::cout << "audio_cpu_ms=" << audioCpu*1000 << " block_p99_ms=" << blocks[size_t(blocks.size()*.99)]
              << " block_max_ms=" << blocks.back() << " invalid=" << invalid.load() << std::endl;
    return invalid.load() ? 1 : 0;
}
