#include "tests/FontLockObserver.hpp"
#include <atomic>

namespace {
std::atomic<unsigned> observation{0};
thread_local const std::uint32_t* watchedWord = nullptr;
}

void FontTestResetLockObservation_nid_no_patch() {
    observation.store(0, std::memory_order_release);
}

void FontTestWatchOwnerLock_nid_no_patch(const std::uint32_t* word) {
    watchedWord = word;
}

void FontTestObserveOwnerContention_nid_no_patch(const std::uint32_t* word) {
    if (word != watchedWord) return;
    observation.fetch_or(FONT_TEST_CONTENDED, std::memory_order_release);
    observation.notify_all();
}

void FontTestCompleteLockObservation_nid_no_patch() {
    watchedWord = nullptr;
    observation.fetch_or(FONT_TEST_COMPLETED, std::memory_order_release);
    observation.notify_all();
}

unsigned FontTestWaitLockObservation_nid_no_patch() {
    unsigned observed = observation.load(std::memory_order_acquire);
    while (observed == 0) {
        observation.wait(0, std::memory_order_acquire);
        observed = observation.load(std::memory_order_acquire);
    }
    return observed;
}
