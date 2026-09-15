module map65a_mod
   use txpol_mod
   use trimlist_mod
   use iso_fortran_env, only: real64
   implicit none
contains

   subroutine map65a(dd, newdat, nutc, fcenter, ntol, idphi, nfa, nfb, &
                     mousedf, mousefqso, nagain, ndecdone, nfshift, ndphi, max_drift, &
                     nfcal, nkeep, mcall3b, nsum, nsave, nxant, mycall, mygrid, &
                     neme, ndepth, nstandalone, hiscall, hisgrid, nhsym, nfsample, &
                     ndiskdat, nxpol, nmode, ndop00)

      use iso_c_binding
      use wideband_sync
      use timer_module, only: timer
      use debug_log, only: dbg, itoa, rtoa
      use sec_midn_mod, only: sec_midn
      use q65b_mod
      use decode1a_mod
      use ccf65_legacy_mod
      use pctile_mod
      use stdout_channel_mod, only: write_stdout
      use decodes_mod, only: nhsym1, ldecoded, ljt65decoded, ndecodes, mcall3a, decodes_init
      use display_mod
      use timf2_mod
      use getdphi_mod
      use datcom_ptrs_mod, only: ss_old, savg_old
      use npar_ptrs_mod,  only: nsmax_active, nrate_active, nfft_active, t_start, abort_decode, manualDecodeFlag
      use sec0_mod, only: sec0
      use q65_decode, only: nsnr0

      implicit none

      integer, parameter :: MAXMSG = 1000
      real, parameter :: RESULT_DT_TOLERANCE = 0.2
      ! Neighborhood radius (in symspec FFT bins) for ljt65decoded's
      ! cross-call "already decoded" check/mark, below. At df ~= 2.9 Hz/bin
      ! this is ~+/-15 Hz, covering the couple-of-bin spread a single real
      ! JT65 signal can show across separate automatic calls.
      integer, parameter :: JT65_LOCAL_BINS = 5
      real, intent(in) :: dd(4, nsmax_active)
      integer, intent(inout) :: newdat
      integer, intent(inout) :: nutc
      real(real64), intent(in) :: fcenter
      integer, intent(in) :: ntol, idphi, nfa, nfb
      integer, intent(in) :: mousedf, mousefqso, nagain
      integer, intent(inout) :: ndecdone
      integer, intent(in) :: nfshift
      integer, intent(inout) :: ndphi
      integer, intent(in) :: max_drift, nfcal, nkeep
      integer, intent(inout) :: mcall3b
      integer, intent(inout) :: nsum, nsave
      integer, intent(in) :: nxant
      character(len=12), intent(in) :: mycall, hiscall
      character(len=6), intent(in) :: mygrid, hisgrid
      integer, intent(in) :: neme, ndepth, nstandalone
      integer, intent(in) :: nhsym, nfsample, ndiskdat, nxpol, nmode, ndop00

      ! --- local variables (unchanged) ---
      real tavg(-50:50), base(4), sig(MAXMSG,30), a(5)
      real :: df, dphi, dt, dt2, fa, fb, flip, flipk, foffset, freq, freq0, fshort
      real :: ftol, pol, qual, s2db, smax, snr2, ssmax, sync1, sync10, sync2, syncshort
      real :: tdec, thresh0, thresh1, tsec0, fshort0, fqso, syncshort0
      real(real64) :: f0
      character(len=22) msg(MAXMSG)
      character(len=3) shmsg0(4)
      character(len=1) :: cp, cm
      integer indx(MAXMSG)
      integer :: ipol, mode65
      integer :: i, ia, ib, i0, icand, idf, ifreq, ii, iii, ikhz
      integer :: iloop, ip000, ip001, ipol2, j, jp, jpmax, jpz
      integer :: k, km, mfa, mfb, mhz, mode_q65
      integer :: mousefqso0, n, ncand, ndf, ndf0, ndf1, ndf2
      integer :: nfile, nflip, nhist, nhzdiff, nid, nkhz, nkm, nkv
      integer :: noffset, npol, nqd, nqual, nsync1, nsync2, ntry
      integer :: nts_jt65, nts_q65, ntxpol, nutc0, nwrite, nwrite_q65, nz
      integer :: idec
      logical xpol, bq65, q65b_called
      logical candec(MAX_CANDIDATES)
      character(len=22) decoded, blank, decoded_jt65
      character(len=2) cmode
      real short(3, nfft_active)
      real qphi(12)
      type(candidate) :: cand(MAX_CANDIDATES)
      real(real64) :: f00
      character(len=256) :: line
      integer :: n_rms, i2
      real*8 :: rms, sumsq, v
      real*4 :: tsec_mod
      integer :: t_now, t_rate
      logical :: m65_inited = .false.
      logical :: abort_saved
      integer :: icenter
      logical :: initialization_only, shorthand_detected, jt65_success, q65_success
      logical :: already_decoded_nearby
      real :: best_sync1, best_dt, best_flipk, best_syncshort, best_snr2, best_dt2
      integer :: best_i, best_ipol2
      real :: sync1_tmp, dt_tmp, flipk_tmp, syncshort_tmp, snr2_tmp, dt2_tmp
      integer :: ipol_tmp, ipol2_tmp,ftol_bins, manualDecodeFlag_initial
      real :: freq_q65
      integer :: nhsym_prev_call

      data blank/'                      '/, cm/'#'/
      data shmsg0/'ATT','RO ','RRR','73 '/
      data nfile/0/, nutc0/-999/, nid/0/, ip000/1/, ip001/1/, mousefqso0/-999/
      data nhsym_prev_call/-1/
      save

      real(c_float), pointer :: ss_dec(:,:,:)
      real(c_float), pointer :: savg_dec(:,:)

      manualDecodeFlag_initial = manualDecodeFlag

!------------------------------------------------------------
! BUFFER SELECTION
!------------------------------------------------------------
      ! Always use the snapshot (see decode0.f90) -- ss/savg are live and
      ! can be reset/overwritten mid-decode by symspec_() on the GUI thread
      ! once the next cycle's UDP data starts arriving.
      ss_dec   => ss_old
      savg_dec => savg_old

      abort_decode = .false.

      if (.not. m65_inited) then
         call decodes_init()
         call init_wideband_sync()
         m65_inited = .true.
      endif

      rewind 12
      ndecodes = 0
      km = 0
      ipol = 1

!------------------------------------------------------------
! BASIC DECODE SETUP (shared by manual + wideband)
!------------------------------------------------------------
      ! run_m65 can legitimately re-fire map65a() several times back-to-back
      ! at the same nhsym1 value before nhsym itself advances. Reset
      ! ldecoded/ljt65decoded only on a genuinely new cycle at nhsym1, not a
      ! repeat call already at that value, so a repeat doesn't throw away
      ! "already reported this bin" memory from moments earlier and
      ! rediscover/re-report the same decode. A manual click (nagain/=0) is
      ! always a deliberate one-off request and always gets a fresh ledger.
      if ((nhsym .eq. nhsym1 .and. nhsym_prev_call .ne. nhsym1) .or. nagain .ne. 0) then
         ldecoded = .false.
         ljt65decoded = .false.
         call dbg('map65a: ldecoded/ljt65decoded RESET at t=' // rtoa(sec_midn()) // &
                  ' nhsym=' // itoa(nhsym) // ' nhsym_prev_call=' // itoa(nhsym_prev_call) // &
                  ' nagain=' // itoa(nagain))
      else if (nhsym .eq. nhsym1) then
         call dbg('map65a: ldecoded/ljt65decoded NOT reset (repeat call at nhsym1) at t=' // rtoa(sec_midn()) // &
                  ' nhsym=' // itoa(nhsym))
      endif
      nhsym_prev_call = nhsym
      if (ndiskdat .eq. 1) then
         ldecoded = .false.
         ljt65decoded = .false.
      endif

      df = real(nrate_active)/real(nfft_active)
      if (nfsample .eq. 95238) df = 95238.1/real(nfft_active)

      mode65 = mod(nmode,10)
      if (mode65 .eq. 3) mode65 = 4
      mode_q65 = nmode/10
      nts_jt65 = mode65
      nts_q65 = 2**(mode_q65 - 1)
      xpol = (nxpol .ne. 0)

      nwrite_q65 = 0
      bq65 = mode_q65 .gt. 0
      
      mcall3a = mcall3b
      mousefqso0 = mousefqso
      if (.not. xpol) ndphi = 0
      nsum = 0

      dphi = idphi/57.2957795
      foffset = 0.001*(1270 + nfcal)
      iloop = 0

      ! qphi is "save"d across calls (blanket SAVE above) but only gets
      ! written for a trial (iloop=1..12) that actually decodes something;
      ! without this reset, a trial that finds nothing leaves behind
      ! whatever unrelated value qphi(iloop) held from an earlier Find-dPhi
      ! run, silently mixing stale data into getdphi's best-fit calculation.
      if (ndphi .eq. 1) qphi = 0.0

2     if (ndphi .eq. 1) dphi = 30*iloop/57.2957795

      if (nutc .ne. nutc0) nfile = nfile + 1
      nutc0 = nutc

      !### Should use AppDir! ###
      open (23, file='CALL3.TXT', status='unknown')

!------------------------------------------------------------
! MANUAL NARROWBAND DECODE PATH
!------------------------------------------------------------
      if (manualDecodeFlag /= 0) then
         abort_saved = abort_decode
         km = 0
         icenter = nfft_active/2 + 1

         ! JT65/Q65 RF frame (kHz)
         foffset = 0.001*(1270 + nfcal)
         fqso    = mousefqso + foffset - 0.5*(nfa + nfb) + nfshift

         ! Manual clicked RF (kHz)
         freq    = fqso + 0.001*mousedf

         ! Bin index
         i = nint(freq*1000.0/df) + icenter

         ! Tolerance from GUI
         ftol      = real(ntol)      ! Hz
         ftol_bins = nint(ftol / df)

         i = nint(freq*1000.0/df) + icenter   ! bin corresponding to that RF
         i = max(51, min(nfft_active - 51, i))

         ! --- local search around clicked bin within ftol ---
         ftol = real(ntol)
         ftol_bins = nint(ftol / df)

         jpz = merge(4,1,xpol)

         best_sync1     = -1.e9
         best_i         = i
         best_dt        = 0.0
         best_flipk     = 0.0
         best_syncshort = 0.0
         best_snr2      = 0.0
         best_ipol2     = 1
         best_dt2       = 0.0

         do ii = -ftol_bins, ftol_bins
            iii = i + ii
            if (iii < 51 .or. iii > nfft_active - 51) cycle

            ssmax = 1.e30
            call timer('ccf65   ',0)
            call ccf65(ss_dec(:,:,iii), nhsym, ssmax, sync1_tmp, ipol_tmp, jpz, dt_tmp, flipk_tmp, &
                     syncshort_tmp, snr2_tmp, ipol2_tmp, dt2_tmp)
            call timer('ccf65   ',1)

            if (sync1_tmp > best_sync1) then
               best_sync1     = sync1_tmp
               best_i         = iii
               best_dt        = dt_tmp
               best_flipk     = flipk_tmp
               best_syncshort = syncshort_tmp
               best_snr2      = snr2_tmp
               best_ipol2     = ipol2_tmp
               best_dt2       = dt2_tmp
            endif
         enddo

         ! use best bin and metrics from the search
         i         = best_i
         sync1     = best_sync1
         dt        = best_dt
         flipk     = best_flipk
         syncshort = best_syncshort
         snr2      = best_snr2
         ipol2     = best_ipol2
         dt2       = best_dt2

         thresh1 = 1.0
         nflip   = nint(flipk)

         !===========================
         ! SHORTHAND DETECTION (JT65)
         !===========================
         shorthand_detected = .false.

         thresh0 = 1.0
         if (syncshort > thresh0 .and. mode65 > 0) then
            shorthand_detected = .true.

            km = 1
            sig(1,1) = nfile
            sig(1,2) = nutc
            sig(1,3) = freq
            sig(1,4) = syncshort
            sig(1,5) = dt2
            sig(1,6) = 45*(ipol2 - 1)/57.2957795
            sig(1,7) = 0
            sig(1,8) = snr2
            sig(1,9) = 0
            sig(1,10)= 0
            sig(1,12)= savg_dec(ipol2,i)
            msg(1)   = shmsg0(1)
         endif

         ! Bin frequency for this click (for decode1a’s f00)
         f00  = (i - 1)*df
        
         ikhz = nint(freq + 0.5*(nfa + nfb) - foffset) - nfshift
         idf = nint(1000.0*(freq + 0.5*(nfa + nfb) - foffset - (ikHz + nfshift)))

         noffset = nint(1000.0*(freq - fqso) - mousedf)

         ! JT65-specific rejects only when JT65 is active
         if (mode65 > 0) then
            if (sync1 <= thresh1) then

               if (.not. bq65) then
                     ! JT65-only mode → real reject
                     newdat = 0
                     km     = 0
                     ncand  = 0
                     msg(1) = blank
                     msg(2) = blank
                     do j = 1, 30
                        sig(1,j) = 0.0
                     enddo
                     manualDecodeFlag = 0
                     return
               endif

               ! JT65 failed but Q65 is active → DO NOT RETURN
            endif

            if (abs(noffset) > ntol) then

               if (.not. bq65) then
                     ! JT65-only mode → real reject

                  newdat = 0
                  km     = 0
                  ncand  = 0

                  msg(1) = blank
                  msg(2) = blank

                  do j = 1, 30
                     sig(1,j) = 0.0
                  enddo

                  manualDecodeFlag = 0
                  return
               endif

               ! JT65 failed but Q65 is active → DO NOT RETURN
            endif
         endif

         !===========================
         ! JT65 MANUAL DECODE (if active)
         !===========================
         jt65_success = .false.
         decoded_jt65 = '                      '

         if (mode65 > 0 .and. sync1 > thresh1 .and. abs(noffset) <= ntol) then

            call timer('decode1a',0)
            ifreq = i

            call decode1a(dd,newdat,f00,nflip,mode65,nfsample,xpol,mycall,hiscall, &
                          hisgrid,neme,ndepth,1,dphi,ndphi,nutc,ikHz,idf,ipol,ntol, &
                          sync2,a,dt,pol,nkv,nhist,nsum,nsave,qual,decoded_jt65)
            call timer('decode1a',1)

            abort_decode = abort_saved

            if (decoded_jt65 /= '                      ') then
               jt65_success = .true.

               km = km + 1
               sig(km,1) = nfile
               sig(km,2) = nutc
               sig(km,3) = freq + 0.5*(nfa+nfb)
               sig(km,4) = sync1
               sig(km,5) = dt
               sig(km,6) = pol
               sig(km,7) = flipk
               sig(km,8) = sync2
               sig(km,9) = nkv
               sig(km,10)= qual
               sig(km,12)= savg_dec(ipol,i)
               sig(km,13)= a(1)
               sig(km,14)= a(2)
               sig(km,15)= a(3)
               sig(km,16)= a(4)
               sig(km,18)= nhist
               msg(km)   = decoded_jt65
            endif
         endif

         !===========================
         ! Q65 MANUAL DECODE (if active)
         !===========================
         q65_success = .false.

         if (bq65) then
            ! Include mousedf (the sub-kHz part of the click) so the search
            ! is centered on the actual clicked frequency, not just rounded
            ! to the nearest kHz -- matters once q65b targets this exactly
            ! (see the manualDecodeFlag handling around k0 in q65b.F90).
            freq = mousefqso + 0.001*mousedf
            freq_q65 = freq
            f0       = freq_q65 - (nkhz_center - real(nrate_active)/2000.0 - 1.27046)
            nqd      = 1   ! target-frequency decode, like wideband's quick pass at
                           ! fQSO; also required so q65b's own write_stdout/output
                           ! path (gated on nqd==1) actually emits the result
            ikhz     = nint(freq_q65)
            ! NB: mousedf must stay relative to mousefqso here, unmodified --
            ! q65b.F90 also uses it (via f_mouse) to pick k0, the actual
            ! sample-extraction point for the decode. A previous attempt to
            ! re-reference it to ikhz here (for q65b's output gate, which
            ! compares against ikhz-relative nq65df) fixed the gate but broke
            ! k0/f_mouse, silently decoding whatever signal sits near the
            ! *other* kHz bucket instead of the one actually clicked. The
            ! gate's reference-frame mismatch is now fixed inside q65b.F90
            ! itself instead, from f0 (already unambiguous), leaving this
            ! mousedf untouched for f_mouse/k0 to keep working correctly.

            call timer('q65b    ', 0)
            call q65b(nutc, nqd, nxant, fcenter, nfcal, nfsample, ikhz, mousedf, &
                      ntol, xpol, mycall, mygrid, hiscall, hisgrid, mode_q65, f0, fqso, &
                      newdat, nagain, max_drift, ndop00, idec)
            call timer('q65b    ', 1)

            ! NB: idec, as returned by q65b, is not a trustworthy success flag:
            ! q65b derives it by parsing cq0(2:2) (see q65b.F90, label 900), and
            ! cq0 is not reset on a failed attempt, so it can still read back
            ! whatever digit was left over from an earlier, unrelated decode.
            ! nsnr0 is q65b's own internal success test (freshly reset to -99
            ! immediately before it attempts this decode), so use that instead.

            q65_success = (nsnr0 .gt. -99)
            ! On success, q65b has already written the decoded text itself
            ! (write_stdout, plus units 26/21/12) because nqd=1 and the result
            ! is within ntol of mousedf -- the same mechanism the wideband
            ! quick-decode pass uses. Nothing further to add to km/msg/sig here.
         endif

         !===========================
         ! EMIT DECODE(S) DIRECTLY
         !===========================
         ! NB: this used to "go to 600" to reuse the wideband write/rescan
         ! block, but that block lives inside "do nqd = 1, 0, -1 ... enddo"
         ! while this code is lexically outside that loop. Branching into
         ! the interior of a DO construct from outside it is illegal and
         ! was causing a stray re-entrant q65b call (via the stale, never-
         ! populated "cand" array) plus a "if (mode65.eq.0) km = 0" reset
         ! that silently discarded the manual Q65 decode before it could be
         ! written. Emit the km/msg/sig entries here instead.
         if (jt65_success .or. q65_success .or. shorthand_detected) then
            nwrite = 0

            do k = 1, km
               decoded = msg(k)
               if (decoded .ne. '                      ') then
                  nutc = sig(k,2)
                  freq = sig(k,3)
                  sync1 = sig(k,4)
                  dt = sig(k,5)
                  npol = nint(57.2957795*sig(k,6))
                  flip = sig(k,7)
                  sync2 = sig(k,8)
                  nkv = sig(k,9)
                  nqual = sig(k,10)
                  if (flip .lt. 0.0) then
                     i = len_trim(decoded)
                     if (i .eq. 0) stop 'Error in message format'
                     if (i .le. 18) decoded(i + 2:i + 4) = 'OOO'
                  endif
                  nkHz = nint(freq - foffset) - nfshift
                  mhz = fcenter
                  f0 = mhz + 0.001*nkHz
                  ndf = nint(1000.0*(freq - foffset - (nkHz + nfshift)))
                  nsync1 = sync1
                  s2db = 10.0*log10(sync2) - 40
                  nsync2 = nint(s2db)
                  if (decoded(1:4) .eq. 'RO  ' .or. decoded(1:4) .eq. 'RRR  ' .or. &
                      decoded(1:4) .eq. '73  ') then
                     nsync2 = nint(1.33*s2db + 2.0)
                  endif

                  nwrite = nwrite + 1
                  if (nxant .ne. 0) then
                     npol = npol - 45
                     if (npol .lt. 0) npol = npol + 180
                  endif

                  call txpol(xpol, decoded, mygrid, npol, nxant, ntxpol, cp)

                  write (line, '("!",I3,I5,I4,I6.4,F5.1,I5,1X,A1,1X,A22,I2,I5,I5,1X,A1)') &
                     nkHz, ndf, npol, nutc, dt, nsync2, cm, decoded, nkv, nqual, ntxpol, cp
                  call write_stdout(trim(line)//new_line('a'))
               endif
            enddo  ! k=1,km

            manualDecodeFlag = 0
            return
         endif

         newdat = 0
         km     = 0
         ncand  = 0
         msg(1) = blank
         msg(2) = blank
         do j = 1, 30
            sig(1,j) = 0.0
         enddo
         manualDecodeFlag = 0
         return
      endif


!------------------------------------------------------------
! WIDEBAND CODE RESUMES HERE
!------------------------------------------------------------

! km is "save"d across calls, but this point runs once per Find-dPhi trial
! (iloop 0..12 via "go to 2" below) as well as once for a normal decode.
! Without resetting it here, each later trial's "do k=1,km" write-out loop
! replays every earlier trial's decode(s) again verbatim -- the repeated,
! made-up-looking decodes seen with Find Delta Phi.
km = 0

! ljt65decoded/ldecoded mark a bin as already decoded for the rest of this
! accumulation cycle (see the reset near subroutine entry, and their use
! below) -- correct for suppressing a genuinely repeated automatic call,
! but wrong for a Find Delta Phi trial: each of the 13 trials (iloop 0..12,
! via "go to 2" below) is a deliberate, fresh re-probe of the same
! frequency at a different phase hypothesis, not a repeat of the same
! request. Without resetting here too, trial 0's decode marks the bin, and
! every later trial then sees it as "already decoded" and never calls
! decode1a() again, leaving qphi(iloop) unpopulated for the rest of the
! sweep.
if (ndphi .eq. 1) then
   ldecoded = .false.
   ljt65decoded = .false.
endif

ftol = 0.010
fqso = mousefqso + foffset - 0.5*(nfa + nfb) + nfshift
nkhz_center = nint(1000.0*(fcenter - int(fcenter)))
mfa = nfa - nkhz_center + int(nrate_active/2000.0)
mfb = nfb - nkhz_center + int(nrate_active/2000.0)

tsec_mod = real(mod(t_start, 60), kind=4)
! Number of samples to use for RMS check
n_rms = min(322, 2048)

sumsq = 0.0d0
do i2 = 1, n_rms
   v = ss_dec(1, i2, 1)
   sumsq = sumsq + v*v
end do

if (n_rms > 0) then
   rms = sqrt(sumsq / dble(n_rms))
else
   rms = 0.0d0
endif

if (nagain .eq. 0) then
   call timer('get_cand', 0)
   call get_candidates(ss_dec, savg_dec, xpol, nhsym, mfa, mfb, nts_jt65, nts_q65, cand, ncand)
   call timer('get_cand', 1)
   candec = .false.
   ! TEMP diagnostic 2026-09-09 for the missing-upper-Q65-decode investigation.
   call dbg('map65a: get_candidates done at t=' // rtoa(sec_midn()) // &
            ' ncand=' // itoa(ncand) // ' n_q65cand=' // itoa(count(cand(1:max(ncand,0))%iflip == 0)) // &
            ' bq65=' // itoa(merge(1,0,bq65)) // ' xpol=' // itoa(merge(1,0,xpol)) // &
            ' nhsym=' // itoa(nhsym) // ' manualDecodeFlag_initial=' // itoa(manualDecodeFlag_initial))
endif

      do nqd = 1, 0, -1

         call system_clock(t_now, t_rate)
         call dbg('map65a: nqd loop top at t=' // rtoa(sec_midn()) // &
                  ' nqd=' // itoa(nqd) // ' elapsed=' // rtoa(real(t_now - t_start)/real(t_rate)) // &
                  ' manualDecodeFlag_initial=' // itoa(manualDecodeFlag_initial))
         if (real(t_now - t_start)/real(t_rate) > 40.0) then
            abort_decode = .true.
            call dbg('map65a: ABORT (40s budget exceeded) at t=' // rtoa(sec_midn()) // ' nqd=' // itoa(nqd))
            go to 700
         endif

         if (manualDecodeFlag_initial == 1 .and. nqd == 0) then
            call dbg('map65a: nqd=0 CYCLE-skipped (manualDecodeFlag_initial=1) at t=' // rtoa(sec_midn()))
            cycle
         endif


         if (nqd .eq. 1) then                     !Quick decode, at fQSO
            fa = 1000.0*(fqso + 0.001*mousedf) - ntol
            fb = 1000.0*(fqso + 0.001*mousedf) + ntol + 4*(96000.0/1783.0)
         else                                  !Wideband decode at all freqs
            fa = -1000*0.5*(nfb - nfa) + 1000*nfshift
            fb = 1000*0.5*(nfb - nfa) + 1000*nfshift

         ! Debug: report JT65 wideband search window

!write(sfa, '(F20.6)') fa
!write(sfb, '(F20.6)') fb
!write(sspan, '(F20.6)') (fb-fa)/1000.0

         endif
         icenter = nfft_active/2 + 1
         ia = nint(fa/df) + icenter
         ib = nint(fb/df) + icenter
         ia = max(51, ia)
         ib = min(nfft_active - 51, ib)
         if (ndiskdat .eq. 1 .and. mode65 .eq. 0) ib = ia

         nkm = 1
         freq0 = -999.
         sync10 = -999.
         fshort0 = -999.
         syncshort0 = -999.
         ntry = 0
         short = 0.                                 !Zero the whole short array
         jpz = 1
         if (xpol) jpz = 4

         ! TEMP diagnostic 2026-09-09 for the missing-other-JT65-signal investigation.
         call dbg('map65a: JT65 sweep window at t=' // rtoa(sec_midn()) // &
                  ' nqd=' // itoa(nqd) // ' fa=' // rtoa(fa) // ' fb=' // rtoa(fb) // &
                  ' ia=' // itoa(ia) // ' ib=' // itoa(ib) // ' km_entering=' // itoa(km))

         do i = ia, ib                               !Search over freq range

            call system_clock(t_now, t_rate)
            if (real(t_now - t_start)/real(t_rate) > 40.0) then
               call dbg('Decode abort: exceeded 40 seconds in do i = ia, ib pass, nqd=' // itoa(nqd) // ' i=' // itoa(i))
               abort_decode = .true.
               ! "go to 700" here used to jump past the Q65 candidate-decode
               ! loop below (do icand = 1, ncand), which only runs after this
               ! JT65 sweep completes -- so a slow JT65 sweep (e.g. many
               ! birdie-triggered decode1a calls) could burn the whole 40 s
               ! budget and starve Q65 of any decode attempt at all, even
               ! though its candidates were already found by get_candidates()
               ! before this loop started. Just stop scanning more JT65 bins
               ! instead, so Q65 still gets its turn this pass.
               exit
            endif

            freq = 0.001*(i - icenter)*df
!  Find the local base level for each polarization; update every 10 bins.
            if (mod(i - ia, 10) .eq. 0) then
               do jp = 1, jpz
                  do ii = -50, 50
                     iii = i + ii
                     if (iii .ge. 1 .and. iii .le. nfft_active) then
                        tavg(ii) = savg_dec(jp, iii)
                     else
                        write (13, *) 'Error in iii:', iii, ia, ib, fa, fb
                        flush (13)
                        go to 900
                     endif
                  enddo
                  call pctile(tavg, 101, 50, base(jp))
               enddo
            endif

!  Find max signal at this frequency
            smax = 0.
            jpmax = 1
            do jp = 1, jpz
               if (savg_dec(jp, i)/base(jp) .gt. smax) then
                  smax = savg_dec(jp, i)/base(jp)
                  jpmax = jp
               endif
            enddo
            
            if (smax .gt. 1.1 .or. ia .eq. ib) then

!  Look for JT65 sync patterns and shorthand square-wave patterns.
               call timer('ccf65   ', 0)
               ssmax = 1.e30
               call ccf65(ss_dec(:,:,i), nhsym, ssmax, sync1, ipol, jpz, dt, flipk, &
                  syncshort, snr2, ipol2, dt2)
               call timer('ccf65   ', 1)
               ! 2026-09-12: instrumentation for the "identical anomalous
               ! dtbest across several consecutive nqd=0 candidates" investigation.
               ! Logging ccf65's own per-candidate dt (the value that becomes
               ! decode1a's dt00 input) so a live capture can show directly
               ! whether the staleness/repetition already exists at THIS
               ! input, before decode1a/afc65b ever runs.
               call dbg('map65a: ccf65 result at t=' // rtoa(sec_midn()) // &
                        ' nqd=' // itoa(nqd) // ' i=' // itoa(i) // ' dt=' // rtoa(dt) // &
                        ' sync1=' // rtoa(sync1) // ' ntry=' // itoa(ntry))
               if (mode65 .eq. 0) syncshort = -99.0     !If "No JT65", don't waste time

! ########################### Search for Shorthand Messages #################
!  Is there a shorthand tone above threshold?
               thresh0 = 1.0
!  Use lower thresh0 at fQSO
               if (nqd .eq. 1 .and. ntol .le. 100) thresh0 = 0.
               if (syncshort .gt. thresh0) then
! ### Do shorthand AFC here (or maybe after finding a pair?) ###
                  short(1, i) = syncshort
                  short(2, i) = dt2
                  short(3, i) = ipol2

!  Check to see if lower tone of shorthand pair was found.
                  do j = 2, 4
                     i0 = i - nint(j*mode65*10.0*(11025.0/4096.0)/df)
!  Should this be i0 +/- 1, or just i0?
!  Should we also insist that difference in DT be either 1.5 or -1.5 s?
                     if (short(1, i0) .gt. thresh0) then
                        fshort = 0.001*(i0 - icenter)*df
                        noffset = 0
                        if (nqd .eq. 1) noffset = nint(1000.0*(fshort - fqso) - mousedf)
                        if (abs(noffset) .le. ntol) then
!  Keep only the best candidate within ftol.
!### NB: sync2 was not defined here!
!                       sync2=syncshort                   !### try this ???
                           if (fshort - fshort0 .le. ftol .and. &
                            syncshort.gt.syncshort0 .and. nkm.eq.2) km=km-1
                           if (fshort - fshort0 .gt. ftol .or. &
                               syncshort .gt. syncshort0) then
                              if (km .lt. MAXMSG) km = km + 1
                              sig(km, 1) = nfile
                              sig(km, 2) = nutc
                              sig(km, 3) = fshort + 0.5*(nfa + nfb)
                              sig(km, 4) = syncshort
                              sig(km, 5) = dt2
                              sig(km, 6) = 45*(ipol2 - 1)/57.2957795
                              sig(km, 7) = 0
                              sig(km, 8) = snr2
                              sig(km, 9) = 0
                              sig(km, 10) = 0
!                           sig(km,11)=rms0
                              sig(km, 12) = savg_dec(ipol2, i)
                              sig(km, 13) = 0
                              sig(km, 14) = 0
                              sig(km, 15) = 0
                              sig(km, 16) = 0
!                           sig(km,17)=0
                              sig(km, 18) = 0
                              msg(km) = shmsg0(j)
                             
                              fshort0 = fshort
                              syncshort0 = syncshort
                              nkm = 2
                           endif
                        endif
                     endif
                  enddo
               endif

! ########################### Search for Normal Messages ###########
!  Is sync1 above threshold?
               thresh1 = 1.0
!  Use lower thresh1 at fQSO
               if (nqd .eq. 1 .and. ntol .le. 100) thresh1 = 0.
               noffset = 0
               if (nqd .ge. 1) noffset = nint(1000.0*(freq - fqso) - mousedf)
               initialization_only = .false.
               if (newdat .eq. 1 .and. sync1 .gt. -99.0 .and. &
                   (sync1 .le. thresh1 .or. abs(noffset) .gt. ntol)) then
                  initialization_only = .true.
                  sync1 = thresh1 + 1.0
                  noffset = 0
               endif
               ! Computed once per candidate, before the "keep only best
               ! within ftol" collapse logic just below -- see the SKIPPED
               ! branch right after for why the ordering matters. Must be
               ! false whenever initialization_only is true: that probe
               ! doesn't accumulate anything regardless, but still needs
               ! decode1a() called once per sweep for its own internal
               ! state, so it must not be intercepted here.
               already_decoded_nearby = (.not. initialization_only) .and. &
                    any(ljt65decoded(max(1,i-JT65_LOCAL_BINS):min(nfft_active,i+JT65_LOCAL_BINS)))

               if (sync1 .gt. thresh1 .and. abs(noffset) .le. ntol) then
                  if (already_decoded_nearby) then
                     ! Skip a candidate whose bin (or a nearby one, within
                     ! JT65_LOCAL_BINS) already produced a real decode
                     ! earlier this same accumulation cycle, mirroring
                     ! Q65's ldecoded(ipk) (decodes_mod.f90) -- otherwise a
                     ! repeat automatic call re-discovers and re-emits the
                     ! same JT65 decode.
                     !
                     ! Must run before the "keep only best within ftol"
                     ! collapse logic below, not after: that logic's
                     ! "km=km-1" runs unconditionally, on the assumption
                     ! that accumulation always follows with a matching
                     ! "km=km+1" -- skipping AFTER that decrement leaves it
                     ! unmatched and silently drives km negative, hiding
                     ! the whole signal from this pass's output.
                     call dbg('map65a: JT65 i=' // itoa(i) // &
                              ' SKIPPED (already decoded nearby this cycle) at t=' // rtoa(sec_midn()))
                  else
!  Keep only the best candidate within ftol.
!  (Am I deleting any good decodes by doing this?)
              if(freq-freq0.le.ftol .and. sync1.gt.sync10 .and.       &
                   nkm.eq.1) km=km-1
                  if (freq - freq0 .gt. ftol .or. sync1 .gt. sync10) then
                     nflip = nint(flipk)
                     f00 = (i - 1)*df          !Freq of detected sync tone (0-96000 Hz)
                     ntry = ntry + 1
                     if ((nqd .eq. 1 .and. ntry .ge. 40) .or. &
                         (nqd .eq. 0 .and. ntry .ge. 400)) then
  ! Too many calls to decode1a for this pass -- stop scanning more
  ! frequency bins for JT65 candidates here, but only here: exit just this
  ! do i=ia,ib sweep. A "go to 900" used to jump all the way past the rest
  ! of this pass (including the wideband Q65 full-decode loop further
  ! below when nqd=0), all remaining nqd passes, and the post-loop
  ! cleanup/display() -- silently costing Q65 a whole minute even though
  ! the abort was tripped by JT65 candidate volume, not a Q65 problem.
                        call dbg('map65a: Signal too strong, decoding aborted, nqd=' // itoa(nqd) // &
                                 ' ntry=' // itoa(ntry) // ' i=' // itoa(i))
                        call write_stdout('! Signal too strong, or suspect data?  Decoding aborted.'//new_line('a'))
                        write (13, *) 'Signal too strong, or suspect data?  Decoding aborted.'
                        flush (13)
                        exit
                     endif

                     call timer('decode1a', 0)
                     ifreq = i
                     ikhz = nint(freq + 0.5*(nfa + nfb) - foffset) - nfshift
                     idf = nint(1000.0*(freq + 0.5*(nfa + nfb) - foffset - (ikHz + nfshift)))

                     call decode1a(dd, newdat, f00, nflip, merge(0,mode65,initialization_only), nfsample, &
                                   xpol, mycall, hiscall, hisgrid, neme, ndepth, nqd, dphi, &
                                   ndphi, nutc, ikHz, idf, ipol, ntol, sync2, &
                                   a, dt, pol, nkv, nhist, nsum, nsave, qual, decoded)
                     call timer('decode1a', 1)
                     ! 2026-09-12: paired with the ccf65 instrumentation above --
                     ! logs decode1a's OUTPUT dt (dt00+dtbest+1.7, so this
                     ! encodes dtbest even though dtbest itself isn't passed
                     ! back to this scope), the running real-candidate count
                     ! this pass (ntry, already used for the "signal too
                     ! strong" abort check), and elapsed wall-clock time, so a
                     ! live capture can show whether the anomaly correlates
                     ! with how many real candidates/how much time this pass
                     ! has already chewed through before reaching this one.
                     call dbg('map65a: decode1a call ' // itoa(ntry) // ' this pass at t=' // &
                              rtoa(sec_midn()) // ' nqd=' // itoa(nqd) // ' i=' // itoa(i) // &
                              ' f00=' // rtoa(real(f00)) // ' dt_out=' // rtoa(dt) // &
                              ' initialization_only=' // itoa(merge(1,0,initialization_only)) // &
                              ' decoded="' // trim(decoded) // '"')

                     call dbg('map65a: decode1a result at t=' // rtoa(sec_midn()) // &
                              ' nqd=' // itoa(nqd) // ' i=' // itoa(i) // ' freq=' // rtoa(freq) // &
                              ' sync1=' // rtoa(sync1) // ' initialization_only=' // itoa(merge(1,0,initialization_only)) // &
                              ' decoded="' // trim(decoded) // '"')

                     if (mode65 .ne. 0 .and. .not. initialization_only) then
                        if (km .lt. MAXMSG) km = km + 1
                        sig(km, 1) = nfile
                        sig(km, 2) = nutc
                        sig(km, 3) = freq + 0.5*(nfa + nfb)
                        sig(km, 4) = sync1
                        sig(km, 5) = dt
                        sig(km, 6) = pol
                        sig(km, 7) = flipk
                        sig(km, 8) = sync2
                        sig(km, 9) = nkv
                        sig(km, 10) = qual
!                    sig(km,11)=idphi
                        sig(km, 12) = savg_dec(ipol, i)
                        sig(km, 13) = a(1)
                        sig(km, 14) = a(2)
                        sig(km, 15) = a(3)
                        sig(km, 16) = a(4)
!                     sig(km,17)=a(5)
                        sig(km, 18) = nhist
                        msg(km) = decoded
                        freq0 = freq
                        sync10 = sync1
                        nkm = 1
                        ! The lookup supplies the neighborhood radius;
                        ! marking it here too would double the exclusion.
                        if (decoded .ne. '                      ') &
                           ljt65decoded(i) = .true.
                     endif
                  endif
                  endif
               endif
            endif
         enddo  !i=ia,ib

         call dbg('map65a: JT65 sweep done at t=' // rtoa(sec_midn()) // &
                  ' nqd=' // itoa(nqd) // ' km_exiting=' // itoa(km) // ' ntry=' // itoa(ntry))

         if (nqd .eq. 1) then
            nwrite = 0
            if (mode65 .eq. 0) km = 0
                        
            do k = 1, km
               decoded = msg(k)
               if (decoded .ne. '                      ') then
                  nutc = sig(k, 2)
                  freq = sig(k, 3)
                  sync1 = sig(k, 4)
                  dt = sig(k, 5)
                  npol = nint(57.2957795*sig(k, 6))
                  flip = sig(k, 7)
                  sync2 = sig(k, 8)
                  nkv = sig(k, 9)
                  nqual = sig(k, 10)
!              idphi=nint(sig(k,11))
                  if (flip .lt. 0.0) then
                     do i = 22, 1, -1
                        if (decoded(i:i) .ne. ' ') go to 8
                     enddo
                     stop 'Error in message format'
8                    if (i .le. 18) decoded(i + 2:i + 4) = 'OOO'
                  endif
                  nkHz = nint(freq - foffset) - nfshift
                  mhz = fcenter                         ! ... +fadd ???
                  f0 = mhz + 0.001*nkHz
                  ndf = nint(1000.0*(freq - foffset - (nkHz + nfshift)))
                  nsync1 = sync1
                  s2db = 10.0*log10(sync2) - 40             !### empirical ###
                  nsync2 = nint(s2db)
                  if (decoded(1:4) .eq. 'RO  ' .or. decoded(1:4) .eq. 'RRR  ' .or. &
                      decoded(1:4) .eq. '73  ') then
                     nsync2 = nint(1.33*s2db + 2.0)
                  endif

                  nwrite = nwrite + 1
                  if (nxant .ne. 0) then
                     npol = npol - 45
                     if (npol .lt. 0) npol = npol + 180
                  endif

                  call txpol(xpol, decoded, mygrid, npol, nxant, ntxpol, cp)
                  
                  if (ndphi .eq. 0) then
                     write (line, '("!",I3,I5,I4,I6.4,F5.1,I5,1X,A1,1X,A22,I2,I5,I5,1X,A1)') &
                        nkHz, ndf, npol, nutc, dt, nsync2, cm, decoded, nkv, nqual, ntxpol, cp
                     call write_stdout(trim(line)//new_line('a'))
                  else
                     if (iloop .ge. 1) qphi(iloop) = sig(k, 10)

                     write (line, '("!",I3,I5,I4,I6.4,F5.1,I5,1X,A1,1X,A22,I2,I5,I5,1X,A1)') &
                        nkHz, ndf, npol, nutc, dt, nsync2, cm, decoded, nkv, nqual, 30*iloop, '-'
                     call write_stdout(trim(line)//new_line('a'))
                     write (27, 1011) 30*iloop, nkHz, ndf, npol, nutc, &
                        dt, sync2, nkv, nqual, cm, decoded
1011                 format(i3, i4, i5, i4, i6.4, 1x, f5.1, f7.1, i3, i5, a1, 1x, a22)
                  endif
               endif
            enddo  ! k=1,km

            if (bq65) then
               q65b_called = .false.
               do icand = 1, ncand
                  if (cand(icand)%iflip .ne. 0) cycle        !Keep only Q65 candidates
                  freq = cand(icand)%f + nkhz_center - real(nrate_active)/2000.0 - 1.27046
                  nhzdiff = nint(1000.0*(freq - mousefqso) - mousedf) - nfcal
! Now looking for "quick decode" (nqd=1) candidates at cursor freq +/- ntol.
                  if (nqd .eq. 1 .and. abs(nhzdiff) .gt. ntol) cycle
                  ikhz = mousefqso
                  q65b_called = .true.
                  f0 = cand(icand)%f
                  call timer('q65b    ', 0)                  

                  call q65b(nutc, nqd, nxant, fcenter, nfcal, nfsample, ikhz, mousedf, &
                           ntol, xpol, mycall, mygrid, hiscall, hisgrid, mode_q65, f0, fqso, &
                           newdat, nagain, max_drift, ndop00, idec)

                  call timer('q65b    ', 1)

                  ! idec is not a trustworthy success flag here -- q65b
                  ! derives it from cq0(2:2), which is not reset on a
                  ! failed/no-op attempt, so a later candidate in this same
                  ! loop can falsely read back an earlier candidate's
                  ! leftover cq0 digit. nsnr0 is reset to -99 at the top of
                  ! every q65b() call (q65b.F90) and can't carry state
                  ! across candidates this way.
                  if (nsnr0 .gt. -99) candec(icand) = .true.
               enddo
               if (.not. q65b_called) then
                  freq = mousefqso + 0.001*mousedf
                  ikhz = mousefqso
                  f0 = freq - (nkhz_center - real(nrate_active)/2000.0 - 1.27046)
                  call timer('q65b    ', 0)
                 
                  call q65b(nutc, nqd, nxant, fcenter, nfcal, nfsample, ikhz, mousedf, &
                        ntol, xpol, mycall, mygrid, hiscall, hisgrid, mode_q65, f0, fqso, &
                        newdat, nagain, max_drift, ndop00, idec)

                  call timer('q65b    ', 1)
               endif
            endif

            if (nwrite .eq. 0 .and. nwrite_q65 .eq. 0) then
               write (line, '("!",I3,9X,I6.4,"  ")') mousefqso, nutc
               call write_stdout(trim(line)//new_line('a'))
            endif
         endif  !nqd.eq.1

         if (ndphi .eq. 1 .and. iloop .lt. 12) then
            iloop = iloop + 1
            go to 2
         endif

         if (ndphi .eq. 1 .and. iloop .eq. 12 .and. nqd .eq. 1) call getdphi(qphi)
         if (nqd .eq. 1) then
            call sec0(1, tdec)
            write (line, '("<QuickDecodeDone>",3I4,I6,F6.2)') &
               nsum, nsave, nstandalone, nhsym, tdec

            call write_stdout(trim(line)//new_line('a'))

            open (16, file='tquick.dat', status='unknown', position='append')
            write (16, 1016) nutc, tdec
1016        format(i4.4, f7.1)
            close (16)
         endif
         call sec0(1, tsec0)
         call dbg('map65a: end of nqd=' // itoa(nqd) // ' body at t=' // rtoa(sec_midn()) // &
                  ' tsec0=' // rtoa(tsec0) // ' nhsym=' // itoa(nhsym) // ' nagain=' // itoa(nagain))
         if (nhsym .eq. nhsym1 .and. tsec0 .gt. 3.0) then
            call dbg('map65a: ABORT (early-pass 3s budget) at t=' // rtoa(sec_midn()) // &
                     ' tsec0=' // rtoa(tsec0) // ' -- nqd=0 will NOT run this pass')
            go to 700
         endif
         if (nqd .eq. 1 .and. nagain .eq. 1) then
            call dbg('map65a: SKIP nqd=0 (nagain=1, manual repeat) at t=' // rtoa(sec_midn()))
            go to 900
         endif

         if (nqd .eq. 0 .and. bq65) then
! Do the wideband Q65 decode
            do icand = 1, ncand
               if (cand(icand)%iflip .ne. 0) cycle    !Do only Q65 candidates here
               if (candec(icand)) cycle             !Skip if already decoded
               freq = cand(icand)%f + nkhz_center - real(nrate_active)/2000.0 - 1.27046
!###! If here at nqd=1, do only candidates at mousefqso +/- ntol
!###           if(nqd.eq.1 .and. abs(freq-mousefqso).gt.0.001*ntol) cycle
               ikhz = nint(freq)
               f0 = cand(icand)%f
               call timer('q65b    ', 0)

               call q65b(nutc, nqd, nxant, fcenter, nfcal, nfsample, ikhz, mousedf, &
                        ntol, xpol, mycall, mygrid, hiscall, hisgrid, mode_q65, f0, fqso, &
                        newdat, nagain, max_drift, ndop00, idec)

               call timer('q65b    ', 1)

               ! See the matching note on the nqd==1 candidate loop above --
               ! idec is unreliable across back-to-back q65b() calls in the
               ! same loop; use nsnr0 instead.
               if (nsnr0 .gt. -99) candec(icand) = .true.
               if (abort_decode) go to 700
            enddo  ! icand
         endif         
         call sec0(1, tsec0)

         call system_clock(t_now, t_rate)
         if (real(t_now - t_start)/real(t_rate) > 40.0) then
            call dbg('Decode abort: exceeded 40 seconds at end of do nqd = 1, 0, -1 pass, nqd=' // itoa(nqd))
            abort_decode = .true.
            go to 700
         endif

      enddo  ! nqd

700   continue
      ! TEMP diagnostic 2026-09-09 for the missing-other-signals-in-Messages investigation.
      call dbg('map65a: reached label 700 at t=' // rtoa(sec_midn()) // &
               ' km=' // itoa(km) // ' abort_decode=' // itoa(merge(1,0,abort_decode)) // &
               ' nhsym=' // itoa(nhsym))
      call select_unique_decodes(sig, msg, km, ftol, RESULT_DT_TOLERANCE, indx, nz)
      call dbg('map65a: select_unique_decodes done at t=' // rtoa(sec_midn()) // &
               ' km=' // itoa(km) // ' nz=' // itoa(nz))

      do n = 1, nz
         i = indx(n)
         if (i .ge. 1) then
               nutc = sig(i, 2)
               freq = sig(i, 3)
               sync1 = sig(i, 4)
               dt = sig(i, 5)
               npol = nint(57.2957795*sig(i, 6))
               flip = sig(i, 7)
               sync2 = sig(i, 8)
               nkv = sig(i, 9)
               nqual = min(sig(i, 10), 10.0)
!                  rms0=sig(i,11)
               do k = 1, 5
                  a(k) = sig(i, 12 + k)
               enddo
               nhist = sig(i, 18)
               decoded = msg(i)

               if (flip .lt. 0.0) then
                  do i = 22, 1, -1
                     if (decoded(i:i) .ne. ' ') go to 10
                  enddo
                  stop 'Error in message format'
10                if (i .le. 18) decoded(i + 2:i + 4) = 'OOO'
               endif
               mhz = fcenter                             !... +fadd ???
               nkHz = nint(freq - foffset) - nfshift
               f0 = mhz + 0.001*nkHz
               ndf = nint(1000.0*(freq - foffset - (nkHz + nfshift)))
               ndf0 = nint(a(1))
               ndf1 = nint(a(2))
               ndf2 = nint(a(3))
               nsync1 = sync1

               s2db = 10.0*log10(sync2) - 40             !### empirical ###
               nsync2 = nint(s2db)
               if (decoded(1:4) .eq. 'RO  ' .or. decoded(1:4) .eq. 'RRR  ' .or. &
                   decoded(1:4) .eq. '73  ') then
                  nsync2 = nint(1.33*s2db + 2.0)
               endif

               if (nxant .ne. 0) then
                  npol = npol - 45
                  if (npol .lt. 0) npol = npol + 180
               endif
               
               call txpol(xpol, decoded, mygrid, npol, nxant, ntxpol, cp)
               
               cmode = '#A'
               if (mode65 .eq. 2) cmode = '#B'
               if (mode65 .eq. 4) cmode = '#C'
               write (26, 1014) f0, ndf, ndf0, ndf1, ndf2, dt, npol, nsync1, &
                  nsync2, nutc, decoded, '#', cp, cmode ! was decoded,cp,
1014           format(f8.3, i5, 3i3, f5.1, i4, i3, i4, i5.4, 4x, a22, 7x, 2a1, 2x, a2) ! was a22,2x,a1,3x,a2
               ndecodes = ndecodes + 1
               write (21, 1100) f0, ndf, dt, npol, nsync2, nutc, decoded, '#', cp, &
                  cmode(1:1), cmode(2:2)! was decoded,cp,
1100           format(f8.3, i5, f5.1, 2i4, i5.4, 2x, a22, 7x, 2a1, 3x, a1, 1x, a1) ! was a22,2x,a1,1x,a1
         endif
      enddo

      write (26, 1015) nutc
1015  format(37x, i6.4, ' ')
      flush (21)
      flush (26)
      call display(nkeep, ftol)
      ndecdone = 2

900   close (23)
      flush (12)
      ndphi = 0
      mcall3b = mcall3a

      return
   end subroutine map65a

end module map65a_mod
