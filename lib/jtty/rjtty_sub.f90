subroutine rjtty_sub(iwave,kz,line1)

  parameter (nframe=53*384)
  parameter (nchunk=53*384+7680)
  integer*2 iwave(kz)
  character*(*) line1
  character*80 line
  character*80 umsg
  logical synced
  data kz0/9999999/
  save i0,kz0,kchar,line

  f0=1500.0
  ftol=50.0
  smin=0.

  if(kz .le. kz0 ) then
     kz0=kz
     i0=1
     kchar=0
     line=""
     return
  endif

  write(*,*) 'rjtty_sub ',kz0,kz,i0,kz-i0+1
  if(kz-i0+1 .lt. nchunk) return      ! wait for more data 
  synced=.false.                      ! sync on every call for now
  call jtty_decode(iwave(i0),nchunk,f0,ftol,smin,synced,xdt,f1,snr,umsg)
  i0=i0+nframe/4

  if(synced) then
     n = len(trim(umsg))
     do i=1,n
        if(umsg(i:i).eq.'~') umsg(i:i)=' '
        line(kchar+i:kchar+i) = umsg(i:i)
     enddo
     kchar = kchar + n
     if(kchar.lt.80) then
        write(line1,'(a)') umsg(1:n)
     endif
     line1(n+1:n+1)=char(0)
     line(kchar+1:kchar+1)=char(0)
  endif

900 continue
  if(kchar.gt.0) then
  endif
  line1=line

  return
end subroutine rjtty_sub
