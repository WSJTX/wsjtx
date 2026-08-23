!------------------------------------------------------------------------------
! NOTE: This routine preserves the legacy JT65 correlation core. Its boundary
!       validation and output defaults make degenerate inputs deterministic
!       without changing the normal signal-processing path.
!------------------------------------------------------------------------------

module ccf65_legacy_mod
  implicit none
contains

subroutine ccf65(ss_plane, nhsym, ssmax, sync1, ipol1, jpz, dt1, flipk, &
                 syncshort, snr2, ipol2, dt2)

  use four2a_legacy_wrap_mod, only: r2c_legacy, c2r_legacy
  use pctile_mod, only: pctile
  use, intrinsic :: ieee_arithmetic, only: ieee_is_finite

  implicit none

  integer, parameter :: NFFT = 512
  integer, parameter :: NH = NFFT/2
  integer, parameter :: MAX_NHSYM = 322

  integer, intent(in) :: nhsym, jpz
  real, intent(in) :: ssmax
  real, intent(out) :: sync1, dt1, flipk, syncshort, snr2, dt2
  integer, intent(out) :: ipol1, ipol2

  ! Modern interface:
  !   ss_plane(4,322) is passed as a proper 2-D slice (ss(:,:,i))
  real, intent(in) :: ss_plane(4,MAX_NHSYM)

  ! Legacy expects: real ss(4,322) passed by reference from ss(1,1,i)
  real :: ss(4,MAX_NHSYM)

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

  real tmp1(MAX_NHSYM)
  real ccf(-11:54,4)
  integer npr(126)
  integer :: i, j, k, ip, lag, lagpk, lagpk2, npol
  real :: fac, base, ccfbest, ccfbest2, ccf2, sumccf, sq, rms

! The JT65 pseudo-random sync pattern:
  data npr/                                        &
      1,0,0,1,1,0,0,0,1,1,1,1,1,1,0,1,0,1,0,0,     &
      0,1,0,1,1,0,0,1,0,0,0,1,1,1,0,0,1,1,1,1,     &
      0,1,1,0,1,1,1,1,0,0,0,1,1,0,1,0,1,0,1,1,     &
      0,0,1,1,0,1,0,1,0,1,0,0,1,0,0,0,0,0,0,1,     &
      1,0,0,0,0,0,0,0,1,1,0,1,0,0,1,0,1,1,0,1,     &
      0,1,0,1,0,0,1,1,0,0,1,0,0,1,0,0,0,0,1,1,     &
      1,1,1,1,1,1/

  sync1 = -4.0
  syncshort = -4.0
  snr2 = 0.01
  dt1 = 0.0
  dt2 = 0.0
  flipk = 1.0
  ipol1 = 1
  ipol2 = 1

  if (nhsym < 2 .or. nhsym > MAX_NHSYM) return
  if (jpz < 1 .or. jpz > 4) return
  if (.not. ieee_is_finite(ssmax)) return

  npol = jpz
  ss = ss_plane
  if (.not. all(ieee_is_finite(ss(1:npol,1:nhsym)))) return

  fac = 1.0/NFFT
  do i=1,NFFT
     pr(i)=0.
     pr2(i)=0.
     k=2*mod((i-1)/8,2)-1
     if(i.le.NH) pr2(i)=fac*k
  enddo
  do i=1,126
     j=2*i
     pr(j)=fac*(2*npr(i)-1)
  enddo

  call r2c_legacy(pr,  cpr, NFFT)
  call r2c_legacy(pr2, cpr2, NFFT)

  ccf = 0.0

! Look for JT65 sync pattern and shorthand square-wave pattern.
  ccfbest=0.
  ccfbest2=0.
  ipol1=1
  ipol2=1
  lagpk=0
  lagpk2=0
  do ip=1,npol                                  !Do npol polarizations
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
        cs2(i) = cs(i) * conjg(cpr2(i))
        cs(i)  = cs(i) * conjg(cpr(i))
     enddo

     ! === Inverse FFT: packed half-spectrum ? real ===
     call c2r_legacy(cs,  s, NFFT)
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
     
     do lag=-11,54                             !Check for best shorthand
        ccf2=s2(lag+28)
        if(ccf2.gt.ccfbest2) then
           ccfbest2=ccf2
           lagpk2=lag
           ipol2=ip
        endif
     enddo
     
  enddo

  if (.not. ieee_is_finite(ccfbest) .or. ccfbest <= 0.0) return

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
  if (ieee_is_finite(rms) .and. rms > 0.0) then
     sync1=ccfbest/rms - 4.0
     syncshort=0.5*ccfbest2/rms - 4.0
  endif
  dt1=lagpk*(2048.0/11025.0) - 2.5

! Find base level for normalizing snr2.
  do i=1,nhsym
     tmp1(i)=ss(ipol2,i)
  enddo
  call pctile(tmp1,nhsym,40,base)
  snr2=0.01
  if(base.gt.0.0) snr2=0.398107*ccfbest2/base  !### empirical
  dt2=2.5 + lagpk2*(2048.0/11025.0)

  return
end subroutine ccf65
end module ccf65_legacy_mod
