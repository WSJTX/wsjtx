subroutine fillhashvar(numthreads,lfill)

  use packjt77 ! also setting mycall13var,dxcall13var
  use ft8_mod1, only : mycall,hiscall
  integer, intent(in) :: numthreads
  logical, intent(in) :: lfill
  character*13 cw

  if(lfill) then
    do i=1,numthreads
      do m=1,nlast_callsvar(i)
        nposition=nthrindexvar(i)+m
        cw=last_callsvar(nposition)
!print *,i,m,cw
        n10=ihashcallvar(cw,10)
        if(n10.ge.0 .and. n10 .le. 1023 .and. cw.ne.mycall13var) calls10var(n10)=cw

        n12=ihashcallvar(cw,12)
        if(n12.ge.0 .and. n12 .le. 4095 .and. cw.ne.mycall13var) calls12var(n12)=cw

        n22=ihashcallvar(cw,22)
        if(any(ihash22var.eq.n22)) then   ! If entry exists, make sure callsign is the most recently received one
          where(ihash22var.eq.n22) calls22var=cw
          go to 900
        endif

! New entry: move table down, making room for new one at the top
        ihash22var(nzhashvar:2:-1)=ihash22var(nzhashvar-1:1:-1)

! Add the new entry
        calls22var(nzhashvar:2:-1)=calls22var(nzhashvar-1:1:-1)
        ihash22var(1)=n22
        calls22var(1)=cw
        if(nzhashvar.lt.MAXHASHvar) nzhashvar=nzhashvar+1
900     continue
      enddo
    enddo
  else
    nlast_callsvar=0
    mycall13var=mycall//' '; dxcall13var=hiscall//' '
    if(mycall13var.ne.mycall13_0var) then
      if(len(trim(mycall13var)).gt.2) then
        mycall13_setvar=.true.
        mycall13_0var=mycall13var
        call save_hash_mycallvar(mycall13var,hashmy10var,hashmy12var,hashmy22var)
!print *,mycall13var,hashmy10var,hashmy12var,hashmy22var
      else
        mycall13_setvar=.false.
      endif
    endif

    if(dxcall13var.ne.dxcall13_0var) then
      if(len(trim(dxcall13var)).gt.2) then
        dxcall13_setvar=.true.
        dxcall13_0var=dxcall13var
        hashdx10var=ihashcallvar(dxcall13var,10)
! make sure new DX Call is stored in hash tables prior to decoding
! it is needed if manually callsign set in DX Call window was not decoded before
        call save_hash_callvar(dxcall13var,1)
!print *,dxcall13var,hashdx10var
      else
        dxcall13_setvar=.false.
      endif
    endif
  endif

  return
end subroutine fillhashvar
