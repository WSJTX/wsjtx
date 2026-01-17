subroutine rjtty_sub(iwave,kz,line1)

  parameter (NFRAME=53*384)
  parameter (NCHUNK=NFRAME + NFRAME/4)
  integer*2 iwave(kz)
  character*(*) line1
  character*80 line
  character*80 umsg
  logical synced,success
  data kz0/9999999/
  save istart,kz0,kchar,line,success

  f0=1500.0
  ftol=50.0
  smin=1.0

  if(kz .le. kz0 ) then
     kz0=kz
     istart=1
     kchar=0
     line=""
     go to 999
  endif

  if(kz-istart+1 .lt. NCHUNK) return      ! wait for enough data
  do while (istart+NCHUNK-1 .le. kz)
     synced=.false.                          ! sync on every call for now
     success=.false.
     call jtty_decode(iwave(istart),NCHUNK,f0,ftol,smin,synced,xdt,f1,  &
          snr,umsg,success,nharderrors,nsync)

     if(success) then
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
     else
        istart=istart+NFRAME/4
     endif
     line1=line
  enddo

999 return
end subroutine rjtty_sub
