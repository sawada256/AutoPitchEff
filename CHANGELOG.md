# Changes

## 0.7.1

- Windows 10/11 x64 support: VST3 plug-ins, an installer (Inno Setup) and a ZIP. The C++ runtime is linked statically.
- AutoScaleLink ↔ AutoPitchEff linking works on Windows (shared memory in the user's session).
- CI builds and tests macOS and Windows, validates the Windows plug-ins with pluginval, and publishes both on tags.

## 0.7.0 — first public release

AutoPitchEff
- Low-latency live pitch correction (mono/stereo), Chromatic / Major / Minor, preset "High Female / Hard Tune".
- MIDI Key: C0–B0 selects a major key, C1–B1 adds notes, C2 clears (channel and octave naming selectable).
- MIDI Target: held MIDI note sets the target pitch (Nearest octave or Absolute); Max Pitch Shift 1–12 semitones.
- Formant preservation with no added latency (LPC, True Envelope or Auto), Formant Preserve / Formant Shift.
- Chorus tools: Detune, Humanize, Double Delay.
- Sidechain and Hybrid (main-first) pitch detection with host-reported alignment delay.
- Receives keys from AutoScaleLink.

AutoScaleLink
- Live key analysis (mix or vocal), approve or automatic mode, click-to-approve chroma bars, link groups.

Development history before the public release is not included in this repository.
