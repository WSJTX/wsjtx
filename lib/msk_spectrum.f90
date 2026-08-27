module msk_spectrum
  use, intrinsic :: iso_fortran_env, only: real64
  use, intrinsic :: ieee_arithmetic, only: ieee_is_finite
  implicit none
  private

  public :: msk_clip_spectrum_window
  public :: msk_peak_offset

contains

  pure subroutine msk_clip_spectrum_window(nfft,requested_lo,requested_hi, &
       lo,hi,usable)
    integer, intent(in) :: nfft
    real(real64), intent(in) :: requested_lo,requested_hi
    integer, intent(out) :: lo,hi
    logical, intent(out) :: usable

    lo=1
    hi=0
    usable=.false.
    if(nfft.lt.1) return
    if(.not.ieee_is_finite(requested_lo) .or. &
         .not.ieee_is_finite(requested_hi)) return
    if(requested_lo.gt.requested_hi) return
    if(requested_hi.lt.0.5_real64 .or. &
         requested_lo.ge.real(nfft,real64)+0.5_real64) return

    lo=nint(max(1.0_real64,min(real(nfft,real64),requested_lo)))
    hi=nint(max(1.0_real64,min(real(nfft,real64),requested_hi)))
    usable=lo.le.hi
    if(.not.usable) then
      lo=1
      hi=0
    endif
  end subroutine msk_clip_spectrum_window

  pure real function msk_peak_offset(spectrum,peak_bin) result(offset)
    complex, intent(in) :: spectrum(:)
    integer, intent(in) :: peak_bin
    complex :: curvature
    real :: candidate,scale

    offset=0.0
    if(peak_bin.le.lbound(spectrum,1) .or. &
         peak_bin.ge.ubound(spectrum,1)) return

    curvature=2.0*spectrum(peak_bin)-spectrum(peak_bin-1)- &
         spectrum(peak_bin+1)
    scale=max(abs(spectrum(peak_bin-1)),abs(spectrum(peak_bin)), &
         abs(spectrum(peak_bin+1)))
    if(scale.le.0.0) return
    if(abs(curvature).le.epsilon(scale)*scale) return

    candidate=-real((spectrum(peak_bin-1)-spectrum(peak_bin+1))/curvature)
    if(ieee_is_finite(candidate)) offset=candidate
  end function msk_peak_offset

end module msk_spectrum
