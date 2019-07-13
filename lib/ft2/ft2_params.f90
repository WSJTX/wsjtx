! LDPC (128,90) code
parameter (KK=90)                     !Information bits (77 + CRC13)
parameter (ND=128)                    !Data symbols
parameter (NS=16)                     !Sync symbols (2x8)
parameter (NN=NS+ND)                  !Total channel symbols (144)
parameter (NSPS=160)                  !Samples per symbol at 12000 S/s
parameter (NZ=NSPS*NN)                !Samples in full 1.92 s waveform (23040)
parameter (NMAX=30000)                !Samples in iwave (2.5*12000)
parameter (NFFT1=400, NH1=NFFT1/2)    !Length of FFTs for symbol spectra
parameter (NSTEP=NSPS/4)              !Rough time-sync step size
parameter (NHSYM=NMAX/NSTEP-3)        !Number of symbol spectra (1/4-sym steps)
parameter (NDOWN=16)                  !Downsample factor
