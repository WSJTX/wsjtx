! SPDX-License-Identifier: GPL-3.0-or-later
! FFTW's wisdom saved whole: exported under critical(fftw), as every FFTW
! planner call is made, to a file beside the target that then replaces it
! (lib/wisdom.c), so a process stopped during the write leaves the old file.
module fftw_wisdom
  use, intrinsic :: iso_c_binding, only: c_char, c_int
  implicit none
  private
  public :: save_wisdom

  interface
     function wsjt_save_wisdom(filename) result(saved) bind(C, name='wsjt_save_wisdom')
       import :: c_char, c_int
       character(kind=c_char), intent(in) :: filename(*)
       integer(c_int) :: saved
     end function wsjt_save_wisdom
  end interface

contains

  ! filename ends with a NUL, as FFTW's own calls take it; false when the
  ! file could not be replaced, which then stays as it was.
  logical function save_wisdom(filename)
    use fftw_tsan, only: wsjt_tsan_acquire_fftw, wsjt_tsan_release_fftw
    character(len=*), intent(in) :: filename
    integer(c_int) :: saved
    !$omp critical(fftw)
    call wsjt_tsan_acquire_fftw()
    saved = wsjt_save_wisdom(filename)
    call wsjt_tsan_release_fftw()
    !$omp end critical(fftw)
    save_wisdom = saved .ne. 0
  end function save_wisdom

end module fftw_wisdom
