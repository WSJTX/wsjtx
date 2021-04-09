subroutine lorentzian(y,npts,a)

! Input:  y(npts); assume x(i)=i, i=1,npts
! Output: a(5)
!         a(1) = baseline
!         a(2) = amplitude
!         a(3) = x0
!         a(4) = width
!         a(5) = chisqr

  real y(npts)
  real a(5)
  real deltaa(4)

  a=0.
  df=12000.0/8192.0                               !df = 1.465 Hz
  width=0.
  ipk=0
  ymax=-1.e30
  do i=1,npts
     if(y(i).gt.ymax) then
        ymax=y(i)
        ipk=i
     endif
!     write(50,3001) i,i*df,y(i)
!3001 format(i6,2f12.3)
  enddo
!  base=(sum(y(ipk-149:ipk-50)) + sum(y(ipk+51:ipk+150)))/200.0
  base=(sum(y(1:20)) + sum(y(npts-19:npts)))/40.0
  stest=ymax - 0.5*(ymax-base)
  ssum=y(ipk)
  do i=1,50
     if(ipk+i.gt.npts) exit
     if(y(ipk+i).lt.stest) exit
     ssum=ssum + y(ipk+i)
  enddo
  do i=1,50
     if(ipk-i.lt.1) exit
     if(y(ipk-i).lt.stest) exit
     ssum=ssum + y(ipk-i)
  enddo
  ww=ssum/y(ipk)
  width=2
  t=ww*ww - 5.67
  if(t.gt.0.0) width=sqrt(t)
  a(1)=base
  a(2)=ymax-base
  a(3)=ipk
  a(4)=width

! Now find Lorentzian parameters

  deltaa(1)=0.1
  deltaa(2)=0.1
  deltaa(3)=1.0
  deltaa(4)=1.0
  nterms=4

!  Start the iteration
  chisqr=0.
  chisqr0=1.e6
  do iter=1,5
     do j=1,nterms
        chisq1=fchisq0(y,npts,a)
        fn=0.
        delta=deltaa(j)
10      a(j)=a(j)+delta
        chisq2=fchisq0(y,npts,a)
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
        chisq3=fchisq0(y,npts,a)
        if(chisq3.lt.chisq2) then
           chisq1=chisq2
           chisq2=chisq3
           go to 20
        endif

! Find minimum of parabola defined by last three points
        delta=delta*(1./(1.+(chisq1-chisq2)/(chisq3-chisq2))+0.5)
        a(j)=a(j)-delta
        deltaa(j)=deltaa(j)*fn/3.
!          write(*,4000) iter,j,a,chisq2
!4000      format(i1,i2,4f10.4,f11.3)
     enddo
     chisqr=fchisq0(y,npts,a)
!       write(*,4000) 0,0,a,chisqr
     if(chisqr/chisqr0.gt.0.99) exit
     chisqr0=chisqr
  enddo
  a(5)=chisqr

  return
end subroutine lorentzian

