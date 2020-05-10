! FT4S280
! LDPC(280,101)/CRC24 code, six 4x4 Costas arrays for sync, ramp-up and ramp-down symbols

parameter (KK=77)                     !Information bits (77 + CRC24)
parameter (ND=140)                    !Data symbols
parameter (NS=24)                     !Sync symbols 
parameter (NN=NS+ND)                  !Sync and data symbols (164)
parameter (NN2=NS+ND+2)               !Total channel symbols (166)
parameter (NSPS=8400)                 !Samples per symbol at 12000 S/s
parameter (NZ=NSPS*NN)                !Sync and Data samples (1,377,600)
parameter (NZ2=NSPS*NN2)              !Total samples in shaped waveform (1,394,400)
parameter (NMAX=408*3456)             !Samples in iwave (1,410,048)
parameter (NFFT1=4*NSPS, NH1=NFFT1/2) !Length of FFTs for symbol spectra
parameter (NSTEP=NSPS)                !Coarse time-sync step size
parameter (NHSYM=(NMAX-NFFT1)/NSTEP)  !Number of symbol spectra (1/4-sym steps)
parameter (NDOWN=28)                  !Downsample factor
