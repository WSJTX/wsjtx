subroutine lorentzian_fading(c,npts,fs,fspread)
!
! npts is the total length of the simulated data vector
!
  complex c(0:npts-1)
  complex cspread(0:npts-1)
  complex z

  twopi=8.0*atan(1.0)
  df=fs/npts
  nh=npts/2
  cspread(0)=1.0
  cspread(nh)=0.
  b=6.0
  do i=1,nh
     f=i*df
     x=b*f/fspread
     z=0.
     a=0.
     if(x.lt.3.0) then
        a=sqrt(1.111/(1.0+x*x)-0.1)
        phi1=twopi*rran()
        z=a*cmplx(cos(phi1),sin(phi1))
     endif
     cspread(i)=z
     z=0.
     if(x.lt.3.0) then
        phi2=twopi*rran()
        z=a*cmplx(cos(phi2),sin(phi2))
     endif
     cspread(npts-i)=z
  enddo

  call four2a(cspread,npts,1,1,1)

  s=sum(abs(cspread)**2)
  avep=s/npts
  fac=sqrt(1.0/avep)
  cspread=fac*cspread
  c=cspread*c
   
  return
end subroutine lorentzian_fading
