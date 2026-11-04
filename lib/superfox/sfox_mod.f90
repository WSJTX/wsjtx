module sfox_mod
  
  parameter (NMAX=15*12000)       !Samples in iwave (180,000)
  integer, parameter :: MM=7          !Bits per symbol
  integer, parameter :: NQ=2**MM      !Q, number of MFSK tones
  integer, parameter :: NN=127        !Codeword length
  integer, parameter :: KK=50         !Number of information symbols
  integer, parameter :: NS=24         !Number of sync symbols
  integer, parameter :: NDS=NN+NS     !Total number of channel symbols
  integer, parameter :: NFZ=3         !First zero
  integer, parameter :: NSPS=1024     !Samples per symbol: the FFT size nearest 12.8*12000/NDS
  integer, parameter :: NSYNC=NS*NSPS !Samples in sync waveform
  integer, parameter :: NZ=NSPS*NDS   !Samples in full Tx waveform
  integer, parameter :: NFFT1=2*NSPS  !Length of FFTs for symbol spectra
  real, parameter :: baud=12000.0/NSPS
  real, parameter :: tsym=1.0/baud
  real, parameter :: bw=NQ*baud

contains
  subroutine sfox_init(mm0,nn0,kk0,itu,fspread,delay,fsample,ns0)

    character*2 itu

    if(mm0.ne.MM .or. nn0.ne.NN .or. kk0.ne.KK .or. ns0.ne.NS .or.            &
         fsample.ne.12000.0) error stop 'sfox_init: parameters differ from sfox_mod'

    fspread=0.0
    delay=0.0
    if(itu.eq.'LQ') then
       fspread=0.5
       delay=0.5
    else if(itu.eq.'LM') then
       fspread=1.5
       delay=2.0
    else if(itu.eq.'LD') then
       fspread=10.0
       delay=6.0
    else if(itu.eq.'MQ') then
       fspread=0.1
       delay=0.5
    else if(itu.eq.'MM') then
       fspread=0.5
       delay=1.0
    else if(itu.eq.'MD') then
       fspread=1.0
       delay=2.0
    else if(itu.eq.'HQ') then
       fspread=0.5
       delay=1.0
    else if(itu.eq.'HM') then
       fspread=10.0
       delay=3.0
    else if(itu.eq.'HD') then
       fspread=30.0
       delay=7.0
    endif

    return
  end subroutine sfox_init

end module sfox_mod
