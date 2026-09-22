module jtty_fec

! JTTY's forward-error-correction configuration: the 13-symbol 4-FSK sync
! sequence and established TBCC/outer-check code.
! The profile and codec APIs are re-exported so JTTY callers need one module.

  use jtty_tbcc_code_profiles
  use tbcc

! The ACF of this 4FSK sync sequence has peak sidelobe level of 2/13
! and 46 such values over the entire 25x7 lag space
   integer :: is13(13) = [0,2,2,3,0,0,3,2,1,3,1,2,0]

! Bit 33 of the 34-bit payload (see jtty_mod.f90's pack_jtty/unpack_jtty) is
! reserved-zero in the current protocol. Assigning it requires a coordinated
! protocol revision. Bit 34 is the "last frame of this message" flag.
   integer, parameter :: JTTY_RESERVED_BIT   = 33

end module jtty_fec
