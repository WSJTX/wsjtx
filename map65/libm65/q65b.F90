! This routine provides an interface between MAP65 and the Q65 decoder
! in WSJT-X.  All arguments are input data obtained from the MAP65 GUI.
! Raw Rx data are available as the 96 kHz complex spectrum ca(MAXFFT1)
! in cacb_mod.  If xpol is true, we also have cb(MAXFFT1) for the
! orthogonal polarization.  Decoded messages are sent back to the GUI
! on stdout.

      module q65b_mod
      implicit none
      contains

   subroutine write_wav_header(unit, nsamp, fs)
      integer, intent(in) :: unit, nsamp, fs
      integer :: datasize, file_size

      datasize  = nsamp * 2        ! 16-bit samples
      file_size = 36 + datasize

      write(unit) 'RIFF'
      write(unit) file_size
      write(unit) 'WAVE'
      write(unit) 'fmt '
      write(unit) 16               ! PCM header size
      write(unit) 1                ! PCM format
      write(unit) 1                ! mono
      write(unit) fs               ! sample rate
      write(unit) fs*2             ! byte rate
      write(unit) 2                ! block align
      write(unit) 16               ! bits per sample
      write(unit) 'data'
      write(unit) datasize
   end subroutine write_wav_header

      subroutine q65b(nutc, nqd, nxant, fcenter, nfcal, nfsample, ikhz, mousedf, ntol, xpol, &
                  mycall0, mygrid, hiscall0, hisgrid, mode_q65, f0, fqso, newdat, nagain, &
                  max_drift, ndop00, idec)

      use iso_c_binding
      use q65_decode
      use wideband_sync
      use timer_module, only: timer
      use debug_log
      use stdout_channel_mod, only: write_stdout
      use decodes_mod,  only: ldecoded, ndecodes
      use cacb_mod
      use four2a_mod
      use txpol_mod
      use iso_fortran_env, only: real64, int16
      use map65_mmdec_mod
      use npar_ptrs_mod, only: nrate_active, nfft_big_active, nfft_active, t_start, abort_decode, &
                               manualDecodeFlag, ndepth
      use sec_midn_mod, only: sec_midn

      implicit none

      !==== Dummy arguments =====================================================
      integer,      intent(in)    :: nutc
      integer,      intent(in)    :: nqd
      integer,      intent(in)    :: nxant
      real(real64),       intent(in)    :: fcenter
      integer,      intent(in)    :: nfcal
      integer,      intent(in)    :: nfsample
      integer,      intent(in)    :: ikhz
      integer,      intent(in)    :: mousedf
      integer,      intent(in)    :: ntol
      logical,      intent(in)    :: xpol
      character(len=12), intent(in)    :: mycall0
      character(len=6),  intent(in)    :: mygrid
      character(len=12), intent(in)    :: hiscall0
      character(len=6),  intent(in)    :: hisgrid
      integer,      intent(in)    :: mode_q65
      real(real64),       intent(in)    :: f0
      real,         intent(in)    :: fqso
      integer,      intent(in)    :: newdat
      integer,      intent(in)    :: nagain
      integer,      intent(in)    :: max_drift
      integer,      intent(in)    :: ndop00
      integer,      intent(out)   :: idec

      !==== Local parameters ====================================================
      ! MAXFFT1/2 are *max* sizes; actual runtime sizes are derived below.
      ! MAXFFT1 must be = nfft_big_active; MAXFFT2 must be = the
      ! downsampled-FFT length at the highest supported rate.
      integer, parameter :: MAXFFT1 = 56*192000
      integer, parameter :: MAXFFT2 = 336000*2
      integer, parameter :: NMAX    = 60*12000
      real(real64), parameter  :: RAD     = 57.2957795

      !==== Local variables =====================================================
      integer(int16) :: iwave(300*12000)
      complex   :: cx(0:MAXFFT2-1), cy(0:MAXFFT2-1), cz(0:MAXFFT2)
      integer   :: ipk1(1)
      integer   :: i, ia, ib, ifreq, ikhz1, ipk, ipol
      integer   :: j, ja, jb, k0, mhz, ndf, nfft1, nfft2
      integer   :: npol, nq65df, nsubmode, ntxpol, nutc00, nh
      integer   :: nfa, nfb
      integer   :: k0_click, mousedf_gate
      real      :: df, df3, f_ipk, f_mouse, fac
      real      :: freq1_00, frx, fsked, poldeg, r, snr1
      real(real64)    :: freq0, freq1
      character(len=12) :: mycall, hiscall
      character(len=4)  :: grid4
      character(len=28) :: msg00
      character(len=1)  :: cp
      character(len=2)  :: cmode
      character(len=256) :: linenew

      integer :: t_now, t_rate

      ! From q65_decode / wideband_sync / globals:
      !   real    :: xdt0
      !   integer :: nsnr0, nfreq0, nkhz_center
      !   character(len=3) :: cq0
      !   character(len=28) :: msg0
      !   type(sync_type) :: sync(:)
      ! These are assumed to be provided by the used modules.

      data nutc00 /-1/
      data msg00  /'                            '/
      save

      idec = -1
      nsnr0 = -99
      msg0 = ' '
      cq0 = '   '
      xdt0 = 0.0
      nfreq0 = 0

      call system_clock(t_now, t_rate)
    if (real(t_now - t_start)/real(t_rate) > 40.0) then
      abort_decode = .true.
      return
    endif

      ! ca/cb storage is sized at MAXFFT1 in cacb_mod; the *active* big FFT
      ! length is nfft_big_active, set from C++ via set_runtime_params_().
      call init_cacb(MAXFFT1)
      
      ! wideband_sync expects the active symspec FFT length
      call init_wideband_sync()


      if (mycall0(1:1) .ne. ' ') mycall = mycall0
      if (hiscall0(1:1) .ne. ' ') hiscall = hiscall0
      if (hisgrid(1:4) .ne. '    ') grid4 = hisgrid(1:4)

! Find best frequency and ipol from sync_dat, the "orange sync curve".
      ! df3 is the bin width of the symspec FFT (savg), based on the
      ! active runtime rate and FFT length.
      df3 = real(nrate_active)/real(nfft_active)
      ifreq = nint((1000.0*f0)/df3)

      ! 2026-09-09: for a wideband candidate (not a manual click), f0 is
      ! already a precise, sub-bin frequency estimate from wb_sync --
      ! searching +/-ntol (the GUI's Ftol, which can be very wide, e.g.
      ! 1000 Hz) around it for the "orange sync curve" peak lets a stronger
      ! NEARBY signal's peak dominate the search. With several closely-
      ! spaced Q65 candidates and a wide Ftol, a genuinely different
      ! candidate can resolve to that same dominant bin and get silently
      ! skipped by the ldecoded(ipk) check below as "already decoded", even
      ! though it's a distinct signal at a different frequency. Use ifreq
      ! directly instead, the same way k0 does a few lines down for this
      ! exact reason. Originally this also excluded nagain=1 (Decode button
      ! / Find Delta Phi repeat), but that path runs the same per-candidate
      ! loop over real wb_sync candidates as automatic decoding -- f0 is
      ! precise there too, so it needs the same fix, not the wide search.
      ! Only an actual manual click (manualDecodeFlag/=0), where the target
      ! itself is an imprecise mousefqso-driven guess, still needs it.
      if (manualDecodeFlag .ne. 0) then
         ia = nint(ifreq - ntol/df3)
         ib = nint(ifreq + ntol/df3)
         if (ia >= 1 .and. ia <= nfft_active .and. ib >= 1 .and. ib <= nfft_active) then
            ipk1 = maxloc(sync(ia:ib)%ccfmax)
         else
            go to 901
         endif
         ipk = ia + ipk1(1) - 1
      else
         if (ifreq >= 1 .and. ifreq <= nfft_active) then
            ipk = ifreq
         else
            go to 901
         endif
      endif
      snr1 = sync(ipk)%ccfmax

      ! TEMP diagnostic 2026-09-09 for the two-close-Q65-signals investigation.
      call dbg('q65b: ipk check at t=' // rtoa(sec_midn()) // &
               ' nqd=' // itoa(nqd) // ' f0=' // rtoa(real(f0)) // ' ntol=' // itoa(ntol) // &
               ' ifreq=' // itoa(ifreq) // ' ipk=' // itoa(ipk) // &
               ' ldecoded(ipk)=' // itoa(merge(1,0,ldecoded(ipk))) // &
               ' manualDecodeFlag=' // itoa(manualDecodeFlag) // ' nagain=' // itoa(nagain))
      ! ldecoded(ipk) exists to keep the wideband scan from repeatedly
      ! re-decoding a bin it already got a result from. A manual click is a
      ! deliberate, targeted request to decode at this exact bin, so it must
      ! not be skipped just because an earlier wideband pass happened to
      ! visit the same bin first.
      if (ldecoded(ipk) .and. manualDecodeFlag .eq. 0) then
         call dbg('q65b: SKIPPED (ldecoded already set) at t=' // rtoa(sec_midn()) // &
                  ' ipk=' // itoa(ipk))
         go to 900
      endif
      snr1 = sync(ipk)%ccfmax
      ! ipol was never declared and its value is never used
      ipol = 1
      if (xpol) ipol = sync(ipk)%ipol

      ! Runtime big-FFT length: nfft_big_active is 56 * nrate_active,
      ! set from C++ at startup. Clamp to MAXFFT1 for safety.
      nfft1 = min(nfft_big_active, MAXFFT1)

      ! Use a fixed small-FFT length that matches the legacy 96 kHz
      ! geometry and stays within MAXFFT2 at all supported rates.
      nfft2 = min(336000, MAXFFT2)

      ! Preserve the legacy 95238 special case; if you still use it,
      ! override the derived sizes with the historical ones.
      if (nfsample .eq. 95238) then
         nfft1 = min(5120000, MAXFFT1)
         nfft2 = min(322560,  MAXFFT2)
      endif

      ! Bin width of the big FFT: use the active runtime rate instead
      ! of a hardcoded 96000. The 95238 special case is preserved.
      df = real(nrate_active)/real(nfft1)
      if (nfsample .eq. 95238) df = 96000.0/real(nfft1)

      nh = nfft2/2
      f_mouse = 1000.0*(fqso + real(nrate_active/2000.0)) + mousedf
      f_ipk = ipk*df3
      ! k0 used to come from f_ipk (ipk*df3), i.e. re-derived from the coarse,
      ! JT65-oriented "orange sync curve" bin (df3 ~ 2.9 Hz resolution) rather
      ! than from the candidate's own already-refined frequency. For a
      ! wideband Q65 candidate, f0 (set by the caller from cand()%f, which
      ! wb_sync already computed with sub-bin precision) is a strictly better
      ! estimate of the true signal frequency than re-quantizing through ipk
      ! -- and the difference matters here because the actual decode below
      ! only searches a tight +/-10 Hz window (nfa=990..1010) around k0.
      ! Use f0 directly, same as the manual-click/nagain path already does
      ! via f_mouse below.
      k0 = nint((1000.0*f0 - 1000.0)/df)
      ! ipk (and therefore f_ipk) comes from maxloc'ing the JT65-oriented
      ! "orange sync curve" over the whole +/-ntol window -- fine for a
      ! wideband scan, but if a strong JT65 signal happens to sit anywhere
      ! in that window it will always win, even over an equal-strength Q65
      ! signal, because that curve doesn't score Q65 signals the same way.
      ! A manual click already tells us exactly where to look, so center
      ! the analysis window on the actual clicked frequency (f_mouse, which
      ! properly includes mousedf) instead of trusting that curve.
      ! 2026-09-09: dropped "nagain .eq. 1 .or." -- nagain=1 (Decode button
      ! / Find Delta Phi repeat) still runs the same per-candidate loop over
      ! real, precise wb_sync candidates that automatic decoding does; f0 is
      ! not an imprecise mousefqso click there, so forcing k0 to f_mouse
      ! made every candidate in that loop decode the SAME narrow window
      ! near the cursor instead of its own frequency. The one nagain=1 case
      ! where f0 genuinely comes from mousefqso (the "no candidate found"
      ! fallback in map65a.f90) already sets f0 = mousefqso + mousedf
      ! directly, so the default (unoverridden) k0 below lands on the same
      ! point anyway -- this override was redundant there, not needed.
      if (manualDecodeFlag .ne. 0) k0 = nint((f_mouse - 1000.0)/df)

      if (k0 .lt. nh .or. k0 .gt. nfft1 - nfft2 + 1) go to 900
      ! Likewise, snr1 is the sync-curve strength at ipk, which for a manual
      ! click may be the wrong bin entirely (see above) -- don't let it gate
      ! a manual decode attempt. Let q65_decode's own Q65-native search
      ! (below) determine success or failure instead.
      if (snr1 .lt. 1.5 .and. manualDecodeFlag .eq. 0) go to 900                      !### Threshold needs work? ###

      fac = 1.0/nfft2
      cx(0:nfft2 - 1) = ca(k0:k0 + nfft2 - 1)
      cx = fac*cx
      if (xpol) then
         cy(0:nfft2 - 1) = cb(k0:k0 + nfft2 - 1)
         cy = fac*cy
      endif

! Here cx and cy (if xpol) are frequency-domain data around the selected
! QSO frequency, taken from the full-length FFT computed in filbig().
! Values for fsample, nfft1, nfft2, df, and the downsampled data rate
! are as follows:

!  fSample  nfft1       df        nfft2  fDownSampled
!    (Hz)              (Hz)                 (Hz)
!----------------------------------------------------
!   96000  5376000  0.017857143  336000   6000.000
!   95238  5120000  0.018601172  322560   5999.994

      poldeg = 0.
      if (xpol) then
         ! NB: still uses the (possibly wrong-bin, see k0 above) ipk for
         ! manual clicks in xpol mode -- not exercised by current testing,
         ! but worth revisiting if xpol manual decode misbehaves similarly.
         poldeg = sync(ipk)%pol
         cz(0:MAXFFT2 - 1) = cos(poldeg/RAD)*cx + sin(poldeg/RAD)*cy
      else
         cz(0:MAXFFT2 - 1) = cx
      endif

      cz(MAXFFT2) = 0.
! Roll off below 500 Hz and above 2500 Hz.
      ja = nint(500.0/df)
      jb = nint(2500.0/df)
      do i = 0, ja
         r = 0.5*(1.0 + cos(i*3.14159/ja))
         cz(ja - i) = r*cz(ja - i)
         cz(jb + i) = r*cz(jb + i)
      enddo
      cz(ja + jb + 1:) = 0.

!Transform to time domain (real), fsample=12000 Hz
      call four2a(cz, 2*nfft2, 1, 1, -1)
      do i = 0, nfft2 - 1
         j = nfft2 - 1 - i
         iwave(2*i + 2) = int(max(-32768, min(32767, nint(real(cz(j))))), kind=2)
         iwave(2*i + 1) = int(max(-32768, min(32767, nint(aimag(cz(j))))), kind=2)
      enddo

      iwave(2*nfft2 + 1:) = 0

      nsubmode = mode_q65 - 1
      nfa = 990                   !Tight limits around ipk for the wideband decode
      nfb = 1010
      ! 2026-09-09: dropped "nagain .eq. 1 .or." -- see the matching note on
      ! k0 above. A real wideband candidate (nagain=0 or 1) already has a
      ! precise k0/target; widening the search to +/-ntol only makes sense
      ! for an actual manual click, where the target itself is imprecise.
      if (manualDecodeFlag .ne. 0) then
         ! For a manual click, search +/- ntol around the target (k0, set
         ! above) rather than the tight default -- ntol here is the GUI's
         ! ftol, so this is what makes "decode everything within ftol of
         ! the click, nothing outside it" hold for Q65.
         nfa = max(100, 1000 - ntol)
         nfb = min(2500, 1000 + ntol)
      endif

! NB: Frequency of ipk is now shifted to 1000 Hz.

      ! ndepth comes from npar_ptrs_mod, set from the GUI's Decode Depth
      ! menu (see MainWindow::decode() -> setNdepth()). This used to be a
      ! local variable "ndpth" hardwired to 3 here, which was never
      ! connected to the GUI at all -- Q65 always ran at depth 3
      ! regardless of what the user selected.
      call map65_mmdec(nutc, iwave, nqd, 60, nsubmode, nfa, nfb, 1000, ntol, &
                       newdat, nagain, max_drift, ndepth, mycall, hiscall0, hisgrid)

      MHz = fcenter
      freq0 = MHz + 0.001d0*ikhz

      call dbg('q65b: map65_mmdec result at t=' // rtoa(sec_midn()) // &
               ' nqd=' // itoa(nqd) // ' f0=' // rtoa(real(f0)) // ' ipk=' // itoa(ipk) // &
               ' nsnr0=' // itoa(nsnr0))

      if (nsnr0 .gt. -99) then
         ldecoded(ipk) = .true.
         nq65df = nint(1000*( 0.001*k0*df + nkhz_center - real(nrate_active)/2000.0 + 1.000 - 1.27046 - ikhz )) - nfcal
         nq65df = nq65df + nfreq0 - 1000
         ! Gate comparison target, expressed in the same ikhz-relative frame
         ! as nq65df above -- computed from f0 (the unambiguous target
         ! frequency for this call, for both a manual click and a wideband
         ! candidate) via the same k0/nq65df formula, rather than from raw
         ! mousedf, which is relative to mousefqso and only matches ikhz's
         ! frame when the target happens to already be sub-kHz. mousedf
         ! itself is left untouched -- f_mouse/k0 above still need it in its
         ! original, unmodified form.
         k0_click = nint((1000.0*f0 - 1000.0)/df)
         mousedf_gate = nint(1000*( 0.001*k0_click*df + nkhz_center - real(nrate_active)/2000.0 + &
                              1.000 - 1.27046 - ikhz )) - nfcal
         npol = nint(poldeg)
         if (nxant .ne. 0) then
            npol = npol - 45
            if (npol .lt. 0) npol = npol + 180
         endif
         call txpol(xpol, msg0(1:28), mygrid, npol, nxant, ntxpol, cp) ! was 1:22
         ikhz1 = ikhz
         ndf = nq65df
         if (ndf .gt. 500) ikhz1 = ikhz + (nq65df + 500)/1000
         if (ndf .lt. -500) ikhz1 = ikhz + (nq65df - 500)/1000
         ndf = nq65df - 1000*(ikhz1 - ikhz)

         if (nqd .eq. 1 .and. abs(nq65df - mousedf_gate) .lt. ntol) then

            write (linenew, '("!",I3.3,I5,I4,I6.4,F5.1,I5," : ",A28,A3,I4,1X,A1)') &
               ikhz1, ndf, npol, nutc, xdt0, nsnr0, msg0(1:28), cq0, ntxpol, cp
            call write_stdout(trim(linenew)//new_line('a'))
         endif

! Write to lu 26, for Messages and Band Map windows
         cmode = ': '
         cmode(2:2) = char(ichar('A') + mode_q65 - 1)
         freq1 = freq0 + 0.001d0*(ikhz1 - ikhz)
         write (26, 1014) freq1, ndf, 0, 0, 0, xdt0, npol, 0, nsnr0, nutc, msg0(1:28), &
            ':', cp, cmode ! was 1:22
1014     format(f8.3, i5, 3i3, f5.1, i4, i3, i4, i5.4, 4x, a28, 1x, 2a1, 2x, a2) ! was a22

! Suppress writing duplicates (same time, decoded message, and frequency)
! to map65_rx.log
         if (nutc .ne. nutc00 .or. msg0(1:28) .ne. msg00 .or. freq1 .ne. freq1_00) then
! Write to file map65_rx.log:
            ndecodes = ndecodes + 1
            write (21, 1110) freq1, ndf, xdt0, npol, nsnr0, nutc, msg0(1:28), &
               cmode(2:2), cq0
1110        format(f8.3, i5, f5.1, 2i4, i5.4, 2x, a28, ': ', a1, 2x, a3)
            nutc00 = nutc
            msg00 = msg0(1:28)
            freq1_00 = freq1
            frx = 0.001*k0*df + nkhz_center - real(nrate_active/2000.0) + 1.0 - 0.001*nfcal
            fsked = frx - 0.001*ndop00/2.0 - 1.5
            write (12, 1120) nutc, fsked, xdt0, nsnr0, trim(msg0)
1120        format(i4.4, f9.3, f7.2, i5, 2x, a, i6)
         endif
      endif

900   close (13)
      close (17)
      idec = -1
      ! cq0 is blank unless this call's own decode attempt set it (see the
      ! reset at subroutine entry); only parse it when there's really a
      ! digit there, otherwise there's nothing for READ to find.
      if (cq0(2:2) .ne. ' ') read (cq0(2:2), *) idec
      return
901   close (13)
      close (17)
      idec = -1
      return
   end subroutine q65b

end module q65b_mod
