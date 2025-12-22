subroutine plotsave(swide,nw,nh,irow)

  real, dimension(:,:), allocatable :: sw
  real swide(0:nw-1)
  data nw0/-1/,nh0/-1/
  save nw0,nh0,sw

  if(irow.eq.-99) then
     if(allocated(sw)) deallocate(sw)
     go to 900
  endif

  if(nw.ne.nw0 .or. nh.ne.nh0 .or. (.not.allocated(sw))) then
     if(allocated(sw)) deallocate(sw)
!     if(nw0.ne.-1) deallocate(sw)
     allocate(sw(0:nw-1,0:nh-1))
     sw=0.
     nw0=nw
     nh0=nh
  endif
  df=12000.0/16384
  if(irow.lt.0) then
! Push a new row of data into sw
     do j=nh-1,1,-1
        sw(0:nw-1,j)=sw(0:nw-1,j-1)
     enddo
     sw(0:nw-1,0)=swide
  else
! Return the saved "irow" as swide(), for a waterfall replot.
     swide=sw(0:nw-1,irow)
  endif

900 return
end subroutine plotsave
