#include "dsp/PitchEngine.h"
#include "dsp/MidiScale.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>
#include <cstdlib>
#include <new>

static bool watchAllocations = false;
static size_t audioAllocations = 0;
void* operator new(std::size_t n) {
    if (watchAllocations) ++audioAllocations;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

static void check(bool yes, const char* message) { if (!yes) throw std::runtime_error(message); }
static double cents(double a, double b) { return 1200 * std::log2(a / b); }
// Independent, long-window normalized autocorrelation for measuring steady output F0.
static double measure(const std::vector<float>& x, double sr, double near) {
    const int start = static_cast<int>(x.size()) - static_cast<int>(sr * 0.3);
    const int n = static_cast<int>(sr * 0.2);
    const int lo = static_cast<int>(sr / (near * 1.08)), hi = static_cast<int>(sr / (near * 0.92));
    std::vector<double> corr(static_cast<size_t>(hi + 2));
    int peak = lo;
    for (int lag = lo - 1; lag <= hi + 1; ++lag) {
        double xy = 0, energy = 1e-20;
        for (int i = 0; i < n; ++i) {
            const double a = x[static_cast<size_t>(start + i)], b = x[static_cast<size_t>(start + i + lag)];
            xy += a * b; energy += a * a + b * b;
        }
        corr[static_cast<size_t>(lag)] = 2 * xy / energy;
        if (lag >= lo && lag <= hi && corr[static_cast<size_t>(lag)] > corr[static_cast<size_t>(peak)]) peak = lag;
    }
    const auto a = corr[static_cast<size_t>(peak - 1)], b = corr[static_cast<size_t>(peak)], c = corr[static_cast<size_t>(peak + 1)];
    const auto delta = 0.5 * (a - c) / (a - 2 * b + c);
    return sr / (peak + delta);
}
static float voice(double phase) {
    return static_cast<float>(0.20 * std::sin(phase) + 0.14 * std::sin(2 * phase + .3)
        + 0.08 * std::sin(3 * phase + .5) + 0.06 * std::sin(4 * phase + .2));
}
int main() {
    try {
        for (double sr : {44100.0, 48000.0, 96000.0}) {
            for (double f : {196.0, 233.0, 317.0, 452.0, 641.0, 937.0}) {
                ape::PitchEngine engine; engine.prepare(sr);
                std::vector<float> out;
                for (int i = 0; i < static_cast<int>(sr * .65); ++i)
                    out.push_back(engine.process(voice(2 * ape::pi * f * i / sr)));
                auto m = engine.meter();
                const auto target = ape::PitchEngine::midiToFrequency(std::round(ape::PitchEngine::frequencyToMidi(f)));
                const auto outputF0 = measure(out, sr, target);
                std::cout << "tone sr=" << sr << " in=" << f << " detected=" << m.inputHz << " confidence=" << m.confidence
                          << " out=" << outputF0 << " target=" << target << " cents=" << cents(outputF0, target) << '\n';
                check(m.inputHz > 0 && std::abs(cents(m.inputHz, f)) < 15, "Clean input pitch error");
                check(std::abs(cents(outputF0, target)) < 15, "Output not tuned to target");
            }
        }
        // Voice is louder than a low guitar, bass and noise. Count tracking coverage.
        ape::PitchEngine mix; mix.prepare(48000);
        std::mt19937 random(74); std::normal_distribution<float> noise(0, .012f);
        int valid = 0, total = 0;
        for (int i = 0; i < 48000; ++i) {
            const double t = i / 48000.0;
            float x = voice(2 * ape::pi * 452 * t) + .30f * voice(2 * ape::pi * 147 * t)
                + .10f * std::sin(static_cast<float>(2 * ape::pi * 82.4 * t)) + noise(random);
            mix.process(x);
            if (i > 4800 && i % 72 == 0) { ++total; const auto m = mix.meter(); if (m.inputHz > 0 && std::abs(cents(m.inputHz, 452)) < 30) ++valid; }
        }
        std::cout << "mixed tracking=" << valid << '/' << total << '\n';
        check(valid > total * .9, "Lost voice under moderate instrument bleed");
        // Capture/re-capture after silence and note jumps; inspect at the actual time.
        ape::PitchEngine jumps; jumps.prepare(48000);
        double phase = 0; int caught = -1;
        for (int i = 0; i < 48000; ++i) {
            const double f = i < 24000 ? 440 : 659.255;
            phase += 2 * ape::pi * f / 48000;
            jumps.process(voice(phase));
            if (i >= 24000 && caught < 0 && jumps.meter().inputHz > 0 && std::abs(cents(jumps.meter().inputHz, f)) < 25) caught = i - 24000;
        }
        std::cout << "Note jump capture ms=" << caught / 48.0 << '\n';
        check(caught >= 0 && caught < 1440, "Note jump did not re-capture within 30 ms");
        // A held note fluctuating across a semitone boundary must not chatter.
        ape::PitchEngine boundary; boundary.prepare(48000);
        double boundaryPhase = 0; int switches = 0; float lastTarget = 0;
        for (int i = 0; i < 144000; ++i) {
            const double t = i / 48000.0;
            const double f = 440 * std::exp2((45 + 24 * std::sin(2 * ape::pi * 7 * t)) / 1200);
            boundaryPhase += 2 * ape::pi * f / 48000;
            boundary.process(voice(boundaryPhase));
            const float target = boundary.meter().targetHz;
            if (i > 48000 && std::abs(target - lastTarget) > 1) ++switches;
            lastTarget = target;
        }
        check(switches == 0, "Held note chatters across semitone boundary");
        // Stability must still allow deliberate semitone and larger note changes.
        for (double interval : {1.0, 7.0}) {
            ape::PitchEngine steps; steps.prepare(48000);
            double stepPhase = 0; int acquired = -1;
            const double goal = 440 * std::exp2(interval / 12);
            for (int i = 0; i < 36000; ++i) {
                const double f = i < 24000 ? 440 : goal;
                stepPhase += 2 * ape::pi * f / 48000;
                steps.process(voice(stepPhase));
                if (i >= 24000 && acquired < 0 && std::abs(steps.meter().targetHz - goal) < 1)
                    acquired = i - 24000;
            }
            std::cout << "Target step " << interval << " semitones ms=" << acquired / 48.0 << '\n';
            check(acquired >= 0 && acquired < 4320, "Stable target does not follow intentional note step within 90 ms");
        }
        // Linked stereo must preserve panning and opposite polarity, without
        // cross-talk or separate splice positions on each side.
        for (int mode : {0, 1, 2}) {
            ape::PitchEngine stereo; stereo.prepare(48000);
            ape::Settings stereoSettings; stereoSettings.detectorSource = mode; stereo.setSettings(stereoSettings);
            std::vector<float> leftOut, rightOut;
            for (int i = 0; i < 48000; ++i) {
                float left = voice(2 * ape::pi * 452 * i / 48000), right = left * .3f;
                stereo.processStereo(left, right);
                check(std::abs(right - .3f * left) < 2e-6, "Linked stereo changed panning");
                leftOut.push_back(left); rightOut.push_back(right);
            }
            check(std::abs(cents(measure(leftOut, 48000, 440), 440)) < 15, "Stereo left not tuned");
            check(std::abs(cents(measure(rightOut, 48000, 440), 440)) < 15, "Stereo right not tuned");
        }
        ape::PitchEngine opposite; opposite.prepare(48000);
        ape::Settings oppositeSettings; oppositeSettings.detectorSource = 1; opposite.setSettings(oppositeSettings);
        for (int i = 0; i < 48000; ++i) {
            float left = voice(2 * ape::pi * 452 * i / 48000), right = -left;
            opposite.processStereo(left, right);
            check(std::abs(left + right) < 2e-6, "Linked stereo changed opposite polarity");
        }
        check(opposite.meter().inputHz > 0, "Left detection lost opposite-polarity stereo");
        ape::PitchEngine rightOnly; rightOnly.prepare(48000);
        for (int i = 0; i < 48000; ++i) {
            float left = 0, right = voice(2 * ape::pi * 452 * i / 48000);
            rightOnly.processStereo(left, right);
            check(left == 0, "Stereo introduced cross-talk into silent left channel");
        }
        check(rightOnly.meter().inputHz > 0 && std::abs(cents(rightOnly.meter().inputHz, 452)) < 15,
              "Summed detector lost right-only voice");
        // Regression: a changing ratio during a splice used to drive a fading
        // read head beyond its available past samples, flattening it by clamping.
        for (double safetyRate : {44100.0, 48000.0, 96000.0}) {
            ape::PeriodShifter shifting; shifting.prepare(safetyRate);
            double minimumAge = 1e6;
            for (int i = 0; i < static_cast<int>(safetyRate * 4); ++i) {
                const double changingRatio = 1 + .30 * std::sin(i * 2 * ape::pi / (safetyRate * .007));
                const auto output = shifting.processFrame({voice(2 * ape::pi * 452 * i / safetyRate), .01f}, 2,
                                                          changingRatio, safetyRate / 452);
                minimumAge = std::min(minimumAge, shifting.minimumReadAgeSamples());
                check(std::isfinite(output[0]) && std::isfinite(output[1]), "Fast correction produced invalid audio");
                check(minimumAge >= 3 - 1e-8, "Fading read head escaped safe history");
            }
            std::cout << "Read-head safety sr=" << safetyRate << " minimum samples=" << minimumAge << '\n';
            // Octave-wide ratios (Max pitch shift = 12) must stay inside recorded history too.
            ape::PeriodShifter wide; wide.prepare(safetyRate); wide.setRange(12);
            double wideAge = 1e6;
            for (int i = 0; i < static_cast<int>(safetyRate * 4); ++i) {
                const double changingRatio = std::exp2(std::sin(i * 2 * ape::pi / (safetyRate * .011)));
                const auto output = wide.processFrame({voice(2 * ape::pi * 233 * i / safetyRate), .01f}, 2, changingRatio, safetyRate / 233);
                wideAge = std::min(wideAge, wide.minimumReadAgeSamples());
                check(std::isfinite(output[0]) && std::isfinite(output[1]) && wideAge >= 3 - 1e-8, "Octave-wide shift escaped safe history");
            }
        }
        // Absolute targets up to an octave away are reached when Max pitch shift allows it,
        // and limited to the configured range otherwise.
        for (int semitones : {-12, -7, 7, 12}) for (float limit : {4.5f, 12.0f}) {
            ape::PitchEngine wideEngine; wideEngine.prepare(48000);
            ape::Settings wideSettings; wideSettings.maxShift = limit; wideEngine.setSettings(wideSettings);
            wideEngine.setForcedNote(69 + semitones, true);
            std::vector<float> out;
            for (int i = 0; i < 48000; ++i) out.push_back(wideEngine.process(voice(2 * ape::pi * 440 * i / 48000)));
            const double expected = 440 * std::exp2(std::clamp(static_cast<double>(semitones), -static_cast<double>(limit), static_cast<double>(limit)) / 12);
            check(std::abs(cents(measure(out, 48000, expected), expected)) < 5, "Max pitch shift not honoured");
        }
        // Changing the set of allowed notes must supersede old boundary
        // hysteresis, including when returning from a linked scale to chromatic.
        ape::PitchEngine scaleChange; scaleChange.prepare(48000);
        ape::Settings keyed; keyed.root = 6; keyed.scale = 1; scaleChange.setSettings(keyed);
        for (int i = 0; i < 24000; ++i) scaleChange.process(voice(2*ape::pi*452*i/48000));
        check(std::abs(scaleChange.meter().targetHz-466.1638f)<1,"F# major target incorrect");
        keyed.scale = 0; scaleChange.setSettings(keyed);
        for (int i = 24000; i < 28800; ++i) scaleChange.process(voice(2*ape::pi*452*i/48000));
        check(std::abs(scaleChange.meter().targetHz-440.0f)<1,"Old scale hysteresis survived scale change");
        // A custom pitch-class mask overrides root/scale: only G# (and C) allowed.
        keyed.noteMask = (1u << 8) | 1u; scaleChange.setSettings(keyed);
        for (int i = 28800; i < 52800; ++i) scaleChange.process(voice(2*ape::pi*452*i/48000));
        check(std::abs(scaleChange.meter().targetHz-415.3047f)<1,"Custom note mask target incorrect");
        keyed.noteMask = 0; scaleChange.setSettings(keyed);
        for (int i = 52800; i < 57600; ++i) scaleChange.process(voice(2*ape::pi*452*i/48000));
        check(std::abs(scaleChange.meter().targetHz-440.0f)<1,"Clearing note mask did not restore scale");
        // MIDI scale control: C0 = 24 with C3 = 60 naming.
        ape::MidiScale midiScale;
        check(midiScale.handle(26, 24) && midiScale.mask == ape::MidiScale::major(2) && midiScale.root == 2, "D0 did not select D major");
        check(ape::MidiScale::major(2) == 0xAD6u, "D major mask incorrect");
        check(midiScale.handle(24 + 12 + 8, 24) && midiScale.mask == (ape::MidiScale::major(2) | (1u << 8)), "G#1 was not added");
        check(midiScale.handle(48, 24) && midiScale.mask == 0 && midiScale.root == -1, "C2 did not reset");
        midiScale.handle(36 + 1, 24); midiScale.handle(36 + 6, 24);
        check(midiScale.mask == ((1u << 1) | (1u << 6)) && midiScale.root == -1, "Custom notes after reset incorrect");
        check(!midiScale.handle(23, 24) && !midiScale.handle(49, 24) && midiScale.mask == ((1u << 1) | (1u << 6)), "Out-of-range note changed scale");
        // MIDI target: held pitch class in the octave nearest the voice; release resumes the scale.
        ape::PitchEngine forced; forced.prepare(48000);
        ape::Settings cMajor; cMajor.scale = 1; forced.setSettings(cMajor);
        for (int i = 0; i < 24000; ++i) forced.process(voice(2*ape::pi*452*i/48000));
        check(std::abs(forced.meter().targetHz-440.0f)<1,"Scale target before MIDI target incorrect");
        forced.setForcedNote(61); // C#, outside C major, played two octaves low
        for (int i = 0; i < 4800; ++i) forced.process(voice(2*ape::pi*452*i/48000));
        check(std::abs(forced.meter().targetHz-554.3653f)<1,"MIDI target not used in nearest octave");
        forced.setForcedNote(-1);
        for (int i = 0; i < 4800; ++i) forced.process(voice(2*ape::pi*452*i/48000));
        check(std::abs(forced.meter().targetHz-440.0f)<1,"Releasing MIDI target did not resume scale");
        forced.setForcedNote(61, true); // absolute: exactly C#4, not the nearest-octave C#5
        for (int i = 0; i < 4800; ++i) forced.process(voice(2*ape::pi*452*i/48000));
        check(std::abs(forced.meter().targetHz-277.1826f)<1,"Absolute MIDI target not exact");
        forced.setForcedNote(-1);
        ape::MidiTarget held;
        held.noteOn(60); held.noteOn(64); held.noteOn(67); held.noteOff(67);
        check(held.current() == 64, "Released newest note did not return to previous");
        held.noteOff(60); held.noteOff(64);
        check(held.current() == -1, "Released notes still held");
        for (int n = 0; n < 40; ++n) held.noteOn(n);
        check(held.current() == 39, "Held-note overflow lost the newest note");
        check(ape::MidiScale::priority(24, 24) < ape::MidiScale::priority(44, 24) && ape::MidiScale::priority(48, 24) < ape::MidiScale::priority(44, 24), "Key/reset must precede adds");
        // Every operation used inside the audio callback must avoid dynamic allocation.
        ape::PitchEngine realtime; realtime.prepare(48000);
        watchAllocations = true;
        for (int i = 0; i < 48000; ++i) {
            if (i % 64 == 0) realtime.setSettings(ape::Settings{});
            float left = voice(2 * ape::pi * 452 * i / 48000), right = left * .3f;
            realtime.processStereo(left, right);
        }
        watchAllocations = false;
        check(audioAllocations == 0, "Audio processing allocated memory");
        // ---- Formant processing and chorus controls ----
        {
            // A vowel with fixed formants (850/1220/2810 Hz): harmonics weighted by the envelope.
            auto vowelGain = [](double hz) {
                double g = 1 / (1 + hz / 300);
                for (double f : {850.0, 1220.0, 2810.0}) g *= 1 / std::sqrt(std::pow(1 - hz * hz / (f * f), 2) + std::pow(hz * .12 / f, 2));
                return g;
            };
            auto vowel = [&](double f0, int length) {
                std::vector<float> x(static_cast<size_t>(length));
                for (int i = 0; i < length; ++i) {
                    double v = 0;
                    for (int k = 1; k * f0 < 9000; ++k) v += vowelGain(k * f0) * std::sin(2 * ape::pi * k * f0 * i / 48000 + k * k * .7);
                    x[static_cast<size_t>(i)] = static_cast<float>(.02 * v);
                }
                return x;
            };
            // Envelope distance (offset-normalised RMS dB, 300-4000 Hz) between two signals' LPC envelopes,
            // optionally comparing b's envelope at f*warp with a's at f.
            auto envelopeDistance = [](const std::vector<float>& a, const std::vector<float>& b, double warp) {
                ape::FormantFilter ea, eb; ea.prepare(48000); eb.prepare(48000); ea.setMode(1); eb.setMode(1);
                for (size_t i = 0; i < a.size(); ++i) { ea.analyse({a[i], 0}, 1, a[i], 0); eb.analyse({b[i], 0}, 1, b[i], 0); }
                std::vector<double> d;
                for (double hz = 300; hz < 4000; hz += 25) d.push_back(eb.envelopeDb(hz * warp) - ea.envelopeDb(hz));
                double mean = 0; for (double v : d) mean += v; mean /= static_cast<double>(d.size());
                double e = 0; for (double v : d) e += (v - mean) * (v - mean);
                return std::sqrt(e / static_cast<double>(d.size()));
            };
            // Analysis followed by synthesis at the same (integer) age is an exact inverse.
            for (int mode : {1, 2, 3}) {
                ape::FormantFilter identity; identity.prepare(48000); identity.setMode(mode);
                const auto x = vowel(330, 24000);
                std::array<float, 16> line {}; double worst = 0;
                for (int i = 0; i < 24000; ++i) {
                    const auto e = identity.analyse({x[static_cast<size_t>(i)], 0}, 1, x[static_cast<size_t>(i)], 330);
                    line[static_cast<size_t>(i & 15)] = e[0];
                    const auto y = identity.synthesise({line[static_cast<size_t>((i - 8) & 15)], 0}, 1, 8, 1);
                    if (i >= 8) worst = std::max(worst, std::abs(static_cast<double>(y[0]) - x[static_cast<size_t>(i - 8)]));
                }
                check(worst < 1e-4, "Formant analysis/synthesis is not an exact inverse");
            }
            // Shift +7 st by MIDI target: with formant preservation the output envelope stays near the input's.
            const auto input = vowel(330, 48000);
            auto run = [&](int mode, float preserve, float formantShift, float detune, float humanize, float delayMs, int semitones) {
                ape::PitchEngine engine; engine.prepare(48000);
                ape::Settings s; s.maxShift = 12; s.formantMode = mode; s.formantPreserve = preserve; s.formantShift = formantShift;
                s.detuneCents = detune; s.humanizeCents = humanize; s.wetDelayMs = delayMs; engine.setSettings(s);
                engine.setForcedNote(64 + semitones, true); // 330 Hz is E4 (64) plus ~2 ct
                std::vector<float> out(input.size());
                for (size_t i = 0; i < input.size(); ++i) out[i] = engine.process(input[i]);
                return out;
            };
            auto tail = [](const std::vector<float>& x) { return std::vector<float>(x.end() - 19200, x.end()); };
            const auto inputTail = tail(input);
            const double off = envelopeDistance(inputTail, tail(run(0, 1, 0, 0, 0, 0, 7)), 1);
            for (int mode : {1, 2, 3}) {
                const double kept = envelopeDistance(inputTail, tail(run(mode, 1, 0, 0, 0, 0, 7)), 1);
                std::cout << "Formant mode " << mode << " envelope distance " << kept << " dB (off " << off << " dB)\n";
                check(kept < off * .5 && kept < 4, "Formant preservation did not keep the envelope");
            }
            const double followed = envelopeDistance(tail(run(0, 1, 0, 0, 0, 0, 7)), tail(run(1, 0, 0, 0, 0, 0, 7)), 1);
            check(followed < 3, "Formant Preserve 0% does not follow the pitch like Off");
            const auto raised = tail(run(1, 1, 4, 0, 0, 0, 7));
            const double warpFactor = std::exp2(4.0 / 12);
            check(envelopeDistance(inputTail, raised, warpFactor) < envelopeDistance(inputTail, raised, 1) * .6, "Formant Shift did not move the envelope");
            // Detune offsets the corrected pitch; Double Delay delays only the wet signal exactly.
            const auto detuned = run(1, 1, 0, 20, 0, 0, 7);
            const double expected = ape::PitchEngine::midiToFrequency(71) * std::exp2(20.0 / 1200);
            check(std::abs(cents(measure(detuned, 48000, expected), expected)) < 3, "Detune not applied");
            const auto plain = run(1, 1, 0, 0, 0, 0, 7), late = run(1, 1, 0, 0, 0, 12.5, 7);
            double delayError = 0;
            for (size_t i = 9600; i < plain.size(); ++i) delayError = std::max(delayError, static_cast<double>(std::abs(late[i] - plain[i - 600])));
            check(delayError < 1e-5, "Double Delay is not an exact wet delay");
            // Humanize is bounded and deterministic.
            const auto human = run(1, 1, 0, 0, 25, 0, 7), again = run(1, 1, 0, 0, 25, 0, 7);
            check(human == again, "Humanize is not deterministic");
            const double humanCents = cents(measure(human, 48000, ape::PitchEngine::midiToFrequency(71)), ape::PitchEngine::midiToFrequency(71));
            check(std::abs(humanCents) < 26, "Humanize exceeded its depth");
            // No allocation with every formant/chorus feature running.
            ape::PitchEngine live; live.prepare(48000);
            ape::Settings liveSettings; liveSettings.maxShift = 12; liveSettings.formantMode = 3; liveSettings.formantShift = -3;
            liveSettings.formantPreserve = .7f; liveSettings.detuneCents = 7; liveSettings.humanizeCents = 5; liveSettings.wetDelayMs = 20;
            live.setSettings(liveSettings); live.setForcedNote(71, true);
            const auto highVoice = vowel(660, 48000);
            const auto start = std::chrono::steady_clock::now();
            watchAllocations = true;
            for (int i = 0; i < 48000; ++i) {
                if (i % 64 == 0) live.setSettings(liveSettings);
                float left = i < 24000 ? input[static_cast<size_t>(i)] : highVoice[static_cast<size_t>(i)], right = left * .3f;
                live.processStereo(left, right);
                check(std::isfinite(left) && std::abs(right - .3f * left) < 1e-4f, "Formant stereo output invalid or unlinked");
            }
            watchAllocations = false;
            std::cout << "Formant Auto stereo CPU seconds per audio second="
                      << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() << '\n';
            check(audioAllocations == 0, "Formant processing allocated memory");
        }
        // Reset must clear history and reproduce the same samples.
        std::array<float, 4096> first {}, second {};
        realtime.reset();
        for (size_t i = 0; i < first.size(); ++i) first[i] = realtime.process(voice(2 * ape::pi * 452 * static_cast<double>(i) / 48000));
        realtime.reset();
        for (size_t i = 0; i < second.size(); ++i) second[i] = realtime.process(voice(2 * ape::pi * 452 * static_cast<double>(i) / 48000));
        check(first == second, "Reset is not deterministic");
        // Silence, invalid samples, parameter extremes, and fixed bypass latency.
        ape::PitchEngine bypass; bypass.prepare(48000);
        ape::Settings s; s.enabled = false; bypass.setSettings(s);
        for (int i = 0; i < 100; ++i) {
            const auto y = bypass.process(i == 0 ? 1.0f : 0.0f);
            check(std::abs(y - (i == bypass.latencySamples() ? 1.0f : 0.0f)) < 1e-6, "Incorrect bypass latency");
        }
        check(std::isfinite(bypass.process(std::numeric_limits<float>::quiet_NaN())), "NaN escaped");
        for (int i = 0; i < 48000; ++i) check(std::isfinite(bypass.process(0)), "Silence produced invalid output");
        check(bypass.meter().state == ape::TrackingState::silent, "Silence remains voiced");
        s.minHz = 600; s.maxHz = 180; s.threshold = std::numeric_limits<float>::infinity(); bypass.setSettings(s);
        for (int i = 0; i < 4000; ++i) check(std::isfinite(bypass.process(noise(random))), "Extreme settings invalid");
        // Main-first routing, continuous failure wait, warm sidechain, and
        // immediate recovery, independently checked against main detector frames.
        for(double rate : {44100.0,48000.0,96000.0}) for(float wait : {0.0f,25.0f,200.0f}) for(int channels : {1,2}) {
            ape::PitchEngine hybrid; hybrid.prepare(rate);
            ape::Settings config; config.fallbackMs=wait; hybrid.setSettings(config);
            ape::PitchDetector observer; observer.prepare(rate); observer.setSettings(config);
            int failed=-1, switched=-1, recovered=-1;
            watchAllocations=true; audioAllocations=0;
            for(int n=0;n<static_cast<int>(rate);++n) {
                const bool voiced=n<static_cast<int>(rate*.25) || n>=static_cast<int>(rate*.65);
                float left=voiced ? voice(2*ape::pi*452*n/rate) : 0;
                float right=.5f*left;
                const bool frame=observer.push(channels == 2 ? .75f*left : left);
                hybrid.processHybrid(left,right,channels,voice(2*ape::pi*603*n/rate));
                const auto m=hybrid.meter();
                check(std::isfinite(left) && std::isfinite(right),"Hybrid produced invalid audio");
                if(channels==2) check(std::abs(right-.5f*left)<2e-6f,"Hybrid changed stereo panning");
                if(n>rate*.15 && n<rate*.24) {
                    check(!m.usingSidechain && std::abs(m.inputHz-452)<2,"Hybrid did not prefer tracked main");
                }
                if(frame && n>=rate*.25 && n<rate*.65 && failed<0 && observer.meter().state!=ape::TrackingState::tracking) failed=n;
                if(failed>=0 && n<rate*.65 && m.usingSidechain && switched<0) switched=n;
                if(switched>=0 && n<rate*.65 && m.usingSidechain)
                    check(std::abs(m.inputHz-603)<2,"Hybrid fallback did not use warm sidechain");
                if(frame && n>=rate*.65 && observer.meter().state==ape::TrackingState::tracking && recovered<0) {
                    recovered=n;
                    check(!m.usingSidechain && std::abs(m.inputHz-observer.meter().inputHz)<1,"Hybrid recovery delayed main preference");
                }
            }
            watchAllocations=false;
            check(audioAllocations==0,"Hybrid allocated in audio processing");
            check(failed>=0 && switched>=0 && recovered>=0,"Hybrid transition was not exercised");
            const double actualMs=(switched-failed)*1000.0/rate;
            const double hopMs=observer.hopSize()*1000.0/observer.rate();
            check(actualMs>=wait-.01 && actualMs<=wait+hopMs+.01,"Hybrid failure wait was not respected");
            if(wait==0) check(switched==failed,"Zero wait did not switch on first failed frame");
            // Missing reference expires instead of inventing a pitch.
            for(int n=0;n<static_cast<int>(rate*.25);++n) { float l=0,r=0; hybrid.processHybrid(l,r,channels,0); }
            check(hybrid.meter().inputHz==0,"Hybrid retained pitch with both sources silent");
            std::cout<<"Hybrid channels="<<channels<<" rate="<<rate<<" wait="<<wait<<" observed_after_first_failure_ms="<<actualMs<<" passed\n";
        }
        ape::PitchEngine benchmark; benchmark.prepare(48000);
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 48000; ++i) benchmark.process(voice(2 * ape::pi * 452 * i / 48000));
        const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::cout << "CPU wall seconds per audio second=" << seconds << "\nAll DSP checks passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
