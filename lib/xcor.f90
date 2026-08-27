!subroutine xcor(ss,ipk,nsteps,nsym,lag1,lag2,ccf,ccf0,lagpk,flip,fdot,nrobust)
subroutine xcor(ipk,nsteps,nsym,lag1,lag2,ccf,ccf0,lagpk,flip,fdot,nrobust)

! Computes ccf of a row of ss and the pseudo-random array pr.  Returns
! peak of the CCF and the lag at which peak occurs.  For JT65, the 
! CCF peak may be either positive or negative, with negative implying
! the "OOO" message.

  use jt65_mod
  parameter (NHMAX=3413)           !Max length of power spectra
  parameter (NSMAX=552)            !Max number of quarter-symbol steps
  real ss(NSMAX,NHMAX)             !2d spectrum, stepped by half-symbols
  real a(NSMAX)
!  real ccf(-44:118)
  real ccf(lag1:lag2)
!  save
  common/sync/ss

  df=12000.0/8192.
!  dtstep=0.5/df
  dtstep=0.25/df
  fac=dtstep/(60.0*df)
  do j=1,nsteps
     ii=nint((j-nsteps/2)*fdot*fac)+ipk
     if( (ii.ge.1) .and. (ii.le.NHMAX) ) then
       a(j)=ss(j,ii)
     endif
  enddo

  if(nrobust.eq.1) then
! use robust correlation estimator to mitigate AGC attack spikes at beginning
! this reduces the number of spurious candidates overall
    call pctile(a,nsteps,50,xmed)
    do j=1,nsteps
      if( a(j).ge.xmed ) then
        a(j)=1
      else
        a(j)=-1
      endif
    enddo
  endif

  ccfmax=0.
  ccfmin=0.
  lagpk=lag1
  lagmin=lag1
  do lag=lag1,lag2
     x=0.
     do i=1,nsym
        j=4*i-3+lag
        if(j.ge.1 .and. j.le.nsteps) x=x+a(j)*pr(i)
     enddo
     ccf(lag)=2*x                        !The 2 is for plotting scale
     if(ccf(lag).gt.ccfmax) then
        ccfmax=ccf(lag)
        lagpk=lag
     endif

     if(ccf(lag).lt.ccfmin) then
        ccfmin=ccf(lag)
        lagmin=lag
     endif
  enddo

  ccf0=ccfmax
  flip=1.0
  if(-ccfmin.gt.ccfmax) then
     do lag=lag1,lag2
        ccf(lag)=-ccf(lag)
     enddo
     lagpk=lagmin
     ccf0=-ccfmin
     flip=-1.0
  endif

  return
end subroutine xcor
