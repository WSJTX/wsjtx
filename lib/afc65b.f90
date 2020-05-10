subroutine afc65b(cx,npts,fsample,nflip,mode65,a,ccfbest,dtbest)

! Find delta f, f1, f2 ==> a(1:3)

  complex cx(npts)
  real a(5),deltaa(5)

  a=0.
  a1=0.
  a2=0.
  i2=8*mode65
  i1=-i2
  j2=8*mode65
  j1=-j2
  ccfmax=0.
  istep=2*mode65
  do iter=1,2
     do i=i1,i2,istep
        a(1)=i
        do j=j1,j2,istep
           a(2)=j
           chisq=fchisq65(cx,npts,fsample,nflip,a,ccf,dtmax)
           if(ccf.gt.ccfmax) then
              a1=a(1)
              a2=a(2)
              ccfmax=ccf
           endif
!           write(81,3081) istep,i1,i2,j1,j2,i,j,ccf,ccfmax,dtmax,a1,a2
!3081       format(7i4,5f8.2)
        enddo
     enddo
     i1=int(a1)-istep
     i2=int(a1)+istep
     j1=int(a2)-istep
     j2=int(a2)+istep
     istep=1
  enddo
  
!  a(1)=0.
!  a(2)=0.
  a(1)=a1
  a(2)=a2
  a(3)=0.
  a(4)=0.
  deltaa(1)=2.0*mode65
  deltaa(2)=2.0*mode65
  deltaa(3)=1.0
  nterms=2                                  !Maybe 2 is enough?

!  Start the iteration
  chisqr=0.
  chisqr0=1.e6
  do iter=1,100                              !How many iters is enough?
     do j=1,nterms
        chisq1=fchisq65(cx,npts,fsample,nflip,a,ccfmax,dtmax)
        fn=0.
        delta=deltaa(j)
10      a(j)=a(j)+delta
        chisq2=fchisq65(cx,npts,fsample,nflip,a,ccfmax,dtmax)
        if(chisq2.eq.chisq1) go to 10
        if(chisq2.gt.chisq1) then
           delta=-delta                      !Reverse direction
           a(j)=a(j)+delta
           tmp=chisq1
           chisq1=chisq2
           chisq2=tmp
        endif
20      fn=fn+1.0
        a(j)=a(j)+delta
        chisq3=fchisq65(cx,npts,fsample,nflip,a,ccfmax,dtmax)
        if(chisq3.lt.chisq2) then
           chisq1=chisq2
           chisq2=chisq3
           go to 20
        endif

! Find minimum of parabola defined by last three points
        delta=delta*(1./(1.+(chisq1-chisq2)/(chisq3-chisq2))+0.5)
        a(j)=a(j)-delta
        deltaa(j)=deltaa(j)*fn/3.
!        write(*,4000) iter,j,a(1:2),-chisq2
!4000    format(2i2,4f9.4)
     enddo
     chisqr=fchisq65(cx,npts,fsample,nflip,a,ccfmax,dtmax)
     fdiff=chisqr/chisqr0-1.0
!     write(*,4000) 0,0,a(1:2),-chisqr,fdiff
     if(abs(fdiff).lt.0.0001) exit
     chisqr0=chisqr
  enddo
  ccfbest=ccfmax * (1378.125/fsample)**2
  dtbest=dtmax

  return
end subroutine afc65b
