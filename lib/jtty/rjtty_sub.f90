subroutine rjtty_sub(iwave,kz,line1)

  integer*2 iwave(kz)
  character*(*) line1
  character*80 line
  character*80 umsg
  logical synced
  data kz0/9999999/
  save nframe,kzmin,kz0,kchar,line

  f0=1500.0
  ftol=50.0
  smin=0.

  if(kz.lt.kz0) then
     nframe=53*384+7680
     kzmin=nframe
     kchar=0
     line=""
  endif
  kz0=kz
  if(kz .lt. kzmin) return
  kzmin = kzmin + 53*384
  ibuf=(kz-7680)/(53*384)
  i0=(ibuf-1)*53*384 + 1
  if(kz-i0 .lt. nframe/2) go to 900
  synced=.false.                      ! sync on evey call for now
  call jtty_decode(iwave(i0),nframe,f0,ftol,smin,synced,xdt,f1,snr,umsg)
  if(synced) then
     n = len(trim(umsg))
     do i=1,n
        if(umsg(i:i).eq.'~') umsg(i:i)=' '
        line(kchar+i:kchar+i) = umsg(i:i)
!        print*,'AAA',i,kchar+i,umsg(i:i),'  ',line(1:kchar+i)
     enddo
     kchar = kchar + n
     if(kchar.lt.80) then
        write(line1,'(a)') umsg(1:n)
     endif
     line1(n+1:n+1)=char(0)
     line(kchar+1:kchar+1)=char(0)
  endif

900 continue
!  line(kchar+1:kchar+1)=char(0)
  if(kchar.gt.0) then
!     print*,'aa',n,line1(1:n)
!     print*,'bb',kchar,line(1:kchar)
  endif
  line1=line

  return
end subroutine rjtty_sub
