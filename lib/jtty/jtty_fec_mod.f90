module jtty_fec

! JTTY's forward-error-correction configuration: the 13-symbol 4-FSK sync
! sequence, and the WAVA decode parameters for the tail-biting rate-1/2
! convolutional code (see lib/jtty/tbcc.f90). `use tbcc` here re-exports
! tbcc's public API (PAYLOAD_BITS, TOTAL_K, tbcc_init, tbcc_encode,
! tbcc_wava_fsk_decode, encode_crc12, ...) to anyone who does `use jtty_fec`,
! so existing call sites don't need a second use statement.

  use tbcc

! The ACF of this 4FSK sync sequence has peak sidelobe level of 2/13
! and 46 such values over the entire 25x7 lag space
   integer :: is13(13) = [0,2,2,3,0,0,3,2,1,3,1,2,0]

! JTTY's fixed WAVA decode configuration: K=10 (nu=9) tail-biting
! convolutional code, list size 4, 2 trellis wraps. Bit 33 of the 34-bit
! payload (see jtty_mod.f90's pack_jtty/unpack_jtty) is always transmitted
! as 0 and passed to tbcc_wava_fsk_decode as reserved_zero_bit, an extra
! free check beyond the 12-bit CRC. Bit 34 is the real "last frame of this
! message" flag, not a reserved bit.
   integer, parameter :: JTTY_WAVA_NU        = 9
   integer, parameter :: JTTY_WAVA_L         = 4
   integer, parameter :: JTTY_WAVA_ITERS     = 2
   integer, parameter :: JTTY_RESERVED_BIT   = 33

end module jtty_fec
