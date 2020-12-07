program sumsim

! Sum a number of *.wav files so that multiple signals are present

  use wavhdr
  parameter (NMAX=60*12000)
  type(hdr) h                            !Header for the .wav file
  integer*2 iwave(NMAX)                  !i*2 data
  real wave(NMAX)                        !r*4 data
  character*80 fname

  nargs=iargc()
  if(nargs.lt.1) then
     print*,'Usage:    sumsim file1 [file2, ...]'
     go to 999
  endif
  wave=0.

  do ifile=1,nargs
     call getarg(ifile,fname)
     open(10,file=trim(fname),status='old',access='stream')
     read(10) h
     npts=h%ndata/2
     nfsample=h%nsamrate
     read(10) iwave(1:npts)
     n=len(trim(fname))
     wave(1:npts)=wave(1:npts) + iwave(1:npts)
     rms=sqrt(dot_product(wave(1:npts),wave(1:npts))/npts)
     write(*,1000) ifile,npts,float(npts)/nfsample,rms,fname(n-14:n)
1000 format(i3,i8,f6.1,f10.3,2x,a15)
     close(10)
  enddo

!  fac=1.0/sqrt(float(nargs))
  fac=1.0/nargs
  iwave(1:npts)=nint(fac*wave(1:npts))
  
  open(12,file='000000_0000.wav',access='stream',status='unknown')
  write(12) h,iwave(1:npts)
  close(12)

999 end program sumsim
