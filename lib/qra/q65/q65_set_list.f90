subroutine q65_set_list(mycall,hiscall,hisgrid,codewords,ncw)

  character*12 mycall,hiscall
  character*6 hisgrid
  character*37 msg0,msg,msgsent
  integer codewords(63,64)
  integer itone(85)
  integer isync(22)
  data isync/1,9,12,13,15,22,23,26,27,33,35,38,46,50,55,60,62,66,69,74,76,85/

  ncw=0
  if(hiscall(1:1).eq. ' ') return
  
  ncw=58
  msg0=trim(mycall)//' '//trim(hiscall)
  j0=len(trim(msg0))+2
  isnr0=-35
  do i=1,ncw
     msg=msg0
     if(i.eq.2) msg(j0:j0+2)='RRR'
     if(i.eq.3) msg(j0:j0+3)='RR73'
     if(i.eq.4) msg(j0:j0+1)='73'
     if(i.ge.5 .and. i.le.56) then
        isnr=isnr0 + (i-5)/2
        if(iand(i,1).eq.1) write(msg(j0:j0+2),'(i3.2)') isnr
        if(iand(i,1).eq.0) write(msg(j0:j0+3),'("R",i3.2)') isnr
     endif
     if(i.eq.57) msg='CQ '//trim(hiscall)//' '//hisgrid(1:4)
     if(i.eq.58) msg(j0:j0+3)=hisgrid(1:4)
     call genq65(msg,0,msgsent,itone,i3,n3)
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
!     write(*,3001) i,isnr,codewords(1:13,i),trim(msg)
!3001 format(i2,2x,i3.2,2x,13i3,2x,a)
  enddo

  return
end subroutine q65_set_list
