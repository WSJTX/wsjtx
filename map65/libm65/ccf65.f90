subroutine ccf65(ss,nhsym,ssmax,sync1,ipol1,jpz,dt1,flipk,      &
     syncshort,snr2,ipol2,dt2)

  parameter (NFFT=512,NH=NFFT/2)
  real ss(4,322)                   !Input: half-symbol powers, 4 pol'ns
  real s(NFFT)                     !CCF = ss*pr
  complex cs(0:NH)                 !Complex FT of s
  real s2(NFFT)                    !CCF = ss*pr2
  complex cs2(0:NH)                !Complex FT of s2
  real pr(NFFT)                    !JT65 pseudo-random sync pattern
  complex cpr(0:NH)                !Complex FT of pr
  real pr2(NFFT)                   !JT65 shorthand pattern
  complex cpr2(0:NH)               !Complex FT of pr2
  real tmp1(322)
  real ccf(-11:54,4)
  logical first
  integer npr(126)
  data first/.true./
  equivalence (s,cs),(pr,cpr),(s2,cs2),(pr2,cpr2)
  save

! The JT65 pseudo-random sync pattern:
  data npr/                                        &
      1,0,0,1,1,0,0,0,1,1,1,1,1,1,0,1,0,1,0,0,     &
      0,1,0,1,1,0,0,1,0,0,0,1,1,1,0,0,1,1,1,1,     &
      0,1,1,0,1,1,1,1,0,0,0,1,1,0,1,0,1,0,1,1,     &
      0,0,1,1,0,1,0,1,0,1,0,0,1,0,0,0,0,0,0,1,     &
      1,0,0,0,0,0,0,0,1,1,0,1,0,0,1,0,1,1,0,1,     &
      0,1,0,1,0,0,1,1,0,0,1,0,0,1,0,0,0,0,1,1,     &
      1,1,1,1,1,1/

  if(first) then
! Initialize pr, pr2; compute cpr, cpr2.
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
! Not sure why, but it works significantly better without the following line:
!        pr(j-1)=pr(j)
     enddo
     call four2a(cpr,NFFT,1,-1,0)
     call four2a(cpr2,NFFT,1,-1,0)
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
     call four2a(cs,NFFT,1,-1,0)                !Real-to-complex FFT
     do i=0,NH
        cs2(i)=cs(i)*conjg(cpr2(i))            !Mult by complex FFT of pr2
        cs(i)=cs(i)*conjg(cpr(i))              !Mult by complex FFT of pr
     enddo
     call four2a(cs,NFFT,1,1,-1)               !Complex-to-real inv-FFT
     call four2a(cs2,NFFT,1,1,-1)              !Complex-to-real inv-FFT

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

  return
end subroutine ccf65
