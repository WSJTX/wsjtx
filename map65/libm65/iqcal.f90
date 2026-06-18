module iqcal_mod
  implicit none
contains
  subroutine iqcal(nn,c,nfft,gain,phase,zsum,ipk,reject) &
    bind(C, name="iqcal_")
    implicit none
    integer, intent(in) :: nn
    integer, intent(in) :: nfft
    complex, intent(in) :: c(0:nfft-1)
    real, intent(out) :: gain, phase, reject
    complex, intent(inout) :: zsum
    integer, intent(out) :: ipk
    
    complex :: z,zave
    real :: s,smax,pimage,p,tmp
    integer :: i, n_avg
    
    if(nn.eq.0) then
       zsum=0.
    endif
    n_avg = nn + 1
    
    smax=0.
    ipk=1
    do i=1,nfft-1
       s=real(c(i))**2 + aimag(c(i))**2
       if(s.gt.smax) then
          smax=s
          ipk=i
       endif
    enddo
    pimage=real(c(nfft-ipk))**2 + aimag(c(nfft-ipk))**2
    p=smax + pimage
    z=c(ipk)*c(nfft-ipk)/p
    zsum=zsum+z
    
    zave=zsum/real(n_avg) 
    
    tmp=sqrt(1.0 - (2.0*real(zave))**2)
    phase=asin(2.0*aimag(zave)/tmp)
    gain=tmp/(1.0-2.0*real(zave))
    if(smax > 0.0) then
       reject=10.0*log10(pimage/smax)
    else
       reject=0.0
    endif
    
  end subroutine iqcal
end module iqcal_mod
