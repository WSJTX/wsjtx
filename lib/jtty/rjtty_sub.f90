subroutine rjtty_sub(iwave,kz,nsps,nfa,nfb,f0,ftol)
  ! Unwindowed entry point: scans the whole buffer, exactly as before.
  integer*2 iwave(kz)
  call rjtty_core(iwave,kz,nsps,nfa,nfb,f0,ftol,1,kz)
end subroutine rjtty_sub

subroutine rjtty_sub_windowed(iwave,kz,nsps,nfa,nfb,f0,ftol,istart0,istop)
  ! Bounds the scan to [istart0,istop] (sample indices into iwave) instead
  ! of the whole buffer -- e.g. a WideGraph click-driven re-decode that only
  ! needs a window around a known time, not a full "decode again" pass.
  integer*2 iwave(kz)
  integer, intent(in) :: istart0,istop
  call rjtty_core(iwave,kz,nsps,nfa,nfb,f0,ftol,istart0,istop)
end subroutine rjtty_sub_windowed

subroutine rjtty_core(iwave,kz,nsps,nfa,nfb,f0,ftol,istart0,istop)

  use jtty_mdec
  integer*2 iwave(kz)
  integer, intent(in) :: istart0,istop
  data kz0/9999999/,missed_syncs/0/,istart0_save/0/
  save istart,kz0,kchar,missed_syncs,ndtol,istart0_save

  if(nsps.ne.240 .and. nsps.ne.320 .and. nsps.ne.384 .and. nsps.ne.480) return

  nframe = 59*nsps
  nchunk = nframe + nframe/4
  smin=4.6

  ! A shorter/new buffer always restarts; a windowed call whose window has
  ! moved (a new click) also restarts, even mid-buffer, so active assemblies
  ! are not polluted by a previous window's in-progress messages.
  if(kz .le. kz0 .or. istart0.ne.istart0_save) then
     kz0=kz
     istart=istart0
     kchar=0
     ndtol=0
     call reset_decode_search_state()
     istart0_save=istart0
     go to 999
  endif
  kzeff=min(kz,istop)
  if(kzeff-istart+1 .lt. nchunk) return    ! wait for enough data

  nsync=0
  do while (istart+nchunk-1 .le. kzeff)
     ndebug=-1
     snr=-99.0
     call jtty_mdecode_step(iwave,kz,istart,istart0,nchunk,nsps,ndebug,nfa,nfb, &
          f0,ftol,smin)
     istart=istart+nframe/4
  enddo

999 return
end subroutine rjtty_core

subroutine jtty_get_updates(text_blocks,message_ids,frequencies,start_tsync,eom,count)

  use iso_fortran_env, only: int64
  use jtty_mdec
  integer, parameter            :: BATCH_SIZE = 30
  integer, parameter            :: MESSAGE_LENGTH = 80
  character(len=BATCH_SIZE*MESSAGE_LENGTH), intent(out) :: text_blocks
  integer(int64), intent(out)   :: message_ids(BATCH_SIZE)
  real, intent(out)             :: frequencies(BATCH_SIZE)
  real, intent(out)             :: start_tsync(BATCH_SIZE)
  logical*1, intent(out)        :: eom(BATCH_SIZE)
  integer, intent(out)          :: count
  character(len=MESSAGE_LENGTH) :: msg
  integer :: i,index,offset

  text_blocks=''
  message_ids=0_int64
  frequencies=0.0
  start_tsync=0.0
  eom=.false.
  count=min(npending,BATCH_SIZE)

  do i=1,count
     index=pending_first+i-1
     msg=display_message_text(pending_updates(index)%decoded)
     offset=(i-1)*MESSAGE_LENGTH
     text_blocks(offset+1:offset+MESSAGE_LENGTH)=msg
     message_ids(i)=pending_updates(index)%message_id
     frequencies(i)=pending_updates(index)%f1
     start_tsync(i)=pending_updates(index)%start_tsync
     eom(i)=pending_updates(index)%complete
  enddo

  ! Pending membership is the delivery guarantee; remove records only after copying them out.
  pending_first=pending_first+count
  npending=npending-count
  if(npending.eq.0) then
     pending_first=1
     if(allocated(pending_updates)) then
        if(size(pending_updates).gt.MAX_ACTIVE_MESSAGES) then
           deallocate(pending_updates)
           allocate(pending_updates(MAX_ACTIVE_MESSAGES))
        endif
     endif
  endif
end subroutine jtty_get_updates
