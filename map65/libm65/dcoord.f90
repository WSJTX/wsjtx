module dcoord_mod
  implicit none
contains

subroutine dcoord(A0, B0, AP, BP, A1, B1, A2, B2)
  implicit none

  !--------------------------------------------------------------------
  ! Arguments (all double precision)
  !--------------------------------------------------------------------
  real(8), intent(in)  :: A0, B0, AP, BP
  real(8), intent(in)  :: A1, B1
  real(8), intent(out) :: A2, B2

  !--------------------------------------------------------------------
  ! Locals
  !--------------------------------------------------------------------
  real(8) :: SB0, CB0, SBP, CBP, SB1, CB1
  real(8) :: SB2, CB2, SAA, CAA, CBB, SBB
  real(8) :: SA2, CA2, TA2O2

  !--------------------------------------------------------------------
  ! Compute sines and cosines
  !--------------------------------------------------------------------
  SB0 = sin(B0)
  CB0 = cos(B0)
  SBP = sin(BP)
  CBP = cos(BP)
  SB1 = sin(B1)
  CB1 = cos(B1)

  !--------------------------------------------------------------------
  ! Core spherical rotation
  !--------------------------------------------------------------------
  SB2 = SBP*SB1 + CBP*CB1*cos(AP - A1)
  CB2 = sqrt(1.0d0 - SB2**2)
  B2  = atan(SB2 / CB2)

  SAA = sin(AP - A1) * CB1 / CB2
  CAA = (SB1 - SB2*SBP) / (CB2*CBP)

  CBB = SB0 / CBP
  SBB = sin(AP - A0) * CB0

  SA2 = SAA*CBB - CAA*SBB
  CA2 = CAA*CBB + SAA*SBB

  !--------------------------------------------------------------------
  ! atan2-like reconstruction using stable formula
  !--------------------------------------------------------------------
  TA2O2 = 0.0d0
  if (CA2 <= 0.0d0) TA2O2 = (1.0d0 - CA2) / SA2
  if (CA2 >  0.0d0) TA2O2 = SA2 / (1.0d0 + CA2)

  A2 = 2.0d0 * atan(TA2O2)

  if (A2 < 0.0d0) A2 = A2 + 6.2831853071795864d0

end subroutine dcoord

end module dcoord_mod
