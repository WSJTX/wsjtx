subroutine plotsave(swide,nw,nh,irow)

  real, dimension(:,:), allocatable :: sw,sw_old
  real swide(0:nw-1)
  integer mw,mh
  data nw0/-1/,nh0/-1/
  save nw0,nh0,sw

  if(irow.eq.-99) then
     if(allocated(sw)) deallocate(sw)
     go to 900
  endif

  if(nw.ne.nw0 .or. nh.ne.nh0 .or. (.not.allocated(sw))) then
     if(allocated(sw)) then
! Resize: keep the overlapping history instead of blanking the waterfall.
        call move_alloc(sw,sw_old)
        allocate(sw(0:nw-1,0:nh-1))
        sw=0.
        mw=min(nw,nw0)
        mh=min(nh,nh0)
        sw(0:mw-1,0:mh-1)=sw_old(0:mw-1,0:mh-1)
        deallocate(sw_old)
     else
        allocate(sw(0:nw-1,0:nh-1))
        sw=0.
     endif
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
