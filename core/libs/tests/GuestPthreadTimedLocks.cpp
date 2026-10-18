#include "SceTypes.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <thread>

extern "C" {
int APS5_VABI scePthreadCreate(Pthread* thread, const PthreadAttr* attr, PthreadEntry entry, void* arg, const char* name);
int APS5_VABI scePthreadJoin(Pthread thread, void** retval);
int APS5_VABI pthread_rwlock_wrlock_nid_postfix(PthreadRwlock* rwlock);
int APS5_VABI pthread_rwlock_unlock_nid_postfix(PthreadRwlock* rwlock);
int APS5_VABI pthread_rwlock_destroy_nid_postfix(PthreadRwlock* rwlock);
int APS5_VABI pthread_rwlock_timedrdlock_nid_postfix(PthreadRwlock* rwlock, const KernelTimespec* abstime);
int APS5_VABI pthread_rwlock_timedwrlock_nid_postfix(PthreadRwlock* rwlock, const KernelTimespec* abstime);
int APS5_VABI pthread_mutex_init_nid_postfix(PthreadMutex* mutex, const PthreadMutexattr* attr);
int APS5_VABI pthread_mutex_lock_nid_postfix(PthreadMutex* mutex);
int APS5_VABI pthread_mutex_unlock_nid_postfix(PthreadMutex* mutex);
int APS5_VABI pthread_mutex_destroy_nid_postfix(PthreadMutex* mutex);
int APS5_VABI pthread_mutex_timedlock_nid_postfix(PthreadMutex* mutex, const KernelTimespec* abstime);
int APS5_VABI pthread_mutex_reltimedlock_np_nid_postfix(PthreadMutex* mutex, std::uint64_t usec);
int APS5_VABI pthread_rwlock_reltimedrdlock_np_nid_postfix(PthreadRwlock* rwlock, std::uint32_t usec);
int APS5_VABI pthread_rwlock_reltimedwrlock_np_nid_postfix(PthreadRwlock* rwlock, std::uint32_t usec);
}

static constexpr int SCE_OK = 0;
static constexpr int GUEST_EDEADLK = 11;
static constexpr auto HoldTime = std::chrono::milliseconds(50);

static void Require(bool value) { if (!value) std::abort(); }

struct Holder {
    void* lock;
    bool mutex;
    std::atomic<bool> held{false};
    std::atomic<bool> releasing{false};
    Pthread thread = nullptr;
};

static void* APS5_VABI Hold(void* arg) {
    auto& holder = *static_cast<Holder*>(arg);
    auto* mutex = static_cast<PthreadMutex*>(holder.lock);
    auto* rwlock = static_cast<PthreadRwlock*>(holder.lock);
    Require((holder.mutex ? pthread_mutex_lock_nid_postfix(mutex) : pthread_rwlock_wrlock_nid_postfix(rwlock)) == 0);
    holder.held.store(true);
    std::this_thread::sleep_for(HoldTime);
    holder.releasing.store(true);
    Require((holder.mutex ? pthread_mutex_unlock_nid_postfix(mutex) : pthread_rwlock_unlock_nid_postfix(rwlock)) == 0);
    return nullptr;
}

template <typename Lock>
static void RequireWaitsForHolder(void* lock, bool mutex, Lock acquire) {
    Holder holder{lock, mutex};
    Require(scePthreadCreate(&holder.thread, nullptr, Hold, &holder, nullptr) == SCE_OK);
    while (!holder.held.load()) std::this_thread::yield();
    Require(acquire() == 0);
    Require(holder.releasing.load());
    Require(scePthreadJoin(holder.thread, nullptr) == SCE_OK);
}

static void CheckRwlock() {
    PthreadRwlock rwlock = nullptr;
    Require(pthread_rwlock_timedrdlock_nid_postfix(&rwlock, nullptr) == 0 && rwlock != nullptr);
    Require(pthread_rwlock_unlock_nid_postfix(&rwlock) == 0);
    Require(pthread_rwlock_timedwrlock_nid_postfix(&rwlock, nullptr) == 0);
    Require(pthread_rwlock_unlock_nid_postfix(&rwlock) == 0);
    RequireWaitsForHolder(&rwlock, false, [&] { return pthread_rwlock_timedrdlock_nid_postfix(&rwlock, nullptr); });
    Require(pthread_rwlock_unlock_nid_postfix(&rwlock) == 0);
    RequireWaitsForHolder(&rwlock, false, [&] { return pthread_rwlock_timedwrlock_nid_postfix(&rwlock, nullptr); });
    Require(pthread_rwlock_unlock_nid_postfix(&rwlock) == 0);
    Require(pthread_rwlock_destroy_nid_postfix(&rwlock) == 0);
}

static void CheckMutex() {
    PthreadMutex mutex = nullptr;
    Require(pthread_mutex_init_nid_postfix(&mutex, nullptr) == 0);
    Require(pthread_mutex_timedlock_nid_postfix(&mutex, nullptr) == 0);
    Require(pthread_mutex_timedlock_nid_postfix(&mutex, nullptr) == GUEST_EDEADLK);
    Require(pthread_mutex_unlock_nid_postfix(&mutex) == 0);
    RequireWaitsForHolder(&mutex, true, [&] { return pthread_mutex_timedlock_nid_postfix(&mutex, nullptr); });
    Require(pthread_mutex_unlock_nid_postfix(&mutex) == 0);
    Require(pthread_mutex_destroy_nid_postfix(&mutex) == 0);
}

static void CheckRelativeTimedLocks() {
    constexpr int GuestEtimedout = 60;
    PthreadMutex mutex = nullptr;
    Require(pthread_mutex_reltimedlock_np_nid_postfix(&mutex, 1000) == 0 && mutex != nullptr);
    Require(pthread_mutex_unlock_nid_postfix(&mutex) == 0);
    Holder mutexHolder{&mutex, true};
    Require(scePthreadCreate(&mutexHolder.thread, nullptr, Hold, &mutexHolder, "rel-mutex") == SCE_OK);
    while (!mutexHolder.held.load()) std::this_thread::yield();
    const int mutexShort = pthread_mutex_reltimedlock_np_nid_postfix(&mutex, 1000);
    Require(mutexShort == GuestEtimedout || (mutexShort == 0 && mutexHolder.releasing.load()));
    if (mutexShort == 0) Require(pthread_mutex_unlock_nid_postfix(&mutex) == 0);
    Require(pthread_mutex_reltimedlock_np_nid_postfix(&mutex, 5000000) == 0);
    Require(pthread_mutex_unlock_nid_postfix(&mutex) == 0);
    Require(scePthreadJoin(mutexHolder.thread, nullptr) == SCE_OK);
    Require(pthread_mutex_destroy_nid_postfix(&mutex) == 0);

    PthreadRwlock rwlock = nullptr;
    Require(pthread_rwlock_reltimedrdlock_np_nid_postfix(&rwlock, 1000) == 0 && rwlock != nullptr);
    Require(pthread_rwlock_unlock_nid_postfix(&rwlock) == 0);
    Require(pthread_rwlock_reltimedwrlock_np_nid_postfix(&rwlock, 1000) == 0);
    Require(pthread_rwlock_unlock_nid_postfix(&rwlock) == 0);
    Holder rwlockHolder{&rwlock, false};
    Require(scePthreadCreate(&rwlockHolder.thread, nullptr, Hold, &rwlockHolder, "rel-rwlock") == SCE_OK);
    while (!rwlockHolder.held.load()) std::this_thread::yield();
    const int readShort = pthread_rwlock_reltimedrdlock_np_nid_postfix(&rwlock, 1000);
    Require(readShort == GuestEtimedout || (readShort == 0 && rwlockHolder.releasing.load()));
    if (readShort == 0) Require(pthread_rwlock_unlock_nid_postfix(&rwlock) == 0);
    const int writeShort = pthread_rwlock_reltimedwrlock_np_nid_postfix(&rwlock, 1000);
    Require(writeShort == GuestEtimedout || (writeShort == 0 && rwlockHolder.releasing.load()));
    if (writeShort == 0) Require(pthread_rwlock_unlock_nid_postfix(&rwlock) == 0);
    Require(pthread_rwlock_reltimedwrlock_np_nid_postfix(&rwlock, 5000000) == 0);
    Require(pthread_rwlock_unlock_nid_postfix(&rwlock) == 0);
    Require(scePthreadJoin(rwlockHolder.thread, nullptr) == SCE_OK);
    Require(pthread_rwlock_destroy_nid_postfix(&rwlock) == 0);
}

int main() {
    CheckRelativeTimedLocks();
    CheckRwlock();
    CheckMutex();
}
