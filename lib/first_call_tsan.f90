! SPDX-License-Identifier: GPL-3.0-or-later
! TSan tokens for the critical(first_call) sections that build the encoders'
! tables (lib/ft8var/ft8_tsan.c): GCC's TSan does not observe libgomp's locks.
module first_call_tsan
  implicit none
  interface
     subroutine wsjt_tsan_acquire_first_call() bind(C)
     end subroutine wsjt_tsan_acquire_first_call
     subroutine wsjt_tsan_release_first_call() bind(C)
     end subroutine wsjt_tsan_release_first_call
  end interface
end module first_call_tsan
