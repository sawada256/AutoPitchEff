#include "key/KeyAnalyser.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <random>
static void check(bool okay, const char* reason) { if (!okay) throw std::runtime_error(reason); }
static float chord(double t, int root, bool minor) {
    const int majorChords[8][3] = {{0,4,7},{5,9,12},{7,11,14},{0,4,7},{9,12,16},{2,5,9},{7,11,14},{0,4,7}};
    const int minorChords[8][3] = {{0,3,7},{5,8,12},{7,10,14},{0,3,7},{8,12,15},{10,14,17},{7,10,14},{0,3,7}};
    const auto& offsets = minor ? minorChords : majorChords;
    const int segment = static_cast<int>(t) % 8;
    const double envelope = std::min(1.0,(t-std::floor(t))*30);
    double value = 0;
    for (int n : offsets[segment]) {
        const double hz = ape::PitchEngine::midiToFrequency(48+root+n);
        for (int h = 1; h <= 6; ++h) value += .045*std::sin(2*ape::pi*hz*h*t+ .13*h)/std::pow(h,1.5);
    }
    return static_cast<float>(value*envelope);
}
static void runChords(ape::KeyAnalyser& a,int key,double seconds, double origin = 0, bool opposite = false) {
    for (int i = 0; i < static_cast<int>(seconds*ape::KeyAnalyser::sampleRate); ++i) {
        const double t = origin + i/ape::KeyAnalyser::sampleRate;
        const auto s = chord(t,key%12,key>=12); a.push(s,opposite ? -s : s*.7f);
    }
}
int main() {
    try {
        for (int key = 0; key < 24; ++key) {
            ape::KeyAnalyser a; a.configure(false,6,-55); runChords(a,key,24);
            const auto r = a.result();
            std::cout << "mix expected=" << key << " got=" << r.key << " confidence=" << r.confidence << " ready=" << r.ready << '\n';
            check(ape::KeyAnalyser::scaleMask(r.key)==ape::KeyAnalyser::scaleMask(key),"Wrong scale on harmonic progression");
            check(r.ready && r.confidence >= .6f,"Insufficient confidence on progression");
        }
        ape::KeyAnalyser opposite; runChords(opposite,2,24,0,true);
        check(ape::KeyAnalyser::scaleMask(opposite.result().key)==ape::KeyAnalyser::scaleMask(2),"Opposite polarity lost tonality");
        ape::KeyAnalyser modulation; modulation.configure(false,3,-55);
        runChords(modulation,0,16); check(ape::KeyAnalyser::scaleMask(modulation.result().key)==ape::KeyAnalyser::scaleMask(0),"Initial modulation key");
        runChords(modulation,6,24,16); check(ape::KeyAnalyser::scaleMask(modulation.result().key)==ape::KeyAnalyser::scaleMask(6),"Modulation not followed");
        ape::KeyAnalyser vocal; vocal.configure(true,5,-55);
        constexpr int melody[] = {0,0,4,7,0,2,5,9,11,7,0,0};
        for (int i = 0; i < 24*12000; ++i) {
            const double t = i/12000.0, hz = ape::PitchEngine::midiToFrequency(62+melody[static_cast<int>(t*2)%12]);
            const auto s = static_cast<float>(.12*std::sin(2*ape::pi*hz*t)+.04*std::sin(4*ape::pi*hz*t));
            vocal.push(s,s);
        }
        const auto vr = vocal.result();
        std::cout << "vocal expected=2 got=" << vr.key << " confidence=" << vr.confidence << '\n';
        check(ape::KeyAnalyser::scaleMask(vr.key)==ape::KeyAnalyser::scaleMask(2) && vr.ready,"Vocal melody failed");
        ape::KeyAnalyser rightVoice; rightVoice.configure(true,5,-55);
        for (int i = 0; i < 24*12000; ++i) {
            const double t = i/12000.0, hz = ape::PitchEngine::midiToFrequency(62+melody[static_cast<int>(t*2)%12]);
            rightVoice.push(0,static_cast<float>(.12*std::sin(2*ape::pi*hz*t)+.04*std::sin(4*ape::pi*hz*t)));
        }
        check(ape::KeyAnalyser::scaleMask(rightVoice.result().key)==ape::KeyAnalyser::scaleMask(2) && rightVoice.result().ready,"Right-only vocal lost detection");
        ape::KeyAnalyser ambiguous;
        for (int i = 0; i < 10*12000; ++i) { const float s = .2f*static_cast<float>(std::sin(2*ape::pi*440*i/12000)); ambiguous.push(s,s); }
        check(!ambiguous.result().ready,"Single tone must not qualify automatically");
        ape::KeyAnalyser noise; std::mt19937 random(12345); std::uniform_real_distribution<float> dist(-.2f,.2f);
        for (int i = 0; i < 10*12000; ++i) noise.push(dist(random),dist(random));
        check(!noise.result().ready,"Noise must not qualify");
        for (int i = 0; i < 12*12000; ++i) vocal.push(0,0);
        check(!vocal.result().ready && vocal.result().key < 0,"Silence must clear detection");
        vocal.push(NAN,INFINITY);
        std::cout << "Key analysis: 24 scales, vocal, modulation, polarity, ambiguity, noise, silence passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
