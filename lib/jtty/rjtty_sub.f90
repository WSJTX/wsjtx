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
  ! moved (a new click) also restarts, even mid-buffer, so its slot table
  ! isn't polluted by a previous window's in-progress messages.
  if(kz .le. kz0 .or. istart0.ne.istart0_save) then
     kz0=kz
     istart=istart0
     kchar=0
     ndtol=0
     nslots=0
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

subroutine jtty_get_msgs(f0,ftol,all_new,qso_new,all_freqs,qso_freq,qso_eom, &
     all_tsync,qso_tsync,all_eom,all_slot_ids)

  use jtty_mdec
  character*2400               :: all_freqs
  character*2400               :: all_freqs0 = " "
  character*800                :: qso_freq
  character*800                :: qso_freq0 = " "
  character*80 msg
  character*96 msg2
  integer indx(MAX_SLOTS)
  integer kall,kqso,kqso_line,kall_line,nmsg,ncopy
  real f1(MAX_SLOTS)
  logical*1 all_new,qso_new
  logical*1, intent(out)       :: qso_eom(MAX_SLOTS)
  logical*1, intent(out)       :: all_eom(MAX_SLOTS)
  integer, intent(out)         :: all_slot_ids(MAX_SLOTS)
  real, intent(out)            :: all_tsync(MAX_SLOTS)
  real, intent(out)            :: qso_tsync(MAX_SLOTS)
  save all_freqs0,qso_freq0

  f1(1:nslots)=slot(1:nslots)%f1
  call indexx(f1,nslots,indx)

  kall=1
  kqso=1
  kqso_line=0
  kall_line=0
  qso_eom=.false.
  all_eom=.false.
  all_slot_ids=0
  all_tsync=0.0
  qso_tsync=0.0
  all_freqs=''
  qso_freq=''
  do ii=1,nslots
     i=indx(ii)
     msg=trim(slot(i)%decoded)
     df=slot(i)%f1 - f0
     ! A run of 5 tildes marks a missed-frame gap (decode_and_merge,
     ! jtty_mdecode.f90); " ... " is exactly 5 chars too, so this is a
     ! same-length in-place substitution. Any remaining lone tilde is the
     ! older single-char "implicit leading separator" marker, unchanged.
     do j=1,len_trim(msg)-4
        if(msg(j:j+4).eq.'~~~~~') msg(j:j+4)=' ... '
     enddo
     do j=1,len_trim(msg)
        if(msg(j:j).eq.'~') msg(j:j)=' '
     enddo
     if(msg(1:1).eq.' ') msg=trim(msg(2:))
     write(msg2,1000) nint(slot(i)%f1),trim(msg) // char(10)
1000 format(i4,2x,a)
     nmsg=len_trim(msg2)
     ncopy=min(nmsg,len(all_freqs)-kall)
     if(ncopy.gt.0) then
        all_freqs(kall:kall+ncopy-1)=msg2(1:ncopy)
        kall=kall+ncopy
        if(kall_line.lt.MAX_SLOTS) then
           kall_line=kall_line+1
           all_tsync(kall_line)=slot(i)%frame_tsync(1)
           all_eom(kall_line)=slot(i)%is_last_frame
           all_slot_ids(kall_line)=i
        endif
     endif

     if(abs(df).lt.ftol) then
        ncopy=min(nmsg,len(qso_freq)-kqso)
        if(ncopy.gt.0) then
           qso_freq(kqso:kqso+ncopy-1)=msg2(1:ncopy)
           kqso=kqso+ncopy
           if(kqso_line.lt.MAX_SLOTS) then
              kqso_line=kqso_line+1
              qso_eom(kqso_line)=slot(i)%is_last_frame
              qso_tsync(kqso_line)=slot(i)%frame_tsync(1)
           endif
        endif
     endif
  enddo
  all_freqs(kall:kall)=char(0)
  all_new = trim(all_freqs).ne.trim(all_freqs0)
  all_freqs0 = all_freqs

  qso_freq(kqso:kqso)=char(0)
  qso_new = trim(qso_freq).ne.trim(qso_freq0)
  qso_freq0 = qso_freq

  return
end subroutine jtty_get_msgs
