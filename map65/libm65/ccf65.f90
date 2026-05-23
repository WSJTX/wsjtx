module ccf65_mod
   implicit none

contains

subroutine ccf65(ss_plane, nhsym, ssmax, sync1, ipol1, jpz, dt1, flipk, &
                 syncshort, snr2, ipol2, dt2)

      use debug_log
      use pctile_mod
      use four2a_mod
      implicit none

      integer, parameter :: NFFT = 512, NH = NFFT/2

      ! Arguments
      real, intent(in)    :: ss_plane(4, 322)                   ! Input: half-symbol powers, 4 pol'ns
      integer, intent(in) :: nhsym, jpz
      real, intent(in)    :: ssmax
      real, intent(out)   :: sync1, dt1, flipk, syncshort, snr2, dt2
      integer, intent(out):: ipol1, ipol2

      ! Working arrays (per call)
  real s(NFFT)                     !CCF = ss*pr
      complex cs(NFFT)                    ! Complex FT of s
  real s2(NFFT)                    !CCF = ss*pr2
      complex cs2(NFFT)                   ! Complex FT of s2
  real tmp1(322)
  real ccf(-11:54,4)

      ! Sync templates (persist across calls)
      logical, save :: first = .true.
      integer       :: npr(126)
      real, save :: pr(NFFT)           ! JT65 pseudo-random sync pattern
      real, save :: pr2(NFFT)          ! JT65 shorthand pattern
      complex, save :: cpr(NFFT)          ! Complex FT of pr
      complex, save :: cpr2(NFFT)         ! Complex FT of pr2

      ! Locals
      integer ip, i, j, k, lag, lagpk, lagpk2
      real base, ccf2, ccfbest, ccfbest2
      real fac, rms, sq, sumccf

! The JT65 pseudo-random sync pattern:
  data npr/                                        &
      1,0,0,1,1,0,0,0,1,1,1,1,1,1,0,1,0,1,0,0,     &
      0,1,0,1,1,0,0,1,0,0,0,1,1,1,0,0,1,1,1,1,     &
      0,1,1,0,1,1,1,1,0,0,0,1,1,0,1,0,1,0,1,1,     &
      0,0,1,1,0,1,0,1,0,1,0,0,1,0,0,0,0,0,0,1,     &
      1,0,0,0,0,0,0,0,1,1,0,1,0,0,1,0,1,1,0,1,     &
      0,1,0,1,0,0,1,1,0,0,1,0,0,1,0,0,0,0,1,1,     &
      1,1,1,1,1,1/

      !---------------------------------------------------------------
      ! One-time initialization of sync templates
      !---------------------------------------------------------------
  if(first) then
     fac=1.0/NFFT
  
         pr = 0.0
         pr2 = 0.0

     do i=1,NFFT
        k=2*mod((i-1)/8,2)-1
            if (i <= NH) pr2(i) = fac*k
     enddo

     do i=1,126
        j=2*i
        pr(j)=fac*(2*npr(i)-1)
     enddo

         ! Load real templates into complex arrays and FFT them
         do i = 1, NFFT
            cpr(i) = cmplx(pr(i), 0.0)
            cpr2(i) = cmplx(pr2(i), 0.0)
         enddo

     call four2a(cpr,NFFT,1,-1,0)
     call four2a(cpr2,NFFT,1,-1,0)

     first=.false.
  endif

      syncshort = 0.0
      snr2 = 0.0

      ccfbest = 0.0
      ccfbest2 = 0.0
  ipol1=1
  ipol2=1
      lagpk = 0
      lagpk2 = 0

      !---------------------------------------------------------------
      ! Loop over polarizations
      !---------------------------------------------------------------
      do ip = 1, jpz

         ! Build s from ss, with clipping
     do i=1,nhsym-1
            s(i) = min(ssmax, ss_plane(ip, i) + ss_plane(ip, i + 1))
     enddo
         s(nhsym:NFFT) = 0.0

         ! Remove local baseline
     call pctile(s,nhsym-1,50,base)
     s(1:nhsym-1)=s(1:nhsym-1)-base

         ! Forward FFT of s
         do i = 1, NFFT
            cs(i) = cmplx(s(i), 0.0)
         enddo
         call four2a(cs, NFFT, 1, -1, 0)

         ! Multiply by sync templates in frequency domain
         do i = 1, NFFT
            cs2(i) = cs(i)*conjg(cpr2(i))
            cs(i) = cs(i)*conjg(cpr(i))
         enddo

         ! Inverse FFT back to time domain
         call four2a(cs, NFFT, 1, 1, -1)
         call four2a(cs2, NFFT, 1, 1, -1)

         ! Extract real parts into s, s2
         do i = 1, NFFT
            s(i) = real(cs(i))
            s2(i) = real(cs2(i))
     enddo

         ! Find best JT65 sync
         do lag = -11, 54
        j=lag
            if (j < 1) j = j + NFFT
        ccf(lag,ip)=s(j)
            if (abs(ccf(lag, ip)) > ccfbest) then
           ccfbest=abs(ccf(lag,ip))
           lagpk=lag
           ipol1=ip
           flipk=1.0
               if (ccf(lag, ip) < 0.0) flipk = -1.0
        endif
     enddo
     
         ! Best shorthand
         do lag = -11, 54
        ccf2=s2(lag+28)
            if (ccf2 > ccfbest2) then
           ccfbest2=ccf2
           lagpk2=lag
           ipol2=ip
        endif
     enddo
     
      enddo  ! ip

      ! If no sync peak found, bail cleanly
      if (ccfbest == 0.0) then
         sync1 = -4.0
         dt1 = 0.0
         syncshort = 0.0
         snr2 = 0.0
         dt2 = 0.0
         return
      endif

      !---------------------------------------------------------------
      ! Baseline and RMS for normalization
      !---------------------------------------------------------------
      sumccf = 0.0
  do lag=-11,54
         if (abs(lag - lagpk) > 1) sumccf = sumccf + ccf(lag, ipol1)
  enddo
  base=sumccf/50.0

      sq = 0.0
  do lag=-11,54
         if (abs(lag - lagpk) > 1) sq = sq + (ccf(lag, ipol1) - base)**2
  enddo
  rms=sqrt(sq/49.0)

  sync1=-4.0
      if (rms > 0.0) sync1 = ccfbest/rms - 4.0
  dt1=lagpk*(2048.0/11025.0) - 2.5

      !---------------------------------------------------------------
      ! Shorthand SNR and timing
      !---------------------------------------------------------------
  do i=1,nhsym
         tmp1(i) = ss_plane(ipol2, i)
  enddo
  call pctile(tmp1,nhsym,40,base)

  snr2=0.01
      if (base > 0.0) snr2 = 0.398107*ccfbest2/base
      if (rms > 0.0) then
         syncshort = 0.5*ccfbest2/rms - 4.0
      else
         syncshort = -4.0
      endif
  dt2=2.5 + lagpk2*(2048.0/11025.0)

end subroutine ccf65

end module ccf65_mod
