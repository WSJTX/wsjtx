subroutine polfit(y,npts,a)

! Input:  y(npts)                !Expect npts=4
! Output: a(1) = baseline
!         a(2) = amplitude
!         a(3) = theta (deg)

  real y(npts)
  real a(3)
  real deltaa(3)
  integer ipk(1)
  save

! Set starting values:
  a(1)=minval(y)
  a(2)=maxval(y)-a(1)
  ipk=maxloc(y)
  a(3)=(ipk(1)-1)*45.0

  deltaa(1:2)=0.1*a(2)
  deltaa(3)=10.0
  nterms=3

!  Start the iteration
  chisqr=0.
  chisqr0=1.e6
  iters=10

  do iter=1,iters
     do j=1,nterms
        chisq1=fchisq_pol(y,npts,a)
        fn=0.
        delta=deltaa(j)
10      a(j)=a(j)+delta
        chisq2=fchisq_pol(y,npts,a)
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
        chisq3=fchisq_pol(y,npts,a)
        if(chisq3.lt.chisq2) then
           chisq1=chisq2
           chisq2=chisq3
           go to 20
        endif

! Find minimum of parabola defined by last three points
        delta=delta*(1./(1.+(chisq1-chisq2)/(chisq3-chisq2))+0.5)
        a(j)=a(j)-delta
        deltaa(j)=deltaa(j)*fn/3.
!          write(*,4000) iter,j,a,deltaa,chisq2
!4000      format(2i2,2(2x,3f8.2),f12.5)
     enddo  ! j=1,nterms
     chisqr=fchisq_pol(y,npts,a)
!     write(*,4000) 0,0,a,chisqr
     if(chisqr.lt.1.0) exit
     if(deltaa(1).lt.0.01*(a(2)-a(1)) .and. deltaa(2).lt.0.01*(a(2)-a(1))   &
          .and. deltaa(3).lt.1.0) exit
     if(chisqr/chisqr0.gt.0.99) exit
     chisqr0=chisqr
  enddo  ! iter
  a(3)=mod(a(3)+360.0,180.0)

  return
end subroutine polfit

real function fchisq_pol(y,npts,a)

  real y(npts),a(3)
  data rad/57.2957795/
  
  chisq = 0.
  do i=1,npts
     theta=(i-1)*45.0
     yfit=a(1) + a(2)*cos((theta-a(3))/rad)**2
     chisq=chisq + (y(i) - yfit)**2
  enddo
  fchisq_pol=chisq

  return
end function fchisq_pol
