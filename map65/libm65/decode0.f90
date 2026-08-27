module decode0_mod
   implicit none
contains

   subroutine decode0(nstandalone) bind(C, name='decode0_')

      use iso_c_binding, only: c_int
      use timer_module, only: timer
      use npar_ptrs_mod
      use datcom_ptrs_mod, only: NFFT, dd, dd_old, dd_use, ss, ss_old, savg, savg_old, ss_use, savg_use
      use debug_log, only: dbg, itoa
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
      character(len=128) :: line
      character mycall0*12, hiscall0*12, hisgrid0*6

      data neme0/-99/, mcall3b/1/, mycall0/'            '/, hiscall0/'            '/, hisgrid0/'      '/

      save

      if (newdat /= 0 .and. manualDecodeFlag == 0) then
         dd_old   = dd
         ss_old   = ss
         savg_old = savg
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

      if (newdat .ne. 0 .or. manualDecodeFlag /= 0) then
         nz = int( real(nrate_active, kind=real64) * nhsym / 5.3833_real64 )
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

      if (mycall .ne. mycall0 .or. hiscall .ne. hiscall0 .or. &
          hisgrid .ne. hisgrid0 .or. mcall3 .ne. 0 .or. neme .ne. neme0) mcall3b = 1

      mycall0 = mycall
      hiscall0 = hiscall
      hisgrid0 = hisgrid
      neme0 = neme

      ! Manual decode: force a single nhsym2-style cycle and always emit DecodeFinished
      if (manualDecodeFlag /= 0) then
         nhsym = nhsym2

         call timer('map65a  ', 0)
         call map65a(dd_use, newdat, nutc, fcenter, ntol, idphi, nfa, nfb, &
                     mousedf, mousefqso, nagain, ndecdone, nfshift, ndphi, max_drift, &
                     nfcal, nkeep, mcall3b, nsum, nsave, nxant, mycall, mygrid, &
                     neme, ndepth, nstandalone, hiscall, hisgrid, nhsym, nfsample, &
                     ndiskdat, nxpol, nmode, ndop00)
         call timer('map65a  ', 1)
         call timer('decode0 ', 1)

         call sec0(1, tdec)

         write (line, '("<DecodeFinished>",3I4,I6,F6.2,I5)') &
            nsum, nsave, nstandalone, nhsym, tdec, ndecodes
         call write_stdout(trim(line)//new_line('a'))
         newdat = 0
         manualDecodeFlag = 0   ! <<< add this

         return
      
      else

      ! Normal wideband path: nhsym1 (EarlyFinished) + nhsym2 (DecodeFinished)
      call timer('map65a  ', 0)
      call map65a(dd_use, newdat, nutc, fcenter, ntol, idphi, nfa, nfb, &
                  mousedf, mousefqso, nagain, ndecdone, nfshift, ndphi, max_drift, &
                  nfcal, nkeep, mcall3b, nsum, nsave, nxant, mycall, mygrid, &
                  neme, ndepth, nstandalone, hiscall, hisgrid, nhsym, nfsample, &
                  ndiskdat, nxpol, nmode, ndop00)
      call timer('map65a  ', 1)
      call timer('decode0 ', 1)

      call sec0(1, tdec)

      if (nhsym == nhsym1) then
         write (line, '("<EarlyFinished>",3I4,I6,F6.2)') &
            nsum, nsave, nstandalone, nhsym, tdec
         call write_stdout(trim(line)//new_line('a'))
      end if

      if (nhsym == nhsym2) then
         write (line, '("<DecodeFinished>",3I4,I6,F6.2,I5)') &
            nsum, nsave, nstandalone, nhsym, tdec, ndecodes
         call write_stdout(trim(line)//new_line('a'))
         newdat = 0  !change 20260723
      end if

      return
   end if
   end subroutine decode0

end module decode0_mod