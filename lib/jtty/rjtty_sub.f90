subroutine rjtty_sub(iwave,kz,line1)

  integer*2 iwave(kz)
  character*(*) line1
  character*80 umsg
  logical synced
  data kz0/9999999/
  save nframe,kzmin,kz0,kchar

  f0=1500.0
  ftol=50.0
  smin=0.

  if(kz.lt.kz0) then
     nframe=53*384+7680
     kzmin=nframe
     kchar=0
  endif
  kz0=kz
  if(kz .lt. kzmin) return
  kzmin = kzmin + 53*384
  ibuf=(kz-7680)/(53*384)
  i0=(ibuf-1)*53*384 + 1
!  print*,ibuf,i0
  if(kz-i0 .lt. nframe/2) go to 900
  synced=.false.                      ! sync on evey call for now
  write(81,3081) ibuf,i0,iwave(i0:i0+4)
3081 format(i2,i8,3x,5i6)
  call jtty_decode(iwave(i0),nframe,f0,ftol,smin,synced,xdt,f1,snr,umsg)
  write(82,3082) synced,xdt,f1,snr,trim(umsg)
3082 format(L1,f8.3,2f7.1,2x,a)
  if(synced) then
     n = len(trim(umsg))
     do i=1,n
        if(umsg(i:i).eq.'~') umsg(i:i)=' '
     enddo
     kchar = kchar + n
     if(kchar.lt.80) then
        write(line1,'(a)') umsg(1:n)
!     else
!        write(line1,'(a)') umsg(1:n)
!        write(line1,*) 'debug ',umsg(1:n)
     endif
  endif

900 continue
!  write(*,*) trim(line1)
  write(83,*) trim(line1)

  return
end subroutine rjtty_sub
