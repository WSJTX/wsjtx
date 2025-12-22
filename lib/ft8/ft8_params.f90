! LDPC (174,91) code
parameter (KK=91)                     !Information bits (77 + CRC14)
parameter (ND=58)                     !Data symbols
parameter (NS=21)                     !Sync symbols (3 @ Costas 7x7)
parameter (NN=NS+ND)                  !Total channel symbols (79)
parameter (NSPS=1920)                 !Samples per symbol at 12000 S/s
parameter (NZ=NSPS*NN)                !Samples in full 15 s waveform (151,680)
parameter (NMAX=15*12000)             !Samples in iwave (180,000)
parameter (NFFT1=2*NSPS, NH1=NFFT1/2) !Length of FFTs for symbol spectra
parameter (NSTEP=NSPS/4)              !Rough time-sync step size
parameter (NHSYM=NMAX/NSTEP-3)        !Number of symbol spectra (1/4-sym steps)
parameter (NDOWN=60)                  !Downsample factor
