! SPDX-License-Identifier: GPL-3.0-or-later
! Runs a C callback inside the critical section that serializes FFTW planning
! (four2a.f90), for C code built without OpenMP, such as the WSPR decoder
! (lib/wsprd/wspr_decoder.c), which plans on the codec's decode thread while the
! main thread plans spectrum rows. The plan guard wsprd_context_new takes
! (lib/wsprd/wsprd_decode.h).
integer(c_int) function wsjt_fftw_locked(fn, arg) bind(C, name='wsjt_fftw_locked')
  use, intrinsic :: iso_c_binding, only: c_int, c_funptr, c_ptr, c_f_procpointer
  use fftw_tsan, only: wsjt_tsan_acquire_fftw, wsjt_tsan_release_fftw
  implicit none
  type(c_funptr), value :: fn
  type(c_ptr), value :: arg
  abstract interface
     subroutine callback(data) bind(C)
       import :: c_ptr
       type(c_ptr), value :: data
     end subroutine callback
  end interface
  procedure(callback), pointer :: f

  call c_f_procpointer(fn, f)
  !$omp critical(fftw)
  call wsjt_tsan_acquire_fftw()
  call f(arg)
  call wsjt_tsan_release_fftw()
  !$omp end critical(fftw)
  wsjt_fftw_locked = 0
end function wsjt_fftw_locked
