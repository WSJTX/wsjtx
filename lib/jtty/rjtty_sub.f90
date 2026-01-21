subroutine rjtty_sub(iwave,kz,line1)

  parameter (NFRAME=53*384)
  parameter (NCHUNK=NFRAME + NFRAME/4)
  integer*2 iwave(kz)
  character*(*) line1
  character*80 line
  character*80 umsg
  logical synced,success
  data kz0/9999999/
  save istart,kz0,kchar,line,success,synced,xdt,f1

  f0=1500.0
  ftol=50.0
  smin=2.0

  if(kz .le. kz0 ) then
     kz0=kz
     istart=1
     kchar=0
     line=""
     synced=.false.
     go to 999
  endif
  if(kz-istart+1 .lt. NCHUNK) return      ! wait for enough data

  nsync=0
  dmin=0.0            ! A nonzero value will be returned if OSD produces the decode. Use to reject false decodes?
  do while (istart+NCHUNK-1 .le. kz)
     success=.false.
!     synced=.false.                          ! uncomment this to disable use of prior sync 

     call jtty_decode(iwave(istart),NCHUNK,f0,ftol,smin,synced,xdt,f1,  &
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
        istart=istart+NFRAME
        if(nsync.lt.12) synced=.false.       ! don't use this sync for next frame if it's not strong
     else
        istart=istart+NFRAME/4
        synced=.false.
     endif
     i0=index(line,' CQCQ ')
     if(i0.ge.6) line=line(1:i0+2)//' '//trim(line(i0+3:))
     line1=line
  enddo

999 return
end subroutine rjtty_sub
