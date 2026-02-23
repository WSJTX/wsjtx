subroutine rjtty_sub(iwave,kz,nsps,f0,ftol)

  use jtty_mdec
  integer*2 iwave(kz)
  character*80 umsg
  logical synced,success
  data kz0/9999999/,missed_syncs/0/
  save istart,kz0,kchar,success,synced,missed_syncs,ndtol

  if(nsps.ne.240 .and. nsps.ne.320 .and. nsps.ne.384 .and. nsps.ne.480) return

  nframe = 53*nsps
  nchunk = nframe + nframe/4
  smin=3.0

  if(kz .le. kz0 ) then
     kz0=kz
     istart=1
     kchar=0
     synced=.false.
     ndtol=0
     go to 999
  endif
  if(kz-istart+1 .lt. nchunk) return      ! wait for enough data

  nsync=0
  dmin=0.0    !Nonzero returned if OSD successful. Use to reject false decodes?
  do while (istart+nchunk-1 .le. kz)
     success=.false.
!     synced=.false.             ! uncomment this to disable use of prior sync 
     ndebug=-1
     snr=-99.0
     call jtty_mdecode(istart,iwave(istart),nchunk,nsps,ndebug,f0,ftol, &
              smin,synced,xdt,f1,snr,umsg,success,nharderrors,nsync,dmin)
     
     if(synced) then
        missed_syncs = 0
     else
        missed_syncs = missed_syncs + 1
        if(missed_syncs.ge.2) then
           f1good = -99.
           xdtgood = -99.
!           missed_syncs = 0
        endif
     endif

     if(success) then
        ndecodes = ndecodes + 1
        istart=istart+nframe
! Use this sync for next frame only if was strong strong
        if(nsync.lt.12) synced=.false.
     else
        istart=istart+nframe/4
        synced=.false.
     endif
  enddo

999 return
end subroutine rjtty_sub

subroutine jtty_get_msgs(f0,ftol,all_freqs,qso_freq)

  use jtty_mdec
  character*2400 all_freqs
  character*80 msg,msg2
  character*800 qso_freq
  integer indx(MAX_SLOTS)
  real f1(MAX_SLOTS)

  f1(1:nslots)=slot(1:nslots)%f1
  call indexx(f1,nslots,indx)

  k=1
  all_freqs=''
  qso_freq=''
  do ii=1,nslots
     i=indx(ii)
     msg=trim(slot(i)%decoded)
     df=slot(i)%f1 - f0
     do j=1,len_trim(msg)
        if(msg(j:j).eq.'~') msg(j:j)=' '
     enddo
     if(msg(1:1).eq.' ') msg=trim(msg(2:))
     write(msg2,1000) nint(slot(i)%f1),nint(slot(i)%snrdb - 20.0),  &
          trim(msg) // char(10)
1000 format(2i4,2x,a)
     all_freqs=trim(all_freqs) // trim(msg2)
     k=len_trim(all_freqs)+1

     if(abs(df).lt.ftol) then
        qso_freq=trim(qso_freq) // trim(msg2) !// char(10)
     endif
  enddo
  all_freqs=trim(all_freqs) // char(0)
  qso_freq=trim(qso_freq) // char(0)

  return
end subroutine jtty_get_msgs
