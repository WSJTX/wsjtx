module decode0_mod
   implicit none
contains

   subroutine decode0(nstandalone) bind(C, name='decode0_')

      use iso_c_binding, only: c_int
      use timer_module, only: timer
      use npar_ptrs_mod
      use datcom_ptrs_mod, only: NFFT, dd, dd_old, dd_use, ss, ss_old, savg, savg_old, ss_use, savg_use
      use debug_log, only: dbg, itoa, rtoa
      use sec_midn_mod, only: sec_midn
      use map65a_mod
      use stdout_channel_mod, only: write_stdout
      use decodes_mod, only: ndecodes, nhsym1, nhsym2
      use sec0_mod, only:sec0

      implicit none

      integer(c_int), intent(in) :: nstandalone
      integer hist(0:32768)
      integer i, j1, j2, j3, j4, m, mcall3b, ndphi, neme0, nsum
      integer :: ndecdone
      real :: tdec, tquick, rmsdd
      integer nz
      integer :: nhsym0
      integer :: newdat0
      character(len=128) :: line
      character mycall0*12, hiscall0*12, hisgrid0*6
      logical :: first_mcall3b_check

      data neme0/-99/, mcall3b/0/, mycall0/'            '/, hiscall0/'            '/, hisgrid0/'      '/
      data first_mcall3b_check/.true./

      save

      ! 2026-09-09: nhsym is a plain module variable (npar_ptrs_mod) written
      ! directly by the GUI/audio thread's automatic per-minute trigger and
      ! read directly by this decoder-thread call, with no locking. A single
      ! decode0()/map65a() call can run for several seconds (e.g. a slow Q65
      ! decode attempt), long enough for the NEXT automatic trigger to fire
      ! and overwrite nhsym out from under this still-running call. Without
      ! a private snapshot, this call's own later logic (the nhsym1/nhsym2
      ! completion checks below, and map65a's identical checks -- nhsym is
      ! passed to map65a by reference and was the SAME live memory) can see
      ! a different value than it started with, causing an early pass to
      ! mistake itself for the final pass, prematurely clear newdat, and
      ! silently consume the real final pass's trigger -- so the final pass
      ! never runs as its own independent call at all. Snapshot nhsym here,
      ! as the first thing this call does, and use nhsym0 (not nhsym) for
      ! every decision and every value passed to map65a() from this point on.
      nhsym0 = nhsym

      ! 2026-09-10: newdat is ALSO consumed internally by filbig() (see
      ! filbig.f90) as a one-shot "rebuild the cached big FFT" flag -- the
      ! FIRST decode1a() call in a JT65 sweep sees newdat/=0, rebuilds the
      ! (expensive) big FFT once, and sets the shared newdat back to 0 so
      ! later candidates in the SAME sweep reuse the cache instead of
      ! recomputing it. But that reset hits the exact same module variable
      ! run_m65's trigger loop and the GUI's automatic per-minute trigger
      ! depend on. If the NEXT automatic trigger's own setNewdat(1) lands
      ! (from the other thread) before this call's first filbig() call
      ! consumes it, that incoming trigger gets silently erased -- run_m65
      ! never fires again until some LATER, unrelated trigger happens to
      ! come along (observed: a "final" pass trigger swallowed this way,
      ! with nothing running again until the NEXT MINUTE's own early-pass
      ! trigger, 40+ seconds later). Give this call its own private copy,
      ! same idea as nhsym0 above: use newdat0 for every internal decision
      ! and everything passed down into map65a() (and therefore decode1a()/
      ! filbig()), and only ever write to the real, shared newdat at the
      ! two explicit completion points below, exactly as before.
      newdat0 = newdat

      ! TEMP diagnostic 2026-09-10 for the missing-final-pass / incomplete-
      ! data-set investigation.
      call dbg('decode0: ENTRY at t=' // rtoa(sec_midn()) // &
               ' nhsym0=' // itoa(nhsym0) // ' newdat0=' // itoa(newdat0) // &
               ' manualDecodeFlag=' // itoa(manualDecodeFlag) // &
               ' nagain=' // itoa(nagain) // ' ndiskdat=' // itoa(ndiskdat))

      ! 2026-09-08: added ".and. nagain == 0" -- newdat is forced to 1 by
      ! decode() for EVERY call (manual or automatic) because run_m65's own
      ! trigger loop requires newdat/=0 to fire a decode at all; it does NOT
      ! mean "the live buffers just finished a fresh accumulation." A manual
      ! repeat decode (Decode button / Find Delta Phi) sets nagain=1 for
      ! exactly this case (legacy: "nagain=1 ==> decode only at fQSO +/-
      ! Tol") -- use that to tell a real fresh-data cycle (nagain=0, set by
      ! the automatic dataSink trigger) apart from a manual repeat, instead
      ! of refreshing dd_old from a live buffer that may still be mid-fill.
      if (newdat0 /= 0 .and. manualDecodeFlag == 0 .and. nagain == 0) then
         dd_old   = dd
         ss_old   = ss
         savg_old = savg
         call dbg('decode0: dd_old REFRESHED from live dd at t=' // rtoa(sec_midn()) // ' nhsym0=' // itoa(nhsym0))
      else
         call dbg('decode0: dd_old NOT refreshed (reusing prior snapshot) at t=' // rtoa(sec_midn()) // &
                  ' nhsym0=' // itoa(nhsym0) // ' nagain=' // itoa(nagain))
      endif

      nkeep = 20

      call sec0(0, tquick)
      call timer('decode0 ', 0)
      
      ! Always decode from the snapshot (dd_old/ss_old/savg_old) taken above,
      ! never the live dd/ss/savg. The nhsym2 pass routinely runs past the
      ! minute boundary (several seconds), and symspec_() on the GUI thread
      ! resets/overwrites the live buffers for the next cycle unconditionally
      ! as soon as new UDP data arrives -- with no lock between the two
      ! threads. Without this, a still-running decode reads a mix of this
      ! cycle's and next cycle's data for candidates it hasn't reached yet
      ! in its frequency sweep.
      dd_use   => dd_old
      ss_use   => ss_old
      savg_use => savg_old

      if (newdat0 .ne. 0 .or. manualDecodeFlag /= 0) then
         nz = int( real(nrate_active, kind=real64) * nhsym0 / 5.3833_real64 )
         hist = 0
         do i = 1, nz
            j1 = int(min(abs(dd_use(1, i)), 32768.0))
            hist(j1) = hist(j1) + 1
            j2 = int(min(abs(dd_use(2, i)), 32768.0))
            hist(j2) = hist(j2) + 1
            j3 = int(min(abs(dd_use(3, i)), 32768.0))
            hist(j3) = hist(j3) + 1
            j4 = int(min(abs(dd_use(4, i)), 32768.0))
            hist(j4) = hist(j4) + 1
         enddo
         m = 0
         do i = 0, 32768
            m = m + hist(i)
            if (m .ge. 2*nz) go to 10
         enddo
10       rmsdd = 1.5*i
      endif
      ndphi = 0
      if (iand(nrxlog, 8) .ne. 0) ndphi = 1

      ! 2026-09-10: mcall3b used to default to 1 (forcing a rebuild) AND
      ! mycall0/hiscall0/hisgrid0 started blank, guaranteeing the mismatch
      ! check below fired "changed" on the very first call regardless --
      ! together they made sure the very first decode always rebuilt the
      ! deep65 CALL3.TXT candidate list. That's now redundant: run_m65.f90
      ! builds it once, eagerly, before the decode loop ever starts (see
      ! build_call3_candidates() in deep65.f90). Left as-is, this first-call
      ! forced mismatch was clobbering that already-built cache with a
      ! second, needless ~3.5s rebuild on the very first live decode --
      ! exactly the real-time-audio-starving cost the eager build was meant
      ! to move out of the way. Seed mycall0/hiscall0/hisgrid0/neme0 from
      ! the real values on the first call instead of comparing against
      ! blank sentinels, so this only fires on a GENUINE later change.
      if (first_mcall3b_check) then
         mycall0 = mycall
         hiscall0 = hiscall
         hisgrid0 = hisgrid
         neme0 = neme
         first_mcall3b_check = .false.
      endif

      if (mycall .ne. mycall0 .or. hiscall .ne. hiscall0 .or. &
          hisgrid .ne. hisgrid0 .or. mcall3 .ne. 0 .or. neme .ne. neme0) mcall3b = 1

      mycall0 = mycall
      hiscall0 = hiscall
      hisgrid0 = hisgrid
      neme0 = neme

      ! Manual decode: force a single nhsym2-style cycle and always emit DecodeFinished
      if (manualDecodeFlag /= 0) then
         nhsym0 = nhsym2
         call announce_decode_pass('manual')

         call timer('map65a  ', 0)
         call map65a(dd_use, newdat0, nutc, fcenter, ntol, idphi, nfa, nfb, &
                     mousedf, mousefqso, nagain, ndecdone, nfshift, ndphi, max_drift, &
                     nfcal, nkeep, mcall3b, nsum, nsave, nxant, mycall, mygrid, &
                     neme, ndepth, nstandalone, hiscall, hisgrid, nhsym0, nfsample, &
                     ndiskdat, nxpol, nmode, ndop00)
         call timer('map65a  ', 1)
         call timer('decode0 ', 1)

         call sec0(1, tdec)

         write (line, '("<DecodeFinished>",3I4,I6,F6.2,I5)') &
            nsum, nsave, nstandalone, nhsym0, tdec, ndecodes
         call write_stdout(trim(line)//new_line('a'))
         newdat = 0
         manualDecodeFlag = 0   ! <<< add this

         call dbg('decode0: MANUAL path RETURNING at t=' // rtoa(sec_midn()) // ' newdat now 0')

         return

      else

      ! Normal wideband path: nhsym1 (EarlyFinished) + nhsym2 (DecodeFinished)
      if (ndiskdat /= 0) then
         call announce_decode_pass('disk')
      else if (nhsym0 == nhsym1) then
         call announce_decode_pass('early')
      else if (nhsym0 == nhsym2) then
         call announce_decode_pass('final')
      else
         call announce_decode_pass('unknown')
      endif
      call timer('map65a  ', 0)
      call map65a(dd_use, newdat0, nutc, fcenter, ntol, idphi, nfa, nfb, &
                  mousedf, mousefqso, nagain, ndecdone, nfshift, ndphi, max_drift, &
                  nfcal, nkeep, mcall3b, nsum, nsave, nxant, mycall, mygrid, &
                  neme, ndepth, nstandalone, hiscall, hisgrid, nhsym0, nfsample, &
                  ndiskdat, nxpol, nmode, ndop00)
      call timer('map65a  ', 1)
      call timer('decode0 ', 1)

      call sec0(1, tdec)

      ! TEMP diagnostic 2026-09-10: proves the snapshot fix by comparing the
      ! private nhsym0 this call has used throughout against whatever the
      ! live, shared nhsym reads right now -- if they differ, another
      ! automatic trigger overwrote nhsym while this call was still running,
      ! exactly the race this fix protects against.
      if (nhsym0 /= nhsym) then
         call dbg('decode0: nhsym RACE DETECTED (protected by snapshot) -- ' // &
                  'nhsym0(used)=' // itoa(nhsym0) // ' live nhsym now=' // itoa(nhsym))
      endif

      if (nhsym0 == nhsym1) then
         write (line, '("<EarlyFinished>",3I4,I6,F6.2)') &
            nsum, nsave, nstandalone, nhsym0, tdec
         call write_stdout(trim(line)//new_line('a'))
      end if

      ! 2026-09-09: added ".or. nagain /= 0". A manual repeat decode
      ! (Decode button / Find Delta Phi) is always a single, complete
      ! request from the GUI's point of view, regardless of what nhsym
      ! happens to be -- nhsym only reflects the automatic per-minute
      ! accumulation cycle and, if no automatic decode has ever run yet
      ! this session, is stuck at its startup default and can never equal
      ! nhsym2. Without this, such a manual click never clears newdat
      ! (leaving run_m65's trigger loop to re-fire m65a() every ~50ms
      ! forever) and never sends the GUI <DecodeFinished> (leaving
      ! m_decoderBusy/the Decode button stuck on) -- see the
      ! manualDecodeFlag branch above, which already always does both.
      if (nhsym0 == nhsym2 .or. nagain /= 0) then
         write (line, '("<DecodeFinished>",3I4,I6,F6.2,I5)') &
            nsum, nsave, nstandalone, nhsym0, tdec, ndecodes
         call write_stdout(trim(line)//new_line('a'))
         newdat = 0  !change 20260723
      end if

      call dbg('decode0: WIDEBAND path RETURNING at t=' // rtoa(sec_midn()) // &
               ' nhsym0=' // itoa(nhsym0) // ' newdat0(consumed by filbig)=' // itoa(newdat0) // &
               ' live_newdat(real, shared)=' // itoa(newdat) // ' decoder_ready=' // itoa(decoder_ready))

      return
   end if
   end subroutine decode0

   subroutine announce_decode_pass(pass_name)
      use stdout_channel_mod, only: write_stdout
      implicit none

      character(len=*), intent(in) :: pass_name

      call write_stdout('<Map65DecodePass> '//trim(pass_name)//new_line('a'))
   end subroutine announce_decode_pass

end module decode0_mod
