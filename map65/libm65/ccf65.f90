!------------------------------------------------------------------------------
! NOTE: This module intentionally preserves the original legacy WSJT/MAP65
!       ccf65 implementation. Modernizing this routine changes the numerical
!       behavior of the JT65 correlation (FFT layout, half-spectrum handling,
!       baseline statistics, and sync metrics), which in turn produces a
!       different false-positive/false-negative profile. Extensive testing
!       shows that the legacy algorithm yields the correct and expected
!       decode behavior, so it is retained here without modification.
!------------------------------------------------------------------------------

module ccf65_legacy_mod
contains

subroutine ccf65(ss_plane, nhsym, ssmax, sync1, ipol1, jpz, dt1, flipk, &
                 syncshort, snr2, ipol2, dt2)

  use four2a_legacy_wrap_mod, only: r2c_legacy, c2r_legacy
  use pctile_mod, only: pctile

  parameter (NFFT=512,NH=NFFT/2)
  ! Modern interface:
  !   ss_plane(4,322) is passed as a proper 2-D slice (ss(:,:,i))
  real, intent(in) :: ss_plane(4,322)

  ! Legacy expects: real ss(4,322) passed by reference from ss(1,1,i)
  real :: ss(4,322)

  ! time-domain
  real    :: s(NFFT)                     ! CCF = ss*pr
  real    :: s2(NFFT)                    ! CCF = ss*pr2
  real    :: pr(NFFT)                    ! JT65 pseudo-random sync pattern
  real    :: pr2(NFFT)                   ! JT65 shorthand pattern

  ! frequency-domain: half-spectrum, as in legacy
  complex :: cs(0:NH)                    ! Complex FT of s
  complex :: cs2(0:NH)                   ! Complex FT of s2
  complex :: cpr(0:NH)                   ! Complex FT of pr
  complex :: cpr2(0:NH)                  ! Complex FT of pr2

  real tmp1(322)
  real ccf(-11:54,4)
  logical first
  integer npr(126)
  data first/.true./
  save s,s2,pr,pr2,cs,cs2,cpr,cpr2

! The JT65 pseudo-random sync pattern:
  data npr/                                        &
      1,0,0,1,1,0,0,0,1,1,1,1,1,1,0,1,0,1,0,0,     &
      0,1,0,1,1,0,0,1,0,0,0,1,1,1,0,0,1,1,1,1,     &
      0,1,1,0,1,1,1,1,0,0,0,1,1,0,1,0,1,0,1,1,     &
      0,0,1,1,0,1,0,1,0,1,0,0,1,0,0,0,0,0,0,1,     &
      1,0,0,0,0,0,0,0,1,1,0,1,0,0,1,0,1,1,0,1,     &
      0,1,0,1,0,0,1,1,0,0,1,0,0,1,0,0,0,0,1,1,     &
      1,1,1,1,1,1/

  logical :: debug
  debug = .false. ! (nhsym .gt. 300)
  
  ss = ss_plane
  
  if(first) then
     fac=1.0/NFFT
     do i=1,NFFT
        pr(i)=0.
        pr2(i)=0.
        k=2*mod((i-1)/8,2)-1
        if(i.le.NH) pr2(i)=fac*k
     enddo
     do i=1,126
        j=2*i
        pr(j)=fac*(2*npr(i)-1)
!        pr(j-1)=pr(j)
     enddo

     call r2c_legacy(pr,  cpr,  NFFT)
     call r2c_legacy(pr2, cpr2, NFFT)

     first=.false.
  endif

  syncshort=0.
  snr2=0.

! Look for JT65 sync pattern and shorthand square-wave pattern.
  ccfbest=0.
  ccfbest2=0.
  ipol1=1
  ipol2=1
  do ip=1,jpz                                  !Do jpz polarizations
     do i=1,nhsym-1
!        s(i)=ss(ip,i)+ss(ip,i+1)
        s(i)=min(ssmax,ss(ip,i)+ss(ip,i+1))
     enddo
     call pctile(s,nhsym-1,50,base)
     s(1:nhsym-1)=s(1:nhsym-1)-base
     s(nhsym:NFFT)=0.

     ! === Forward FFT: real ? packed half-spectrum ===
     call r2c_legacy(s, cs, NFFT)

     ! === Multiply by sync patterns in frequency domain ===
     do i=0,NH
            cs2(i) = cs(i)*conjg(cpr2(i))
            cs(i) = cs(i)*conjg(cpr(i))
         enddo

     ! === Inverse FFT: packed half-spectrum ? real ===
     call c2r_legacy(cs,  s,  NFFT)
     call c2r_legacy(cs2, s2, NFFT)

     do lag=-11,54                             !Check for best JT65 sync
        j=lag
        if(j.lt.1) j=j+NFFT
        ccf(lag,ip)=s(j)
        if(abs(ccf(lag,ip)).gt.ccfbest) then
           ccfbest=abs(ccf(lag,ip))
           lagpk=lag
           ipol1=ip
           flipk=1.0
           if(ccf(lag,ip).lt.0.0) flipk=-1.0
        endif
     enddo
     
!###  Not sure why this is ever true???  
     if(sum(ccf).eq.0.0) return
!###
     do lag=-11,54                             !Check for best shorthand
        ccf2=s2(lag+28)
        if(ccf2.gt.ccfbest2) then
           ccfbest2=ccf2
           lagpk2=lag
           ipol2=ip
        endif
     enddo
     
  enddo

! Find rms level on baseline of "ccfblue", for normalization.
  sumccf=0.
  do lag=-11,54
     if(abs(lag-lagpk).gt.1) sumccf=sumccf + ccf(lag,ipol1)
  enddo
  base=sumccf/50.0
  sq=0.
  do lag=-11,54
     if(abs(lag-lagpk).gt.1) sq=sq + (ccf(lag,ipol1)-base)**2
  enddo
  rms=sqrt(sq/49.0)
  sync1=-4.0
  if(rms.gt.0.0) sync1=ccfbest/rms - 4.0
  dt1=lagpk*(2048.0/11025.0) - 2.5

! Find base level for normalizing snr2.
  do i=1,nhsym
     tmp1(i)=ss(ipol2,i)
  enddo
  call pctile(tmp1,nhsym,40,base)
  snr2=0.01
  if(base.gt.0.0) snr2=0.398107*ccfbest2/base  !### empirical
  syncshort=0.5*ccfbest2/rms - 4.0             !### better normalizer than rms?
  dt2=2.5 + lagpk2*(2048.0/11025.0)

  if (debug) write(logunit,*) 'QT?', 'sync1=', sync1, ' lagpk=', lagpk, ' dt1=', dt1,' snr2=',snr2

  return
end subroutine ccf65
end module ccf65_legacy_mod
