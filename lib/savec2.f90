integer function savec2(c2name,ntrseconds,f0m1500)

! Array c0() has complex samples at 1500 Hz sample rate.
! WSPR-2:  downsample by 1/4 to produce c2, centered at 1500 Hz

  parameter (NDMAX=120*1500)         !Sample intervals at 1500 Hz rate

  character*(*) c2name
  character*14 outfile
  real*8 f0m1500
  complex c0
  ! Keep FFT buffers at a stable address so four2a can reuse its plans.
  complex, allocatable, save :: c1(:),c2(:)
  common/c0com/c0(0:NDMAX-1)

  if(ntrseconds.ne.120) then
     savec2=-1
     return
  endif

  ntrminutes=2
  npts=114*1500
  nfft1=262144
  nfft2=65536
  if(.not.allocated(c1)) allocate(c1(0:nfft1-1),c2(0:nfft2-1))
  fac=1.0/nfft1
  c1(0:npts-1)=fac*c0(0:npts-1)
  c1(npts:nfft1-1)=0.

  call four2a(c1,nfft1,1,1,1)                 !Complex FFT to frequency domain

! Select the desired frequency range
  nh2=nfft2/2
  c2(0:nh2)=c1(0:nh2)
  c2(nh2+1:nfft2-1)=c1(nfft1-nh2+1:nfft1-1)

  call four2a(c2,nfft2,1,-1,1)      !Shorter complex FFT, back to time domain

! Write complex time-domain data to disk.
  i1=index(c2name,'.c2')
  outfile=c2name(i1-11:i1+2)
  open(18,file=c2name,status='replace',access='stream', iostat=ioerr)
  if (ioerr.eq.0) then
     write(18) outfile,ntrminutes,f0m1500,c2(0:45000-1)
     close(18)
  endif
  savec2 = ioerr
end function savec2
