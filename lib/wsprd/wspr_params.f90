parameter (NN=162)
parameter (NSPS0=8192)         !Samples per symbol at 12000 S/s
parameter (NDOWN=32)
parameter (NSPS=NSPS0/NDOWN)
parameter (NZ=NSPS*NN)         !Samples in waveform at 12000 S/s
parameter (NZ0=NSPS0*NN)       !Samples in waveform at 375 S/s
parameter (NMAX=120*12000)       !Samples in waveform at 375 S/s

! Define the sync vector:
integer*1 sync(162)
data sync/                                      &
     1,1,0,0,0,0,0,0,1,0,0,0,1,1,1,0,0,0,1,0,   &
     0,1,0,1,1,1,1,0,0,0,0,0,0,0,1,0,0,1,0,1,   &
     0,0,0,0,0,0,1,0,1,1,0,0,1,1,0,1,0,0,0,1,   &
     1,0,1,0,0,0,0,1,1,0,1,0,1,0,1,0,1,0,0,1,   &
     0,0,1,0,1,1,0,0,0,1,1,0,1,0,1,0,0,0,1,0,   &
     0,0,0,0,1,0,0,1,0,0,1,1,1,0,1,1,0,0,1,1,   &
     0,1,0,0,0,1,1,1,0,0,0,0,0,1,0,1,0,0,1,1,   &
     0,0,0,0,0,0,0,1,1,0,1,0,1,1,0,0,0,1,1,0,   &
     0,0/
