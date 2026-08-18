#if defined(__SANITIZE_THREAD__)
#include <sanitizer/tsan_interface.h>
#endif

// GCC TSan does not observe libgomp synchronization. Stable tokens mirror the
// OpenMP edges without changing production synchronization.
#if defined(__SANITIZE_THREAD__)
#define WSJT_TSAN_SYNC(name)                            \
  static unsigned char name##_token;                    \
  void wsjt_tsan_acquire_##name(void)                    \
  {                                                      \
    __tsan_acquire(&name##_token);                       \
  }                                                      \
  void wsjt_tsan_release_##name(void)                    \
  {                                                      \
    __tsan_release(&name##_token);                       \
  }
#else
#define WSJT_TSAN_SYNC(name)                            \
  void wsjt_tsan_acquire_##name(void) {}                 \
  void wsjt_tsan_release_##name(void) {}
#endif

WSJT_TSAN_SYNC(ft8_mtd)
WSJT_TSAN_SYNC(four2avar_setup)
WSJT_TSAN_SYNC(ft8_find_dupes)
WSJT_TSAN_SYNC(ft8_update_structures)
WSJT_TSAN_SYNC(decoder_section_primary)
WSJT_TSAN_SYNC(decoder_section_secondary)
