subroutine rjtty_sub(iwave,kz,nsps,nfa,nfb,f0,ftol)

  use jtty_mdec
  integer*2 iwave(kz)
  data kz0/9999999/,missed_syncs/0/
  save istart,kz0,kchar,missed_syncs,ndtol

  if(nsps.ne.240 .and. nsps.ne.320 .and. nsps.ne.384 .and. nsps.ne.480) return

  nframe = 53*nsps
  nchunk = nframe + nframe/4
  smin=4.6

  if(kz .le. kz0 ) then
     kz0=kz
     istart=1
     kchar=0
     ndtol=0
     nslots=0
     go to 999
  endif
  if(kz-istart+1 .lt. nchunk) return      ! wait for enough data

  nsync=0
  dmin=0.0    !Nonzero returned if OSD successful. Use to reject false decodes?
  do while (istart+nchunk-1 .le. kz)
     ndebug=-1
     snr=-99.0
     call jtty_mdecode(istart,iwave(istart),nchunk,nsps,ndebug,nfa,nfb, &
          f0,ftol,smin)
     istart=istart+nframe/4
  enddo

999 return
end subroutine rjtty_sub

subroutine jtty_get_msgs(f0,ftol,all_new,qso_new,all_freqs,qso_freq)

  use jtty_mdec
  character*2400               :: all_freqs
  character*2400               :: all_freqs0 = " "
  character*800                :: qso_freq
  character*800                :: qso_freq0 = " "
  character*80 msg
  character*96 msg2
  integer indx(MAX_SLOTS)
  integer kall,kqso,nmsg,ncopy
  real f1(MAX_SLOTS)
  logical*1 all_new,qso_new
  save all_freqs0,qso_freq0

  f1(1:nslots)=slot(1:nslots)%f1
  call indexx(f1,nslots,indx)

  kall=1
  kqso=1
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
     nmsg=len_trim(msg2)
     ncopy=min(nmsg,len(all_freqs)-kall)
     if(ncopy.gt.0) then
        all_freqs(kall:kall+ncopy-1)=msg2(1:ncopy)
        kall=kall+ncopy
     endif

     if(abs(df).lt.ftol) then
        ncopy=min(nmsg,len(qso_freq)-kqso)
        if(ncopy.gt.0) then
           qso_freq(kqso:kqso+ncopy-1)=msg2(1:ncopy)
           kqso=kqso+ncopy
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
