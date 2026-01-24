subroutine rjtty_sub(iwave,kz,nsps,f0,ftol,line1)

  integer*2 iwave(kz)
  character*(*) line1
  character*80 line
  character*80 umsg
  logical synced,success
  data kz0/9999999/
  save istart,kz0,kchar,line,success,synced,xdt,f1

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
     go to 999
  endif
  if(kz-istart+1 .lt. nchunk) return      ! wait for enough data

  nsync=0
  dmin=0.0         !Nonzero returned if OSD produced decode. Use to reject false decodes?
  do while (istart+nchunk-1 .le. kz)
     success=.false.
!     synced=.false.                  ! uncomment this to disable use of prior sync 

     call jtty_decode(iwave(istart),nchunk,nsps,f0,ftol,smin,synced,xdt,f1,  &
          snr,umsg,success,nharderrors,nsync,dmin)

     if(success) then
        if(umsg(1:4).eq.'599 ') umsg='~'//trim(umsg)
        n = len(trim(umsg))
        if(n.gt.79) n=79                 ! truncate at 80 chars
        do i=1,n
           if(umsg(i:i).eq.'~') umsg(i:i)=' '
        enddo
        if(kchar+n .gt. 79) kchar=0
        line(kchar+1:kchar+n)=umsg(1:n)
        line(kchar+n+1:kchar+n+1)=char(0)
        kchar = kchar + n
        line1(1:n)=umsg(1:n)
        line1(n+1:n+1)=char(0)
        istart=istart+nframe
        if(nsync.lt.12) synced=.false.       !Use this sync for next frame only if strong
     else
        istart=istart+nframe/4
        synced=.false.
     endif
     i0=index(line,' CQCQ ')
     if(i0.ge.6) line=line(1:i0+2)//' '//trim(line(i0+3:))
     line1=line
  enddo
  if(line1(1:1).eq.' ') line1=line1(2:)

999 return
end subroutine rjtty_sub
