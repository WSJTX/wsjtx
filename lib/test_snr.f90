program test_snr

! Test an algorithm for measuring SNR of EME echoes.

  use wavhdr
  parameter (NMAX=27648)           !27*1024
  parameter (NFFT=32768,NH=NFFT/2)
  parameter (NZ=4096)

  type(hdr) h                            !Header for the .wav file
  integer*2 id2(NMAX)                 !Buffer for Rx data  
  complex c(0:NH)
  real x(NFFT)
  real s(8192)
  real sa(4096)
  real sb(4096)
  real red(4096)
  real blue(4096)
  character*80 infile
  equivalence (x,c)

  nargs=iargc()
  if(nargs.lt.1) then
     print*,'Usage:    test_snr <infile>'
     go to 999
  endif
  call getarg(1,infile)
  i0=index(infile,'_')
  read(infile(1:i0-1),*) fspread0                    !Generated Doppler spread
  read(infile(i0+1:i0+3),*) snrdb0                   !Generated SNR
  open(10,file=trim(infile),status='old',access='stream')
  read(10) h
  npts=h%ndata/2
  npings=npts/NMAX
  nfsample=h%nsamrate
  df=12000.0/NFFT
  s=0.
  fac=1.0/NMAX
  
  do iping=1,npings
     read(10) id2(1:NMAX)
     x(1:NMAX)=fac*id2(1:NMAX)
     x(NMAX+1:)=0.     
     call four2a(x,NFFT,1,-1,0)
     do i=1,8192                             !Accumulate spectrum 0 - 3 kHz
        s(i)=s(i) + real(c(i))**2 + aimag(c(i))**2
     enddo
  enddo

  sa=s(2049:6144)
  sb=s(2049:6144)

  call echo_snr(sa,sb,fspread0,blue,red,snrdb,db_err,fpeak,snr_detect)
  
  nqual=min(10,int(snr_detect-4.0))

  write(*,1010) fspread0,snrdb0,snrdb,snrdb-snrdb0,db_err,fpeak,   &
       snr_detect,nqual

1010 format(5f6.1,2f7.1,i4)

  do i=1,8192
     write(12,1100) i*df,s(i)
1100 format(f10.3,e15.6)
  enddo

999 end program test_snr
