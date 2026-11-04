! SPDX-License-Identifier: GPL-3.0-or-later
! TSan tokens for the critical sections that serialize FFTW planning
! (lib/ft8var/ft8_tsan.c): GCC's TSan does not observe libgomp's locks.
module fftw_tsan
  implicit none
  interface
     subroutine wsjt_tsan_acquire_fftw() bind(C)
     end subroutine wsjt_tsan_acquire_fftw
     subroutine wsjt_tsan_release_fftw() bind(C)
     end subroutine wsjt_tsan_release_fftw
     subroutine wsjt_tsan_acquire_four2a_setup() bind(C)
     end subroutine wsjt_tsan_acquire_four2a_setup
     subroutine wsjt_tsan_release_four2a_setup() bind(C)
     end subroutine wsjt_tsan_release_four2a_setup
  end interface
end module fftw_tsan
