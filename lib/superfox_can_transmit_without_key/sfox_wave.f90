subroutine sfox_wave(fname)

! Called by WSJT-X when it's time for SuperFox to transmit.  Reads array
! itone(1:151) from disk file 'sfox_2.dat' in the writable data directory.

  parameter (NWAVE=(160+2)*134400*4) !Max WSJT-X waveform (FST4-1800 at 48kHz)
  parameter (NN=151,NSPS=1024)
  character*(*) fname
  integer itone(151)
  real*8 dt,twopi,f0,baud,phi,dphi

  common/foxcom/wave(NWAVE)
  
  open(25,file=trim(fname),status='unknown',err=900)
  read(25,'(20i4)',err=900,end=900) itone
  close(25)

! Generate the SuperFox waveform.

  dt=1.d0/48000.d0
  twopi=8.d0*atan(1.d0)
  f0=750.0d0
  phi=0.d0
  baud=12000.d0/NSPS
  k=0
  do j=1,NN
     f=f0 + baud*mod(itone(j),128)
     dphi=twopi*f*dt
     do ii=1,4*NSPS
        k=k+1
        phi=phi+dphi
        xphi=phi
        wave(k)=sin(xphi)
     enddo
  enddo

900 continue

  return  
end subroutine sfox_wave
