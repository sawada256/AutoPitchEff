#include "ScaleLink.h"
#include <chrono>
#include <new>
#include <cstdio>
#if defined(_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#else
 #include <fcntl.h>
 #include <sys/mman.h>
 #include <sys/file.h>
 #include <sys/stat.h>
 #include <unistd.h>
 #include <signal.h>
 #include <cerrno>
#endif

namespace ape {
static_assert(std::atomic<uint64_t>::is_always_lock_free, "Scale link requires lock-free shared atomics");
namespace { constexpr uint64_t magic = 0x41504553434c3031ULL, expiry = 3000000000ULL; }
uint64_t ScaleLink::nowNanos() noexcept {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
#if defined(_WIN32)
namespace {
// A process that cannot be opened is treated as alive unless Windows reports
// that the id no longer exists: never steal a lease from a live writer.
bool processAlive(uint64_t owner) noexcept {
    const DWORD pid = static_cast<DWORD>(owner >> 32);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process == nullptr) return GetLastError() != ERROR_INVALID_PARAMETER;
    DWORD code = 0;
    const bool alive = GetExitCodeProcess(process, &code) == 0 || code == STILL_ACTIVE;
    CloseHandle(process);
    return alive;
}
}
ScaleLink::ScaleLink() {
    static_assert(sizeof(Shared)==576,"Shared ABI must match on all architectures");
    static std::atomic<uint64_t> serial {1};
    token = (static_cast<uint64_t>(GetCurrentProcessId()) << 32) | ((nowNanos() ^ serial.fetch_add(1)) & 0xffffffffULL);
    if (token == 0) token = 1;
    // Objects in the Local namespace are per logon session, matching the per-user POSIX name.
    HANDLE initLock = CreateMutexW(nullptr, FALSE, L"Local\\ape_scale_v1_init");
    if (initLock == nullptr) return;
    if (WaitForSingleObject(initLock, 5000) == WAIT_FAILED) { CloseHandle(initLock); return; }
    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, static_cast<DWORD>(sizeof(Shared)), L"Local\\ape_scale_v1");
    if (mapping != nullptr) {
        // New mappings are zero-filled, so magic == 0 identifies a fresh one.
        if (void* view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared))) {
            auto* data = static_cast<Shared*>(view);
            if (data->magic == 0) { new (data) Shared; data->magic = magic; }
            if (data->magic == magic) { shared = data; mappingHandle = mapping; }
            else { UnmapViewOfFile(view); CloseHandle(mapping); }
        } else CloseHandle(mapping);
    }
    ReleaseMutex(initLock); CloseHandle(initLock);
}
ScaleLink::~ScaleLink() {
    release();
    if (shared) UnmapViewOfFile(shared);
    if (mappingHandle) CloseHandle(static_cast<HANDLE>(mappingHandle));
}
#else
namespace {
bool processAlive(uint64_t owner) noexcept { return kill(static_cast<pid_t>(owner >> 32), 0) == 0 || errno != ESRCH; }
}
ScaleLink::ScaleLink() {
    static_assert(sizeof(Shared)==576,"Shared ABI must match on Intel and ARM");
    static std::atomic<uint64_t> serial {1};
    token = (static_cast<uint64_t>(getpid()) << 32) | ((nowNanos() ^ serial.fetch_add(1)) & 0xffffffffULL);
    if (token == 0) token = 1;
    char name[64]; std::snprintf(name, sizeof(name), "/ape_scale_v1_%u", static_cast<unsigned>(getuid()));
    const int fd = shm_open(name, O_CREAT | O_RDWR, 0600);
    if (fd < 0) return;
    // macOS POSIX shm descriptors do not support flock. A private regular
    // lock file serialises initialisation only, outside all audio callbacks.
    char lockPath[96]; std::snprintf(lockPath,sizeof(lockPath),"/tmp/ape-scale-v1-%u.lock",static_cast<unsigned>(getuid()));
    const int lockFd = open(lockPath,O_CREAT | O_RDWR | O_NOFOLLOW,0600);
    struct stat lockInfo {};
    if (lockFd < 0 || fstat(lockFd,&lockInfo)!=0 || lockInfo.st_uid!=getuid() || !S_ISREG(lockInfo.st_mode) || flock(lockFd,LOCK_EX)!=0) {
        if (lockFd >= 0) close(lockFd); close(fd); return;
    }
    struct stat info {};
    const bool sizeOkay = fstat(fd, &info) == 0;
    const bool fresh = sizeOkay && info.st_size == 0;
    // Darwin rounds shm ftruncate sizes up to the VM page size (16 KiB on
    // Apple Silicon). The magic and fixed layout identify the logical size.
    if (!sizeOkay || (fresh && ftruncate(fd, sizeof(Shared)) != 0) || (!fresh && info.st_size < static_cast<off_t>(sizeof(Shared)))) {
        flock(lockFd, LOCK_UN); close(lockFd); close(fd); return;
    }
    void* mapping = mmap(nullptr, sizeof(Shared), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapping != MAP_FAILED) {
        auto* data = static_cast<Shared*>(mapping);
        if (fresh || data->magic == 0) { new (data) Shared; data->magic = magic; }
        if (data->magic == magic) shared = data; else munmap(mapping, sizeof(Shared));
    }
    flock(lockFd, LOCK_UN); close(lockFd); close(fd);
}
ScaleLink::~ScaleLink() { release(); if (shared) munmap(shared, sizeof(Shared)); }
#endif
void ScaleLink::release() noexcept {
    if (shared && ownedGroup >= 0) {
        auto& slot = shared->slots[ownedGroup]; uint64_t expected = token;
        slot.owner.compare_exchange_strong(expected, 0, std::memory_order_acq_rel);
    }
    ownedGroup = -1;
}
bool ScaleLink::publish(int group, int key, uint64_t now) noexcept {
    if (!shared || group < 0 || group >= 8) return false;
    if (ownedGroup != group) release();
    auto& slot = shared->slots[group];
    auto owner = slot.owner.load(std::memory_order_acquire);
    if (owner != token) {
        // A live process keeps its lease even while paused. Never steal from
        // a suspended writer that could resume halfway through publication.
        if (owner != 0 && processAlive(owner)) return false;
        // A dead process cannot resume writing. Reclaim its abandoned lease.
        if (owner != 0 && !slot.owner.compare_exchange_strong(owner, 0, std::memory_order_acq_rel)) return false;
        uint64_t empty = 0;
        if (!slot.owner.compare_exchange_strong(empty, token, std::memory_order_acq_rel)) return false;
        slot.message.store(0, std::memory_order_release);
    }
    ownedGroup = group;
    slot.message.store(key >= 0 && key < 24 ? static_cast<uint64_t>(key + 1) : 0, std::memory_order_release);
    slot.heartbeat.store(now, std::memory_order_release);
    return true;
}
LinkedScale ScaleLink::read(int group, uint64_t now) const noexcept {
    if (!shared || group < 0 || group >= 8) return {};
    const auto& slot = shared->slots[group];
    const auto owner = slot.owner.load(std::memory_order_acquire);
    const auto beat = slot.heartbeat.load(std::memory_order_acquire);
    const auto message = slot.message.load(std::memory_order_acquire);
    if (!owner || slot.owner.load(std::memory_order_acquire) != owner || now < beat || now-beat > expiry) return {};
    return {message >= 1 && message <= 24 ? static_cast<int>(message - 1) : -1, true};
}
}
