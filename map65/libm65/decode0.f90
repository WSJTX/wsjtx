module decode0_mod
   implicit none
contains

   subroutine decode0(nstandalone) bind(C, name='decode0_')

      use iso_c_binding, only: c_int
      use timer_module, only: timer
      use npar_ptrs_mod
      use datcom_ptrs_mod, only: NFFT, dd
      use debug_log
      use map65a_mod
      use stdout_channel_mod, only: write_stdout
      use decodes_mod, only: ndecodes, nhsym1, nhsym2
      use sec0_mod, only:sec0

      implicit none

      integer, parameter :: NSMAX = 60*96000
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

      nkeep = 20

      call sec0(0, tquick)
      call timer('decode0 ', 0)
      if (newdat .ne. 0) then
         nz = int(96000.0*nhsym/5.3833)
         hist = 0
         do i = 1, nz
            j1 = int(min(abs(dd(1, i)), 32768.0))
            hist(j1) = hist(j1) + 1
            j2 = int(min(abs(dd(2, i)), 32768.0))
            hist(j2) = hist(j2) + 1
            j3 = int(min(abs(dd(3, i)), 32768.0))
            hist(j3) = hist(j3) + 1
            j4 = int(min(abs(dd(4, i)), 32768.0))
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

      call timer('map65a  ', 0)
      call map65a(dd, newdat, nutc, fcenter, ntol, idphi, nfa, nfb, &
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
      end if
!      print *, 'nhsym is: ',nhsym,' nhsym1 is: ',nhsym1,' nhsym2 is: ',nhsym2
      return
   end subroutine decode0

end module decode0_mod
