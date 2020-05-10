! WSPR4
! LDPC(174,74)/CRC24 code, four 4x4 Costas arrays for sync, ramp-up and ramp-down symbols

parameter (KK=50)                     !Information bits (50 + CRC24)
parameter (ND=87)                     !Data symbols
parameter (NS=16)                     !Sync symbols 
parameter (NN=NS+ND)                  !Sync and data symbols (103)
parameter (NN2=NS+ND+2)               !Total channel symbols (105)
parameter (NSPS=13312)                !Samples per symbol at 12000 S/s
parameter (NZ=NSPS*NN)                !Sync and Data samples (1,397,136)
parameter (NZ2=NSPS*NN2)              !Total samples in shaped waveform (1,397,760)
parameter (NMAX=408*3456)             !Samples in iwave (1,410,048)
parameter (NFFT1=4*NSPS, NH1=NFFT1/2) !Length of FFTs for symbol spectra
parameter (NSTEP=NSPS)                !Coarse time-sync step size
parameter (NHSYM=(NMAX-NFFT1)/NSTEP)  !Number of symbol spectra (1/4-sym steps)
parameter (NDOWN=32)                  !Downsample factor
