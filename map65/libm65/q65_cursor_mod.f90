module q65_cursor_mod
   use iso_fortran_env, only: real64
   implicit none
contains

   ! Spectrum frequency (kHz, the units of cand()%f) that a cursor target
   ! on the true frequency mousefqso + mousedf maps to.  The cursor is a
   ! true frequency; with a frequency-calibration offset Fcal the signal
   ! sits at its true frequency + Fcal in the spectrum.
   pure function q65_cursor_f0(mousefqso, mousedf, nkhz_center, nrate, nfcal) result(f0)
      integer, intent(in) :: mousefqso, mousedf, nkhz_center, nrate, nfcal
      real(real64) :: f0
      real :: freq
      ! Same single-precision arithmetic as the expression this replaces,
      ! so f0 is unchanged when nfcal = 0.
      freq = mousefqso + 0.001*(mousedf + nfcal)
      f0 = freq - (nkhz_center - real(nrate)/2000.0 - 1.27046)
   end function q65_cursor_f0

end module q65_cursor_mod
