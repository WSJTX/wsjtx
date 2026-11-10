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

   ! The part of the span mfa..mfb (kHz) that the QSO frequency's candidate and
   ! cursor searches look at: the cursor +/- ntol Hz, plus margin_khz each side.
   ! narrowed is false (and mfa_s..mfb_s the whole span) if that doesn't fit.
   pure subroutine qso_sync_window(rate, nkhz_ctr, mousefqso, mousedf, nfcal, ntol, &
        margin_khz, mfa, mfb, mfa_s, mfb_s, narrowed)
      integer, intent(in)  :: rate, nkhz_ctr, mousefqso, mousedf, nfcal, ntol, margin_khz, mfa, mfb
      integer, intent(out) :: mfa_s, mfb_s
      logical, intent(out) :: narrowed
      real :: f0lo, f0hi

      f0lo = real(q65_cursor_f0(mousefqso, mousedf - ntol, nkhz_ctr, rate, nfcal))
      f0hi = real(q65_cursor_f0(mousefqso, mousedf + ntol, nkhz_ctr, rate, nfcal))
      mfa_s = max(mfa, floor(f0lo) - margin_khz)
      mfb_s = min(mfb, ceiling(f0hi) + margin_khz)
      narrowed = mfb_s .gt. mfa_s
      if (.not. narrowed) then
         mfa_s = mfa
         mfb_s = mfb
      endif
   end subroutine qso_sync_window

end module q65_cursor_mod
