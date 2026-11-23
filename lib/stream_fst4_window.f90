! SPDX-License-Identifier: GPL-3.0-or-later
! A window of the GUI's FST4 and FST4W waveform, as gen_fst4wave makes it
! (lib/fst4/gen_fst4wave.f90, real), for the stream's sample-range renders
! (lib/stream_slot_audio.cpp): only the samples asked for, from a block and
! phase the previous window returned.
module stream_fst4_window
  use, intrinsic :: iso_c_binding, only: c_int32_t, c_float, c_double
  use, intrinsic :: iso_fortran_env, only: real64
  use waveform_kernels, only: waveform_workspace, render_gfsk_window, apply_waveform_ramp
  implicit none
  private
  public :: stream_fst4_wave_window

  type(waveform_workspace), save :: work_
  logical, save :: first_call_ = .true., shape_ = .true.

contains

  ! gen_fst4wave's samples out_first to out_first+nout-1, counted from 0, for
  ! itone(nsym) at nsps samples a symbol, fsample, hmod and f0 as it takes
  ! them. first and phi give the block and phase to start from (0 and 0: the
  ! waveform's start) and return those of sample out_first+nout (first -1:
  ! past the message). Returns 0, or 1 when it cannot start there.
  function stream_fst4_wave_window(itone, nsym, nsps, fsample, hmod, f0,     &
       out_first, nout, wave, first, phi) result(status)                     &
       bind(C, name='stream_fst4_wave_window')
    integer(c_int32_t), value :: nsym, nsps, hmod, out_first, nout
    integer(c_int32_t), intent(in) :: itone(nsym)
    real(c_float), value :: fsample, f0
    real(c_float), intent(out) :: wave(nout)
    integer(c_int32_t), intent(inout) :: first
    real(c_double), intent(inout) :: phi
    integer(c_int32_t) :: status
    character :: value
    integer :: length, tail, block
    real(real64) :: carrier, tsym
    logical :: ok

    ! As gen_fst4wave_owned.
    if (first_call_) then
       call get_environment_variable('FST4_NOSHAPING', value, length)
       shape_ = .not. (length .eq. 1 .and. value .eq. '1')
       first_call_ = .false.
    end if
    tsym=real(nsps,real64)/real(fsample,real64)
    carrier=(8.0_real64*atan(1.0_real64))*(real(f0,real64)-1.5_real64*hmod/tsym)/real(fsample,real64)
    tail=(nsym-1)*nsps+3*nsps/4
    block = first
    call render_gfsk_window(work_, itone, nsps, 2.0, hmod, carrier, nsps,       &
         nsym*nsps, out_first, wave, block, phi, ok)
    status = 1
    if (.not. ok) return
    first = block
    if (shape_) then
       call ramp_window_(wave, out_first, 0, nsps/4, real(nsps/2), -1.0)
       call ramp_window_(wave, out_first, tail, nsps/4+1, real(nsps/2), 1.0)
    end if
    status = 0
  end function stream_fst4_wave_window

  ! gen_fst4wave_owned's apply_waveform_ramp(wave, 1, start, length, ...) on
  ! the window wave, whose first sample is first. Each factor is the one
  ! ramp_real makes, in a loop of the same length (where gfortran calls a
  ! vector cos, which samples it covers depends on the loop), from an array
  ! of ones: halving is exact, so a sample times it is what ramp_real gives.
  subroutine ramp_window_(wave, first, start, length, denominator, direction)
    real(c_float), intent(inout) :: wave(:)
    integer, intent(in) :: first, start, length
    real, intent(in) :: denominator, direction
    real, allocatable :: factor(:)
    integer :: a, b

    a = max(first, start)
    b = min(first + size(wave), start + length) - 1
    if (a .gt. b) return
    allocate(factor(length))
    factor = 1.0
    call apply_waveform_ramp(factor, 1, 0, length, denominator, direction)
    wave(a - first + 1:b - first + 1) = wave(a - first + 1:b - first + 1) *     &
         factor(a - start + 1:b - start + 1)
  end subroutine ramp_window_

end module stream_fst4_window
