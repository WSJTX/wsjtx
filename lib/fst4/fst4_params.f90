! FST4
! LDPC(240,101)/CRC24 code, five 8x4 sync

parameter (KK=77)                     !Information bits (77 + CRC24)
parameter (ND=120)                    !Data symbols
parameter (NS=40)                     !Sync symbols 
parameter (NN=NS+ND)                  !Sync and data symbols (160)
