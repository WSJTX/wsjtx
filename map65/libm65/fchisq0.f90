real function fchisq0(y,npts,a)

  real y(npts),a(4)
  
!  rewind 51
  chisq = 0.
  do i=1,npts
     x=i
     z=(x-a(3))/(0.5*a(4))
     yfit=a(1)
     if(abs(z).lt.3.0) then
        d=1.0 + z*z
        yfit=a(1) + a(2) * (1.0/d - 0.1)
     endif
     chisq=chisq + (y(i) - yfit)**2
!     write(51,3001) i,y(i),yfit,y(i)-yfit
!3001 format(i5,3f10.4)
  enddo
  fchisq0=chisq

  return
end function fchisq0

