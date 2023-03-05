subroutine q65_set_list2(mycall,hiscall,hisgrid,callers,nhist2,codewords,ncw)

  use types
  parameter (MAX_NCW=206)
  parameter (MAX_CALLERS=40)  !For multiple q3 decodes in NA VHf Contest mode
  character*12 mycall,hiscall
  character*6 hisgrid,c6
  character*4 g4
  character*37 msg,msgsent
  logical std,isgrid
  integer codewords(63,MAX_NCW)
  integer itone(85)
  integer isync(22)
  type(q3list) callers(MAX_CALLERS)

  data isync/1,9,12,13,15,22,23,26,27,33,35,38,46,50,55,60,62,66,69,74,76,85/

  isgrid(g4)=g4(1:1).ge.'A' .and. g4(1:1).le.'R' .and. g4(2:2).ge.'A' .and. &
       g4(2:2).le.'R' .and. g4(3:3).ge.'0' .and. g4(3:3).le.'9' .and.       &
       g4(4:4).ge.'0' .and. g4(4:4).le.'9' .and. g4(1:4).ne.'RR73'

  call stdcall(hiscall,std)
  jmax=nhist2
  if(std .and. isgrid(hisgrid(1:4))) then
     jmax=min(MAX_CALLERS,nhist2+1)
     do j=1,nhist2
        if(callers(j)%call .eq. hiscall(1:6)) then
           jmax=nhist2
           exit
        endif
     enddo
  endif

  codewords(:,1)=0
  i=1
  do j=1,jmax
     c6=callers(j)%call
     g4=callers(j)%grid
     if(j.eq.nhist2+1) then
        c6=hiscall(1:6)
        g4=hisgrid(1:4)
     endif
     do k=1,5
        i=i+1
        msg=trim(mycall)//' '//trim(c6)
        j0=len(trim(msg))+1
        if(k.eq.1) msg=msg(1:j0)//g4
        if(k.eq.2) msg=msg(1:j0)//'R '//g4
        if(k.eq.3) msg(j0:j0+3)=' RRR'
        if(k.eq.4) msg(j0:j0+4)=' RR73'
        if(k.eq.5) msg(j0:j0+2)=' 73'
        call genq65(msg,0,msgsent,itone,i3,n3)
        i0=1
        jj=0
        do kk=1,85
           if(kk.eq.isync(i0)) then
              i0=i0+1
              cycle
           endif
           jj=jj+1
           codewords(jj,i)=itone(kk) - 1
        enddo
!        write(71,3001) i,j,k,codewords(1:13,i),trim(msg)
!3001    format(3i3,2x,13i3,2x,a)
     enddo
  enddo
  ncw=i

  return
end subroutine q65_set_list2
