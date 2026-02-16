module pctile_mod
  implicit none
contains

subroutine pctile(x,npts,npct,xpct)

implicit none

  real x(npts)
  real,allocatable :: tmp(:)
  real xpct
  integer npts,npct,j

  if(npts.lt.1 .or. npct.lt.0 .or. npct.gt.100) then !w3sz was if(npts.lt.0 
     xpct=1.0
     go to 900
  endif
  allocate(tmp(npts))

  tmp=x
  call shell(npts,tmp)
  j=nint(npts*0.01*npct)
  if(j.lt.1) j=1
  if(j .gt. npts .and. npts .gt. 0) j=npts  !w3sz added .and. ntpts .gt. 0
  xpct=tmp(j)
  deallocate(tmp)

900 return
end subroutine pctile

end module pctile_mod