module gen_q65_cwave_mod
  implicit none
contains

subroutine gen_q65_cwave(msg,ntxfreq,ntone_spacing,msgsent,cwave,nwave)

  use packjt
  use q65_encoding
  use iso_fortran_env, only: real64

  integer, parameter :: NMAX = 60*96000

  !--------------------------------------------------------------------
  ! Arguments (with correct intent and explicit types)
  !--------------------------------------------------------------------
  character*24, intent(in)  :: msg
  integer,      intent(in)  :: ntxfreq
  integer,      intent(in)  :: ntone_spacing
  character*24, intent(out) :: msgsent
  complex,      intent(out) :: cwave(NMAX)
  integer,      intent(out) :: nwave

  !--------------------------------------------------------------------
  ! Local character variables
  !--------------------------------------------------------------------
  character*37 :: msg37

  !--------------------------------------------------------------------
  ! Explicit declarations for all formerly implicit variables
  !--------------------------------------------------------------------
  ! Implicit REAL(4) variables (A–H, O–Z)
  real(real64) :: t, dt, phi, f, f0, dfgen, dphi, twopi, tsym, xphi

  ! Implicit INTEGER variables (I–N)
  integer :: i, j, j0, nsym

  ! Explicit locals
  integer :: codeword(65), itone(85)
  integer :: icos7(0:6)

  data icos7 /2,5,6,0,4,1,3/
  data twopi /6.283185307179586476d0/
  save

  !--------------------------------------------------------------------
  ! Begin logic (unchanged from legacy)
  !--------------------------------------------------------------------
  msgsent = msg
  msg37 = ''
  msg37(1:24) = msg

  call get_q65_tones(msg37, codeword, itone)

  ! Constants
  nsym = 85
  tsym = 7200.d0 / 12000.d0
  dt   = 1.d0 / 96000.d0
  f0   = ntxfreq
  dfgen = ntone_spacing * 12000.d0 / 7200.d0

  phi = 0.d0
  dphi = twopi * dt * f0

  nwave = 85 * 7200 * 96000.d0 / 12000.d0

  t = 0.d0
  j0 = 0

  do i = 1, nwave
     t = t + dt
     j = t/tsym + 1
     if (j > 85) exit

     if (j /= j0) then
        f = f0 + itone(j) * dfgen
        dphi = twopi * dt * f
        j0 = j
     endif

     phi = phi + dphi
     if (phi > twopi) phi = phi - twopi

     xphi = phi
     cwave(i) = cmplx(cos(xphi), -sin(xphi))
  enddo

end subroutine gen_q65_cwave

end module gen_q65_cwave_mod
