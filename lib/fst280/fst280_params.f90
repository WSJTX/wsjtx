! FT4S280
! LDPC(280,101)/CRC24 code, six 4x4 Costas arrays for sync, ramp-up and ramp-down symbols

parameter (KK=77)                     !Information bits (77 + CRC24)
parameter (ND=140)                    !Data symbols
parameter (NS=24)                     !Sync symbols 
parameter (NN=NS+ND)                  !Sync and data symbols (164)
parameter (NN2=NS+ND+2)               !Total channel symbols (166)
