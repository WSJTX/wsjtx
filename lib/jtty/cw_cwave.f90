subroutine cw_cwave(nsc,csync,cwave)

! Generate complex waveforms for the 64 possible JTTY characters.

  parameter (MAX_TONES=85*7)                !Max number of channel symbols
  parameter (MAX_WAVE=20*12000)             !Max length of iwave()
  integer nsc                               !Samples per character @6000 Hz
  complex cwave(nsc,0:63)                   !Complex waveform for each character
  complex csync(0:2*nsc-1)
  integer itone(MAX_TONES)                  !Channel symbols (tone frequencies)
  integer icos7(7)                          !Costas array
  integer ib13(13)                          !Barker 13 sequence
  real wave(nsc)
  include 'jtty_codewords.f90'
  data icos7/2,5,6,0,4,1,3/                 !Values for 7x7 Costas array
  data ib13/0,0,0,0,0,1,1,0,0,1,0,1,0/      !Barker 13 sequence

! Printable characters and their index (0-63)
!                      1         2         3         4         5         6
!            0123456789012345678901234567890123456789012345678901234567890123
! Printable: 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ +-./?
! Shorthands:                                          !@#$%^&*()_`=[]{}<>|:;

  fsample=6000.0
  nsps=192                          !Samples per symbol @6000 Hz
  nsym=7                            !One character is 7 3-bit symbols
  bt=2.0                            !Default bt=2 (smaller ==> more smoothing)
!  f0=1500.0                         !Frequency of lowest tone
  f0=0.0                            !Frequency of lowest tone
  nwave=nsps*nsym                   !Length of i*2 data written to *.wav file
  icmplx=1

!  itone(1:7)=icos7
!  itone(8:14)=icos7

  itone(1:13)=ib13
  itone(14)=7

! NB: csync is twice as long as the single-character cwave functions.
  call gen_jttywave(itone,2*nsym,nsps,bt,fsample,f0,csync,wave,icmplx,2*nwave)
     
  do icw=0,63                  !Loop over all codewords
     n=cw(icw)
     do j=1,7                  !Get the 7 values of itone() for this character
        itone(j)=iand(n,7)
        n=n/8
     enddo
     call gen_jttywave(itone,nsym,nsps,bt,fsample,f0,cwave(1,icw),   &
          wave,icmplx,nwave)
  enddo
  
  return
end subroutine cw_cwave
