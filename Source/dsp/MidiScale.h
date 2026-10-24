#pragma once

namespace ape {
// Dynamic scale from MIDI note-ons. With `base` = MIDI number of "C0":
//   base+0..11  : key note. Same as a reset followed by all notes of that major
//                 scale (C0 = C major / A minor, D0 = D major / B minor, ...).
//   base+12..23 : add that pitch class (C1..B1) to the allowed notes.
//   base+24     : reset (C2). No notes allowed until a key or add note arrives.
// An empty mask means "inactive"; the caller then keeps its manual scale.
class MidiScale {
public:
    enum Kind { ignored, key, add, reset };
    static constexpr unsigned major(int root) noexcept {
        return ((0xAB5u << root) | (0xAB5u >> (12 - root))) & 0xFFFu;
    }
    static Kind kind(int note, int base) noexcept {
        const int offset = note - base;
        return offset < 0 || offset > 24 ? ignored : offset < 12 ? key : offset < 24 ? add : reset;
    }
    // Order of simultaneous events matters: callers apply key/reset before add.
    static int priority(int note, int base) noexcept { const auto k = kind(note, base); return k == key || k == reset ? 0 : 1; }
    bool handle(int note, int base) noexcept {
        const int offset = note - base;
        switch (kind(note, base)) {
            case key: mask = major(offset); root = offset; return true;
            case add: mask |= 1u << (offset - 12); return true;
            case reset: mask = 0; root = -1; return true;
            case ignored: break;
        }
        return false;
    }
    void clear() noexcept { mask = 0; root = -1; }
    unsigned mask = 0; // bit c allows pitch class c (C = bit 0)
    int root = -1;     // last key note since the last reset, for display
};

// Held MIDI notes that set the correction target directly. Last note priority:
// releasing the newest note returns to the previous one still held.
class MidiTarget {
public:
    void noteOn(int note) noexcept {
        noteOff(note);
        if (count == capacity) { for (int i = 1; i < count; ++i) held[i - 1] = held[i]; --count; }
        held[count++] = note;
    }
    void noteOff(int note) noexcept {
        int out = 0;
        for (int i = 0; i < count; ++i) if (held[i] != note) held[out++] = held[i];
        count = out;
    }
    void clear() noexcept { count = 0; }
    int current() const noexcept { return count > 0 ? held[count - 1] : -1; }
private:
    static constexpr int capacity = 16;
    int held[capacity] {};
    int count = 0;
};
}
