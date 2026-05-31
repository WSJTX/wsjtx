module zplot_mod
  implicit none
contains
  subroutine zplot(amp,nz,jj,ave,rms)
    implicit none
    integer, intent(in) :: nz, jj
    real, intent(in) :: amp(nz)
    real, intent(out) :: ave, rms
    
    character*1 :: line(100)
    character*1 :: mark(0:6)
    data mark/' ',' ','.','-','+','X','$'/
    integer :: i, ipk, n
    real :: sum, smax, sq
  
    sum=0.
    smax=0.
    do i=1,nz
       sum=sum+amp(i)
       if(amp(i).gt.smax) then
          smax=amp(i)
          ipk=i
       endif
    enddo
    ave=(sum-smax)/(nz-1)
    sq=0.
    do i=1,nz
       if(i.ne.ipk) sq=sq+(amp(i)-ave)**2
    enddo
    rms=sqrt(sq/(nz-2))
  
    do i=1,nz
       n=(amp(i)-ave)/rms
  !     n=(amp(i)-ave)/0.33
       if(n.lt.0) n=0
       if(n.gt.6) n=6
       line(i)=mark(n)
    enddo
    write(89,1010) jj,0.01*ave,0.01*rms,(line(i),i=1,nz)
  1010 format(i3,2f6.1,1x,100a1)
    
  end subroutine zplot
end module zplot_mod
