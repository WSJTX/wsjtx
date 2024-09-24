subroutine sfrx_sub(nyymmdd,nutc,nfqso,ntol,iwave)

  use sfox_mod
  use julian

  integer*2 iwave(NMAX)
  integer*8 secday,ntime8
  integer*1 xdec(0:49)
  character*13 foxcall
  complex c0(NMAX)                    !Complex form of signal as received
  real dd(NMAX)
  logical crc_ok
  data secday/86400/

  fsync=nfqso
  ftol=ntol
  fsample=12000.0
  call sfox_init(7,127,50,'no',fspread,delay,fsample,24)
  npts=15*12000

  if(nyymmdd.eq.-1) then
     ntime8=itime8()/30
     ntime8=30*ntime8
  else
     iyr=2000+nyymmdd/10000
     imo=mod(nyymmdd/100,100)
     iday=mod(nyymmdd,100)
     ih=nutc/10000
     im=mod(nutc/100,100)
     is=mod(nutc,100)
     ntime8=secday*(JD(iyr,imo,iday)-2440588) + 3600*ih + 60*im + is
  endif

  dd=iwave
  call sfox_remove_ft8(dd,npts)

  call sfox_ana(dd,npts,c0,npts)

  call sfox_remove_tone(c0,fsync)  ! Needs testing

  ndepth=3
  dth=0.5
  damp=1.0

  call qpc_decode2(c0,fsync,ftol, xdec,ndepth,dth,damp,crc_ok,   &
       snrsync,fbest,tbest,snr)
  if(crc_ok) then
     nsnr=nint(snr)
     nsignature = 1
     call sfox_unpack(nutc,xdec,nsnr,fbest-750.0,tbest,foxcall,nsignature)
  endif

  return
end subroutine sfrx_sub
