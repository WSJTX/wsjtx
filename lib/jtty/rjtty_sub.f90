subroutine rjtty_sub(iwave,kz,line1)

  parameter (nframe=53*384)
  parameter (nchunk=53*384+10176)
  integer*2 iwave(kz)
  character*(*) line1
  character*80 line
  character*80 umsg
  logical synced
  data kz0/9999999/
  save i0,kz0,kchar,line

  f0=1500.0
  ftol=50.0
  smin=0.5

  if(kz .le. kz0 ) then
     kz0=kz
     i0=1
     kchar=0
     line=""
     return
  endif

  if(kz-i0+1 .lt. nchunk) return      ! wait for more data 
  synced=.false.                      ! sync on every call for now
  call jtty_decode(iwave(i0),nchunk,f0,ftol,smin,synced,xdt,f1,snr,umsg)
  i0=i0+nframe/2

  if(synced) then
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
  endif

  line1=line

  return
end subroutine rjtty_sub
