subroutine rjtty_sub(iwave,kz,nsps,f0,ftol)

  use jtty_mdec
  integer*2 iwave(kz)
  character*80 line,line1
  character*80 umsg
  logical synced,success,newsig
  data kz0/9999999/,f1good/-99./,xdtgood/-99./,missed_syncs/0/,newsig/.false./
  save istart,kz0,kchar,line,success,synced,f1good,xdtgood,missed_syncs,  &
       newsig,ndtol

  if(nsps.ne.240 .and. nsps.ne.320 .and. nsps.ne.384 .and. nsps.ne.480) return

  line1=""
  line1(1:1)=char(0)
  nframe = 53*nsps
  nchunk = nframe + nframe/4
  smin=3.0

  if(kz .le. kz0 ) then
     kz0=kz
     istart=1
     kchar=0
     line=""
     synced=.false.
     ndtol=0
     go to 999
  endif
  if(kz-istart+1 .lt. nchunk) return      ! wait for enough data

  nsync=0
  dmin=0.0         !Nonzero returned if OSD produced decode. Use to reject false decodes?
  do while (istart+nchunk-1 .le. kz)
     success=.false.
!     synced=.false.             ! uncomment this to disable use of prior sync 

     ndebug=-1  !### TEMPORARY ??? ###
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
           newsig = .false.
        endif
     endif

     if(success) then

        ndecodes = ndecodes + 1
        newsig = .false.
        if(abs(f1-f1good).gt.2.0 .or. abs(xdt-xdtgood).gt.0.004) then
           newsig = .true.
           kchar = 0
        endif
        f1good = f1
        xdtgood = xdt

!        tdecode=0
!        write(72,3072) istart,xdt,f1,snr,synced,success,newsig,kchar,   &
!             missed_syncs,nsync,nharderrors,dmin,tdecode,trim(umsg)
!3072    format(i8,f7.3,f7.1,f6.1,3L2,4i4,f7.1,f7.3,2x,a)

        if(umsg(1:4).eq.'599 ') umsg='~'//trim(umsg)
        n = len(trim(umsg))
        if(n.gt.79) n=79                 ! truncate at 80 chars
        do i=1,n
           if(umsg(i:i).eq.'~') umsg(i:i)=' '
        enddo
        if(newsig) then
           umsg=char(10)//trim(umsg(1:79))   ! Insert LF
           n=n+1
        endif
        line(kchar+1:kchar+n)=umsg(1:n)
        line(kchar+n+1:kchar+n+1)=char(0)
        kchar = min(kchar + n, 80)
        line1(1:n)=umsg(1:n)
        line1(n+1:n+1)=char(0)
        istart=istart+nframe
! Use this sync for next frame only if was strong strong
        if(nsync.lt.12) synced=.false.
     else
        istart=istart+nframe/4
        synced=.false.
     endif
     i0=index(line,' CQCQ ')
     if(i0.ge.6) line=line(1:i0+2)//' '//trim(line(i0+3:))
     line1=line
  enddo
  
  if(line1(1:1).eq.' ') line1=line1(2:)
  if(success .and. .not.newsig .and. line1(1:1).eq.char(10)) line1=line1(2:)

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
