/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ThreadSanitizer marks for WSPR's threads (lib/wsprd/wspr_parallel.f90,
   lib/wsprd/osdwspr.f90). GCC's TSan does not see libgomp's synchronization
   (as lib/ft8var/ft8_tsan.c notes for FT8), so these mirror its edges: the
   caller's work before a region happens before every index (fork), every
   index before the caller's work after it (join), and osdwspr's generator
   before every thread that takes its lock after it. Without TSan they do
   nothing. */
#if defined(__SANITIZE_THREAD__)
#define WSPR_TSAN 1
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define WSPR_TSAN 1
#endif
#endif

#if defined(WSPR_TSAN)
#include <sanitizer/tsan_interface.h>
#define WSPR_TSAN_SYNC(name)                                    \
    static unsigned char name##_token;                          \
    void wspr_tsan_acquire_##name(void) {__tsan_acquire(&name##_token);} \
    void wspr_tsan_release_##name(void) {__tsan_release(&name##_token);}
#else
#define WSPR_TSAN_SYNC(name)                                    \
    void wspr_tsan_acquire_##name(void) {}                      \
    void wspr_tsan_release_##name(void) {}
#endif

WSPR_TSAN_SYNC(fork)
WSPR_TSAN_SYNC(join)
WSPR_TSAN_SYNC(generator)
