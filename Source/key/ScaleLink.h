#pragma once
#include <atomic>
#include <cstdint>
namespace ape {
struct LinkedScale { int key = -1; bool connected = false; };
// The mapping is opened outside the callback. All callback reads are bounded,
// lock-free and allocation-free. One publisher per user/group, up to 8 groups.
class ScaleLink {
public:
    ScaleLink();
    ~ScaleLink();
    bool available() const noexcept { return shared != nullptr; }
    bool publish(int group, int key, uint64_t now) noexcept;
    void release() noexcept;
    LinkedScale read(int group, uint64_t now) const noexcept;
    static uint64_t nowNanos() noexcept;
private:
    struct alignas(64) Slot {
        std::atomic<uint64_t> owner {0}, message {0}, heartbeat {0};
    };
    struct Shared { uint64_t magic = 0; Slot slots[8]; };
    Shared* shared = nullptr;
    uint64_t token = 0;
    int ownedGroup = -1;
};
}
