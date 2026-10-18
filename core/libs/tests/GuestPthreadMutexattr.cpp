#include "SceTypes.hpp"
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <stdexcept>

extern "C" {
int APS5_VABI pthread_mutexattr_init_nid_postfix(PthreadMutexattr* attr);
int APS5_VABI pthread_mutexattr_destroy_nid_postfix(PthreadMutexattr* attr);
int APS5_VABI pthread_mutexattr_getpshared_nid_postfix(const PthreadMutexattr* attr, int* pshared);
int APS5_VABI pthread_mutexattr_setpshared_nid_postfix(PthreadMutexattr* attr, int pshared);
int APS5_VABI pthread_mutexattr_setprotocol_nid_postfix(PthreadMutexattr* attr, int protocol);
int APS5_VABI pthread_mutexattr_setprioceiling_nid_postfix(PthreadMutexattr* attr, int prioceiling);
int APS5_VABI pthread_mutexattr_getprioceiling_nid_postfix(const PthreadMutexattr* attr, int* prioceiling);
int APS5_VABI pthread_mutexattr_settype_nid_postfix(PthreadMutexattr* attr, int type);
int APS5_VABI pthread_mutexattr_gettype_nid_postfix(const PthreadMutexattr* attr, int* type);
int APS5_VABI pthread_mutex_init_nid_postfix(PthreadMutex* mutex, const PthreadMutexattr* attr);
int APS5_VABI pthread_mutex_lock_nid_postfix(PthreadMutex* mutex);
int APS5_VABI pthread_mutex_unlock_nid_postfix(PthreadMutex* mutex);
int APS5_VABI pthread_mutex_destroy_nid_postfix(PthreadMutex* mutex);
int APS5_VABI pthread_mutex_isowned_np_nid_postfix(PthreadMutex* mutex);
int APS5_VABI pthread_mutex_getspinloops_np_nid_postfix(PthreadMutex* mutex, int* count);
int APS5_VABI pthread_mutex_setspinloops_np_nid_postfix(PthreadMutex* mutex, int count);
int APS5_VABI pthread_mutex_getyieldloops_np_nid_postfix(PthreadMutex* mutex, int* count);
int APS5_VABI pthread_mutex_setyieldloops_np_nid_postfix(PthreadMutex* mutex, int count);
int APS5_VABI pthread_mutex_timedlock_nid_postfix(PthreadMutex* mutex, const KernelTimespec* abstime);
int APS5_VABI scePthreadCreate(Pthread* thread, const PthreadAttr* attr, PthreadEntry entry, void* arg, const char* name);
int APS5_VABI scePthreadJoin(Pthread thread, void** retval);
int APS5_VABI sched_get_priority_max_nid_postfix(int policy);
}

static constexpr int GUEST_EINVAL = 22;
static constexpr int PRIO_NONE = 0;
static constexpr int PRIO_INHERIT = 1;
static constexpr int PRIO_PROTECT = 2;
static constexpr int SCHED_RR = 3;

static void Require(bool value) { if (!value) std::abort(); }

template <typename Call>
static bool Throws(Call call) {
    try {
        call();
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

static constexpr KernelTimespec INVALID_DEADLINE{0, 1000000000};

static void* APS5_VABI TimedlockInvalidDeadline(void* arg) {
    auto* mutex = static_cast<PthreadMutex*>(arg);
    return reinterpret_cast<void*>(static_cast<std::intptr_t>(pthread_mutex_timedlock_nid_postfix(mutex, &INVALID_DEADLINE)));
}

static void CheckTimedlockInvalidDeadline() {
    PthreadMutex mutex = nullptr;
    Require(pthread_mutex_init_nid_postfix(&mutex, nullptr) == 0);
    Require(pthread_mutex_timedlock_nid_postfix(&mutex, &INVALID_DEADLINE) == 0);
    Pthread thread{};
    void* result = nullptr;
    Require(scePthreadCreate(&thread, nullptr, TimedlockInvalidDeadline, &mutex, nullptr) == 0);
    Require(scePthreadJoin(thread, &result) == 0 && reinterpret_cast<std::intptr_t>(result) == GUEST_EINVAL);
    Require(pthread_mutex_unlock_nid_postfix(&mutex) == 0);
    Require(pthread_mutex_destroy_nid_postfix(&mutex) == 0);
}

static void CheckType() {
    PthreadMutexattr attr = nullptr;
    Require(pthread_mutexattr_init_nid_postfix(&attr) == 0);
    int type = -1;
    Require(pthread_mutexattr_gettype_nid_postfix(&attr, &type) == 0 && type == 1);
    for (const int set : {2, 3, 4, 1}) {
        Require(pthread_mutexattr_settype_nid_postfix(&attr, set) == 0);
        Require(pthread_mutexattr_gettype_nid_postfix(&attr, &type) == 0 && type == set);
    }
    Require(pthread_mutexattr_settype_nid_postfix(&attr, 5) == GUEST_EINVAL);
    Require(pthread_mutexattr_gettype_nid_postfix(&attr, &type) == 0 && type == 1);
    Require(Throws([&] { pthread_mutexattr_gettype_nid_postfix(&attr, nullptr); }));
    Require(pthread_mutexattr_destroy_nid_postfix(&attr) == 0);
    type = -1;
    Require(pthread_mutexattr_gettype_nid_postfix(&attr, &type) == GUEST_EINVAL && type == -1);
    Require(pthread_mutexattr_gettype_nid_postfix(nullptr, &type) == GUEST_EINVAL && type == -1);
}

static void CheckProcessSharing() {
    PthreadMutexattr attr = nullptr;
    int sharing = -1;
    Require(pthread_mutexattr_getpshared_nid_postfix(nullptr, &sharing) == GUEST_EINVAL && sharing == -1);
    Require(pthread_mutexattr_getpshared_nid_postfix(&attr, &sharing) == GUEST_EINVAL && sharing == -1);
    Require(pthread_mutexattr_setpshared_nid_postfix(nullptr, 0) == GUEST_EINVAL);
    Require(pthread_mutexattr_setpshared_nid_postfix(&attr, 0) == GUEST_EINVAL);
    Require(pthread_mutexattr_init_nid_postfix(&attr) == 0);
    Require(pthread_mutexattr_getpshared_nid_postfix(&attr, &sharing) == 0 && sharing == 0);
    Require(pthread_mutexattr_settype_nid_postfix(&attr, 2) == 0);
    Require(pthread_mutexattr_setpshared_nid_postfix(&attr, 0) == 0);
    for (const int invalid : {1, -1, 2, 2147483647})
        Require(pthread_mutexattr_setpshared_nid_postfix(&attr, invalid) == GUEST_EINVAL);
    sharing = -1;
    Require(pthread_mutexattr_getpshared_nid_postfix(&attr, &sharing) == 0 && sharing == 0);
    int type = -1;
    Require(pthread_mutexattr_gettype_nid_postfix(&attr, &type) == 0 && type == 2);
    Require(Throws([&] { pthread_mutexattr_getpshared_nid_postfix(&attr, nullptr); }));
    PthreadMutex mutex = nullptr;
    Require(pthread_mutex_init_nid_postfix(&mutex, &attr) == 0);
    Require(pthread_mutex_lock_nid_postfix(&mutex) == 0);
    Require(pthread_mutex_lock_nid_postfix(&mutex) == 0);
    Require(pthread_mutex_unlock_nid_postfix(&mutex) == 0);
    Require(pthread_mutex_unlock_nid_postfix(&mutex) == 0);
    Require(pthread_mutex_destroy_nid_postfix(&mutex) == 0);
    Require(pthread_mutexattr_setpshared_nid_postfix(&attr, 0) == 0);
    Require(pthread_mutexattr_getpshared_nid_postfix(&attr, &sharing) == 0 && sharing == 0);
    Require(pthread_mutexattr_destroy_nid_postfix(&attr) == 0);
    sharing = -1;
    Require(pthread_mutexattr_getpshared_nid_postfix(&attr, &sharing) == GUEST_EINVAL && sharing == -1);
    Require(pthread_mutexattr_setpshared_nid_postfix(&attr, 0) == GUEST_EINVAL);
}

static void* APS5_VABI ReportOwnership(void* arg) {
    return reinterpret_cast<void*>(static_cast<std::intptr_t>(pthread_mutex_isowned_np_nid_postfix(static_cast<PthreadMutex*>(arg))));
}

static void CheckOwnershipAndLoops() {
    PthreadMutex mutex = nullptr;
    int count = -1;
    Require(pthread_mutex_isowned_np_nid_postfix(&mutex) == 0 && mutex == nullptr);
    Require(pthread_mutex_getspinloops_np_nid_postfix(&mutex, &count) == 0 && count == 0 && mutex != nullptr);
    Require(pthread_mutex_isowned_np_nid_postfix(&mutex) == 0);
    Require(pthread_mutex_lock_nid_postfix(&mutex) == 0);
    Require(pthread_mutex_isowned_np_nid_postfix(&mutex) == 1);
    Pthread thread{};
    void* result = reinterpret_cast<void*>(std::intptr_t{7});
    Require(scePthreadCreate(&thread, nullptr, ReportOwnership, &mutex, nullptr) == 0);
    Require(scePthreadJoin(thread, &result) == 0 && result == nullptr);
    Require(pthread_mutex_unlock_nid_postfix(&mutex) == 0);
    Require(pthread_mutex_isowned_np_nid_postfix(&mutex) == 0);
    Require(pthread_mutex_setspinloops_np_nid_postfix(&mutex, 37) == 0);
    Require(pthread_mutex_getspinloops_np_nid_postfix(&mutex, &count) == 0 && count == 37);
    Require(pthread_mutex_setspinloops_np_nid_postfix(&mutex, -1) == 0);
    Require(pthread_mutex_getspinloops_np_nid_postfix(&mutex, &count) == 0 && count == -1);
    Require(pthread_mutex_getyieldloops_np_nid_postfix(&mutex, &count) == 0 && count == 0);
    Require(pthread_mutex_setyieldloops_np_nid_postfix(&mutex, 5) == 0);
    Require(pthread_mutex_getyieldloops_np_nid_postfix(&mutex, &count) == 0 && count == 5);
    Require(Throws([&] { pthread_mutex_getspinloops_np_nid_postfix(&mutex, nullptr); }));
    Require(Throws([&] { pthread_mutex_getyieldloops_np_nid_postfix(&mutex, nullptr); }));
    Require(pthread_mutex_destroy_nid_postfix(&mutex) == 0);
    Require(pthread_mutex_isowned_np_nid_postfix(&mutex) == 0);
    Require(pthread_mutex_getspinloops_np_nid_postfix(&mutex, &count) == GUEST_EINVAL);
    Require(pthread_mutex_setspinloops_np_nid_postfix(&mutex, 1) == GUEST_EINVAL);
    Require(pthread_mutex_getyieldloops_np_nid_postfix(&mutex, &count) == GUEST_EINVAL);
    Require(pthread_mutex_setyieldloops_np_nid_postfix(&mutex, 1) == GUEST_EINVAL);

    PthreadMutexattr attr = nullptr;
    Require(pthread_mutexattr_init_nid_postfix(&attr) == 0);
    Require(pthread_mutexattr_settype_nid_postfix(&attr, 4) == 0);
    PthreadMutex adaptive = nullptr;
    Require(pthread_mutex_init_nid_postfix(&adaptive, &attr) == 0);
    Require(pthread_mutex_getspinloops_np_nid_postfix(&adaptive, &count) == 0 && count == 2000);
    Require(pthread_mutex_getyieldloops_np_nid_postfix(&adaptive, &count) == 0 && count == 0);
    Require(pthread_mutex_destroy_nid_postfix(&adaptive) == 0);
    Require(pthread_mutexattr_destroy_nid_postfix(&attr) == 0);
    PthreadMutex adaptiveStatic = reinterpret_cast<PthreadMutex>(std::uintptr_t{1});
    Require(pthread_mutex_isowned_np_nid_postfix(&adaptiveStatic) == 0);
    Require(pthread_mutex_getspinloops_np_nid_postfix(&adaptiveStatic, &count) == 0 && count == 2000);
    Require(pthread_mutex_destroy_nid_postfix(&adaptiveStatic) == 0);
}

static void CheckInvalidPriorityAttribute(PthreadMutexattr* attr) {
    int ceiling = -1;
    Require(pthread_mutexattr_setprotocol_nid_postfix(attr, PRIO_PROTECT) == GUEST_EINVAL);
    Require(pthread_mutexattr_setprotocol_nid_postfix(attr, -1) == GUEST_EINVAL);
    Require(pthread_mutexattr_setprioceiling_nid_postfix(attr, 300) == GUEST_EINVAL);
    Require(pthread_mutexattr_getprioceiling_nid_postfix(attr, &ceiling) == GUEST_EINVAL && ceiling == -1);
    Require(pthread_mutexattr_getprioceiling_nid_postfix(attr, nullptr) == GUEST_EINVAL);
}

int main() {
    CheckType();
    CheckProcessSharing();
    CheckTimedlockInvalidDeadline();
    CheckOwnershipAndLoops();
    PthreadMutexattr attr = nullptr;
    CheckInvalidPriorityAttribute(nullptr);
    CheckInvalidPriorityAttribute(&attr);
    Require(pthread_mutexattr_init_nid_postfix(&attr) == 0);
    int ceiling = -1;
    Require(pthread_mutexattr_getprioceiling_nid_postfix(&attr, &ceiling) == GUEST_EINVAL && ceiling == -1);
    Require(pthread_mutexattr_setprioceiling_nid_postfix(&attr, 300) == GUEST_EINVAL);
    Require(pthread_mutexattr_setprotocol_nid_postfix(&attr, PRIO_INHERIT) == 0);
    Require(pthread_mutexattr_setprioceiling_nid_postfix(&attr, 300) == GUEST_EINVAL);
    Require(pthread_mutexattr_setprotocol_nid_postfix(&attr, PRIO_PROTECT) == 0);
    Require(pthread_mutexattr_getprioceiling_nid_postfix(&attr, &ceiling) == 0 && ceiling == sched_get_priority_max_nid_postfix(SCHED_RR));
    Require(pthread_mutexattr_setprioceiling_nid_postfix(&attr, 300) == 0);
    Require(pthread_mutexattr_getprioceiling_nid_postfix(&attr, &ceiling) == 0 && ceiling == 300);
    PthreadMutex mutex = nullptr;
    Require(pthread_mutex_init_nid_postfix(&mutex, &attr) == 0);
    Require(pthread_mutex_lock_nid_postfix(&mutex) == 0);
    Require(pthread_mutex_unlock_nid_postfix(&mutex) == 0);
    Require(pthread_mutex_destroy_nid_postfix(&mutex) == 0);
    Require(pthread_mutexattr_setprotocol_nid_postfix(&attr, PRIO_PROTECT) == 0);
    Require(pthread_mutexattr_getprioceiling_nid_postfix(&attr, &ceiling) == 0 && ceiling == sched_get_priority_max_nid_postfix(SCHED_RR));
    Require(pthread_mutexattr_setprotocol_nid_postfix(&attr, PRIO_NONE) == 0);
    Require(pthread_mutexattr_getprioceiling_nid_postfix(&attr, &ceiling) == GUEST_EINVAL);
    Require(Throws([&] { pthread_mutexattr_getprioceiling_nid_postfix(&attr, nullptr); }));
    Require(pthread_mutexattr_destroy_nid_postfix(&attr) == 0);
    CheckInvalidPriorityAttribute(&attr);
    return 0;
}
