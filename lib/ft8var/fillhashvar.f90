subroutine fillhashvar(numthreads,lfill)

  use packjt77
  use ft8_mod1, only : mycall,hiscall
  integer, intent(in) :: numthreads
  logical, intent(in) :: lfill
  character*13 cw

  if(lfill) then
    ! End of a configured/var decode pass: merge per-thread successful decode
    ! effects into the shared callsign hash and recent-call state.
    do i=1,numthreads
      do m=1,nqueued_calls_by_thread(i)
        nposition=thread_call_index(i)+m
        cw=queued_calls_by_thread(nposition)
!print *,i,m,cw
        call save_hash_call(cw,n10,n12,n22)
      enddo
    enddo
    call fold_queued_recent_calls(numthreads)
  else
    ! Start of a configured/var decode pass: clear staged worker effects and
    ! snapshot the operator-configured calls used by hash-only decode guards.
    nqueued_calls_by_thread=0
    nqueued_recent_calls_by_thread=0
    mycall13=mycall//' '; dxcall13=hiscall//' '
    call sync_configured_calls_for_decode_start()
  endif

  return
end subroutine fillhashvar
