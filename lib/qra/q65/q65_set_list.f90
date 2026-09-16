subroutine q65_set_list(mycall,hiscall,hisgrid,codewords,ncw)

  parameter (MAX_NCW=206)
  character*12 mycall,hiscall
  character*6 hisgrid
  character*37 msg,msgsent
  logical my_std,his_std
  integer codewords(63,MAX_NCW)
  integer itone(85)
  integer isync(22)
  data isync/1,9,12,13,15,22,23,26,27,33,35,38,46,50,55,60,62,66,69,74,76,85/

  ncw=0
  if(hiscall(1:1).eq. ' ') return
  call stdcall(mycall,my_std)
  call stdcall(hiscall,his_std)
  
  ncw=MAX_NCW
  do i=1,ncw
     msg=trim(mycall)//' '//trim(hiscall)
     if(.not.my_std) then
        if(i.eq.1 .or. i.ge.6)  msg='<'//trim(mycall)//'> '//trim(hiscall)
        if(i.ge.2 .and. i.le.4) msg=trim(mycall)//' <'//trim(hiscall)//'>'
     else if(.not.his_std) then
        if(i.le.4 .or. i.eq.6) msg='<'//trim(mycall)//'> '//trim(hiscall)
        if(i.ge.7) msg=trim(mycall)//' <'//trim(hiscall)//'>'
     endif
     j0=len(trim(msg))+2
     if(i.eq.2) msg(j0:j0+2)='RRR'
     if(i.eq.3) msg(j0:j0+3)='RR73'
     if(i.eq.4) msg(j0:j0+1)='73'
     if(i.eq.5) then
        if(his_std) msg='CQ '//trim(hiscall)//' '//hisgrid(1:4)
        if(.not.his_std) msg='CQ '//trim(hiscall)
     endif
     if(i.eq.6 .and. his_std) msg(j0:j0+3)=hisgrid(1:4)
     if(i.ge.7 .and. i.le.206) then
        isnr = -50 + (i-7)/2
        if(iand(i,1).eq.1) then
           write(msg(j0:j0+2),'(i3.2)') isnr
           if(msg(j0:j0).eq.' ') msg(j0:j0)='+'
        else
           write(msg(j0:j0+3),'("R",i3.2)') isnr
           if(msg(j0+1:j0+1).eq.' ') msg(j0+1:j0+1)='+'
        endif
     endif

     call genq65(msg,0,msgsent,itone,i3,n3,0)
     i0=1
     j=0
     do k=1,85
        if(k.eq.isync(i0)) then
           i0=i0+1
           cycle
        endif
        j=j+1
        codewords(j,i)=itone(k) - 1
     enddo
!     write(71,3001) i,isnr,codewords(1:13,i),trim(msg)
!3001 format(i3,2x,i3.2,2x,13i3,2x,a)
  enddo

  return
end subroutine q65_set_list

subroutine stdcall(callsign,std)

  character*12 callsign
  character*1 c
  logical is_digit,is_letter,std
!Statement functions:
  is_digit(c)=c.ge.'0' .and. c.le.'9'
  is_letter(c)=c.ge.'A' .and. c.le.'Z'

! Check for standard callsign
  iarea=-1
  n=len(trim(callsign))
  do i=n,2,-1
     if(is_digit(callsign(i:i))) exit
  enddo
  iarea=i                                   !Right-most digit (call area)
  npdig=0                                   !Digits before call area
  nplet=0                                   !Letters before call area
  do i=1,iarea-1
     if(is_digit(callsign(i:i))) npdig=npdig+1
     if(is_letter(callsign(i:i))) nplet=nplet+1
  enddo
  nslet=0                                   !Letters in suffix
  do i=iarea+1,n
     if(is_letter(callsign(i:i))) nslet=nslet+1
  enddo
  std=.true.
  if(iarea.lt.2 .or. iarea.gt.3 .or. nplet.eq.0 .or.       &
       npdig.ge.iarea-1 .or. nslet.gt.3) std=.false.

  return
end subroutine stdcall
