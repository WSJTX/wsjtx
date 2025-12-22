subroutine dxped_fifo(cx,gx,isnrx)

  parameter (NCALLS=268)  
  character*6 xcall(NCALLS)
  character*4 xgrid(NCALLS)
  integer isnr(NCALLS)

  character cx*6,gx*4
  common/dxpfifo/nc,isnr,xcall,xgrid

  if(nc.lt.NCALLS) then
     nc=nc+1
     cx=xcall(nc)
     gx=xgrid(nc)
     isnrx=isnr(nc)
  else
     cx='      '
     gx='    '
     isnrx=0
  endif
  
  return
end subroutine dxped_fifo
