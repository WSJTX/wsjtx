subroutine q65_sync(ss,i0,nts_q65,ntrperiod,iseq,sync_ok,snr,xdt)

! Test for presence of Q65 sync tone

  parameter (NFFT=32768)
  parameter (LAGMAX=33)
  real ss(400,NFFT)                !Symbol spectra
  real ccf(0:LAGMAX)               !The WSJT "blue curve", peak at DT
  logical sync_ok
  integer isync0(22),isync(22),ipk(1)

! Q65 sync symbols
  data isync0/1,9,12,13,15,22,23,26,27,33,35,38,46,50,55,60,62,66,69,74,76,85/

  sync_ok=.false.
!  if(ntrperiod.ne.60) return
  
  tstep=0.15                      !0.5*tsym_Q65-30x, 0.25*tsys_Q65-60x
  nfac=4
  if(ntrperiod.eq.30) nfac=2
  do i=1,22                                !Expand sync stride for Q65-60x
     isync(i)=nfac*(isync0(i)-1) + 1
  enddo

  m=nts_q65/2
  if(ntrperiod.eq.30) m=nts_q65/4
  i1=max(1,i0-m)
  i2=min(NFFT,i0+m)
  ccf=0.
  do lag=0,LAGMAX                     !Search over range of DT
     do j=1,22                        !Test for Q65 sync
        k=isync(j) + lag + iseq*200
        if(k.ge.400) cycle
        if(ntrperiod.eq.60) then
           ccf(lag)=ccf(lag) + sum(ss(k,i1:i2)) + sum(ss(k+1,i1:i2)) &
                + sum(ss(k+2,i1:i2)) + sum(ss(k+3,i1:i2))
        else
           ccf(lag)=ccf(lag) + sum(ss(k,i1:i2)) + sum(ss(k+1,i1:i2))
        endif
! Q: Should we use weighted sums, perhaps a Lorentzian peak?
     enddo
  enddo
  ccfmax=maxval(ccf)
  ipk=maxloc(ccf)
  lagbest=ipk(1)-1
  xdt=lagbest*tstep - 1.0
  if(ntrperiod.eq.30) xd=xdt+0.6      !Why ???

  xsum=0.
  sq=0.
  nsum=0
  fpk=0.001*i0*96000.0/32768.0 + 32.0
  do i=0,lagmax                       !Compute ave and rms of "blue curve"
     if(abs(i-lagbest).gt.2) then
        xsum=xsum+ccf(i)
        sq=sq+ccf(i)**2
        nsum=nsum+1
     endif
!     write(40,3040) i,i*tstep-1.0,ccf(i),fpk
!3040 format(i5,3f8.2)
  enddo
  ave=xsum/nsum
  rms=sqrt(sq/nsum - ave*ave)
  snr=(ccfmax-ave)/rms
  sync_ok=snr.ge.5.0                  !Require snr > 5.0 for sync detection

  return
end subroutine q65_sync
