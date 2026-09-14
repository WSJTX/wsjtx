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

      ! nhsym (npar_ptrs_mod) is written by the GUI/audio thread's automatic
      ! per-minute trigger with no locking and can change mid-call; snapshot
      ! it here and use nhsym0, not the live nhsym, for every decision made
      ! in this call and everything passed down into map65a().
      nhsym0 = nhsym

      ! newdat is also consumed internally by filbig() (see filbig.f90) as a
      ! one-shot "rebuild the cached FFT" flag. Give this call its own
      ! private copy, same idea as nhsym0 above, so that internal reset
      ! can't race with a genuinely new trigger landing on the shared
      ! newdat from the other thread. Only write the real, shared newdat at
      ! the two explicit completion points below.
      newdat0 = newdat

      ! TEMP diagnostic 2026-09-10 for the missing-final-pass / incomplete-
      ! data-set investigation.
      call dbg('decode0: ENTRY at t=' // rtoa(sec_midn()) // &
               ' nhsym0=' // itoa(nhsym0) // ' newdat0=' // itoa(newdat0) // &
               ' manualDecodeFlag=' // itoa(manualDecodeFlag) // &
               ' nagain=' // itoa(nagain) // ' ndiskdat=' // itoa(ndiskdat))

      ! newdat is forced to 1 by decode() for every call (manual or
      ! automatic); it does not by itself mean "the live buffers just
      ! finished a fresh accumulation." nagain=1 marks a manual repeat
      ! (Decode button / Find Delta Phi), which must not refresh dd_old
      ! from a live buffer that may still be mid-fill.
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

      ! Seed mycall0/hiscall0/hisgrid0/neme0 from the real values on the
      ! first call instead of blank sentinels, so the mismatch check below
      ! only fires on a genuine later change, not on startup.
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
         ! Clear the shared newdat here too, not just at final/manual
         ! completion below -- filbig() only ever consumes the private
         ! newdat0 copy above, so without this the shared flag stays set
         ! for the whole gap until the final trigger, and run_m65's poll
         ! loop keeps re-firing this same early pass in a tight loop.
         newdat = 0
      end if

      ! A manual repeat decode (Decode button / Find Delta Phi) is always a
      ! single, complete request regardless of nhsym, which only reflects
      ! the automatic per-minute cycle and may never equal nhsym2 if no
      ! automatic decode has run yet this session.
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
