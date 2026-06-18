! NOTE: This routine must remain a top-level Fortran subroutine.
! It has CHARACTER arguments, which require hidden length parameters
! that are passed automatically by the Fortran ABI.
! Keeping it outside any module preserves the legacy Fortran/C++ ABI
! and ensures the caller can continue to use the symbol name gen_q65_wave_.

subroutine gen_q65_wave(msg,ntxfreq,mode65,msgsent,iwave,nwave)

  use packjt
  use q65_encoding
  implicit none 

  integer, parameter :: NMAX = 2*60*11025

  !--------------------------------------------------------------------
  ! Arguments (must NOT use bind(C) because of CHARACTER arguments)
  !--------------------------------------------------------------------
  character*22, intent(in)  :: msg
  integer,      intent(in)  :: ntxfreq
  integer,      intent(in)  :: mode65
  character*22, intent(out) :: msgsent
  integer*2,    intent(out) :: iwave(NMAX)
  integer,      intent(out) :: nwave

  !--------------------------------------------------------------------
  ! Local character
  !--------------------------------------------------------------------
  character*37 :: msg37

  !--------------------------------------------------------------------
  ! Explicit REAL*8 variables (as in original)
  !--------------------------------------------------------------------
  real*8 :: t, dt, phi, f, f0, dfgen, dphi, twopi, tsym

  !--------------------------------------------------------------------
  ! Formerly implicit variables
  !--------------------------------------------------------------------
  integer :: i, j, j0, nsym, ndf, iz
  real    :: xphi

  integer :: codeword(65), itone(85)
  integer :: icos7(0:6)

  data icos7 /2,5,6,0,4,1,3/
  data twopi /6.283185307179586476d0/
  save

  !--------------------------------------------------------------------
  ! Logic (unchanged)
  !--------------------------------------------------------------------
   msgsent = msg
   msg37   = ''
   msg37(1:22) = msg

   ! Strip JT65-style OOO suffix if present
   if (len_trim(msg37) >= 4) then
      if (msg37(len_trim(msg37)-3:len_trim(msg37)) == ' OOO') then
         msg37(len_trim(msg37)-3:) = ' '
      end if
   end if

   call get_q65_tones(msg37, codeword, itone)

  nsym = 85
  tsym = 7200.d0/12000.d0
  dt   = 1.d0/11025.d0
  f0   = ntxfreq
  ndf  = 2**(mode65-1)
  dfgen = ndf*12000.d0/7200.d0
  phi  = 0.d0
  dphi = twopi*dt*f0
  i    = 0
  iz   = 85*7200*11025.d0/12000.d0
  t    = 0.d0
  j0   = 0

  do i = 1, iz
     t = t + dt
     j = t/tsym + 1.0
     if (j /= j0) then
        f    = f0 + itone(j)*dfgen
        dphi = twopi*dt*f
        j0   = j
     endif
     phi = phi + dphi
     if (phi > twopi) phi = phi - twopi
     xphi = real(phi)
     iwave(2*i-1) = 32767.0*cos(xphi)
     iwave(2*i)   = 32767.0*sin(xphi)
  enddo

  nwave = 2*iz

end subroutine gen_q65_wave

! does not require end module