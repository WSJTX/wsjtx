subroutine pctile(x,npts,npct,xpct)

  real x(npts)
  real,allocatable :: tmp(:)

  allocate(tmp(npts))

  tmp=x
  call shell(npts,tmp)
  j=nint(npts*0.01*npct)
  if(j.lt.1) j=1
  if(j.gt.npts) j=npts
  xpct=tmp(j)

900 continue
  return
end subroutine pctile
