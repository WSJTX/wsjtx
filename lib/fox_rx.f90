subroutine fox_rx(fail,called,fm,hm)

! Given fm, recently transmitted by Fox, determine hm -- the next
! message for Hound to transmit

  parameter (MAXSIG=5,NCALLS=268)  
  character*6 xcall(NCALLS)
!  character*8 mycall_plus
  character*4 xgrid(NCALLS)
  integer isnr(NCALLS)
  character*32 fm
  character*22 hm
  character*6 cx,called,MyCall
  character*4 gx
  common/dxpfifo/nc,isnr,xcall,xgrid
  data MyCall/'KH1DX'/
  save

  call random_number(r)
  if(r.lt.fail) fm=''                        !Hound fails to copy
  i1=index(fm,MyCall)
  if(fm(1:3).eq.'CQ ' .and. i1.ge.4) then
     call dxped_fifo(cx,gx,isnrx)
     ntimes=1
     write(hm,1000) MyCall,cx,gx
1000 format(a6,1x,a6,1x,a4)
  endif

! Check for a "RR73" message
  ia=index(fm,trim(cx))
  ib=index(fm,';')
  ic=index(fm,trim(called))
  id=index(fm,'RR73;')
  if((ia.eq.1 .or. ic.eq.ib+2) .and. id.ge.4) then
     i1=index(fm,';')+2
     i2=index(fm,'<')-2
     cx=fm(i1:i2)                      !Callsign for next QSO
     call random_number(r)
     ireport=nint(-20+40*r)
! Send report to next caller
     write(hm,1004) MyCall,cx,ireport
1004 format(a6,1x,a6,' R',i3.2)
     if(hm(16:16).eq.' ') hm(16:16)='+'
  endif

! Check for a message with a report to Hound
  i1=index(fm,trim(called))
  i2=index(fm,MyCall)
  if(i1.eq.1 .and. i2.ge.5 .and.   &
       (index(fm,'+').ge.8 .or. index(fm,'-').ge.8)) then
! Send "R+rpt" to Fox
     write(hm,1004) MyCall,called,isnrx
     if(hm(16:16).eq.' ') hm(16:16)='+'
  endif

! Collapse multiple blanks in message
  iz=len(trim(hm))
  do iter=1,5
     ib2=index(hm(1:iz),'  ')
     if(ib2.lt.1) exit
     hm=hm(1:ib2)//hm(ib2+2:)
     iz=iz-1
  enddo

  return
end subroutine fox_rx
