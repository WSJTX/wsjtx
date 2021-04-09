subroutine pctile(x,npts,npct,xpct)

  parameter (NMAX=100000)
  real*4 x(npts)
  real*4 tmp(NMAX)

  if(npts.le.0) then
     xpct=1.0
     go to 900
  endif
  if(npts.gt.NMAX) stop

  tmp(1:npts)=x
  call shell(npts,tmp)
  j=nint(npts*0.01*npct)
  if(j.lt.1) j=1
  if(j.gt.npts) j=npts
  xpct=tmp(j)

900 continue
  return
end subroutine pctile
