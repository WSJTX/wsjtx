subroutine fox_tx(maxtimes,fail,called,gcalled,hm,fm,ntimes,log,logit)

! Determine fm, the next message for Fox to transmit in this slot

  character*32 fm
  character*22 hm
  character*4 g4,MyGrid,gcalled,gx,gy
  character*6 MyCall,called,cx,cy
  character*16 log
  logical isgrid,logit
  data MyCall/'KH1DX'/,MyGrid/'AJ10'/
  save

  isgrid(g4)=g4(1:1).ge.'A' .and. g4(1:1).le.'R' .and. g4(2:2).ge.'A' .and. &
       g4(2:2).le.'R' .and. g4(3:3).ge.'0' .and. g4(3:3).le.'9' .and.       &
       g4(4:4).ge.'0' .and. g4(4:4).le.'9' .and. g4(1:4).ne.'RR73'

  logit=.false.
  n=len(trim(hm))
  g4=""
  if(n.gt.8) g4=hm(n-3:n)
  call random_number(r)
  if(r.lt.fail .and. .not.isgrid(g4)) hm=""        !Fox failed to copy

  i2=len(trim(hm))
  if(i2.gt.10) then
     i1=index(hm,' ')
     i3=index(hm(i1+1:),' ') + i1
     cx=hm(i1+1:i3)
     gx=hm(i2-3:i2)
     i4=index(hm,MyCall)

! Check for a new caller
     if(i4.eq.1 .and. isgrid(gx)) then
        call random_number(r)
        isent=nint(-20+40*r)
        write(fm,1002) cx,MyCall,isent
1002    format(a6,1x,a6,i4.2)
        if(fm(15:15).eq.' ') fm(15:15)='+'
        called=cx
        gcalled=gx
     endif
     log=''

! Check for message with R+rpt
     if(i4.eq.1 .and. cx.eq.called .and.                         &
          (index(hm,'R+').ge.8 .or. index(hm,'R-').ge.8)) then
        write(log,1006) called,gcalled,isent        !Format a log entry
1006    format(a6,2x,a4,i4.2)
        if(log(14:14).eq.' ') log(14:14)='+'
        logit=.true.
        call dxped_fifo(cy,gy,isnry)
! If FIFO is empty we should call CQ in this slot
        ntimes=1
        write(fm,1008) cx,cy,isnry
1008    format(a6,' RR73; ',a6,1x,'<KH1DX>',i4.2)
        if(fm(29:29).eq.' ') fm(29:29)='+'
        called=cy
        gcalled=gy
     endif
  endif

  if(hm.eq.'') then
     if(fm(1:3).ne.'CQ ') then
!        if(ntimes.lt.maxtimes) then
           ntimes=ntimes+1
!        else
!           ntimes=1
! If FIFO is empty we should call CQ in this slot
!           call dxped_fifo(cy,gy,isnry)
!           call random_number(r)
!           isnr=nint(-20+40*r)
!           write(fm,1010) cy,gy,isnr
           write(fm,1010) called,MyCall,isent
1010       format(a6,1x,a6,i4.2)
           if(fm(15:15).eq.' ') fm(15:15)='+'
!        endif
     endif
  endif

! Collapse multiple blanks in message
  iz=len(trim(fm))
  do iter=1,5
     ib2=index(fm(1:iz),'  ')
     if(ib2.lt.1) exit
     fm=fm(1:ib2)//fm(ib2+2:)
     iz=iz-1
  enddo

! Generate waveform for fm
  return
end subroutine fox_tx
