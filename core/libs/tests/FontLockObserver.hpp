#ifndef CORE_LIBS_TESTS_FONTLOCKOBSERVER_HPP
#define CORE_LIBS_TESTS_FONTLOCKOBSERVER_HPP

#include <cstdint>

inline constexpr unsigned FONT_TEST_CONTENDED = 1;
inline constexpr unsigned FONT_TEST_COMPLETED = 2;

extern "C" {
void FontTestResetLockObservation_nid_no_patch();
void FontTestWatchOwnerLock_nid_no_patch(const std::uint32_t* word);
void FontTestObserveOwnerContention_nid_no_patch(const std::uint32_t* word);
void FontTestCompleteLockObservation_nid_no_patch();
unsigned FontTestWaitLockObservation_nid_no_patch();
}

#endif
