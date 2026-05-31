! NOTE: This routine must remain a top-level Fortran subroutine.
! It has CHARACTER arguments, which require hidden length parameters
! that are passed automatically by the Fortran ABI.
! Keeping it outside any module preserves the legacy Fortran/C++ ABI
! and ensures the caller can continue to use the symbol name gen65_.

subroutine gen65_core(message, mode65, samfac, nsendingsh, msgsent, iwave, nwave)
  use iso_c_binding
  use packjt
  use chkmsg_mod
  use graycode_mod
  use rs_mod, only: rs_encode_
  use interleave63_mod
  implicit none

  !--------------------------------------------------------------------
  ! Parameters
  !--------------------------------------------------------------------
  integer, parameter :: NMAX = 2*60*11025

  !--------------------------------------------------------------------
  ! Arguments
  !--------------------------------------------------------------------
  character(len=22), intent(inout)    :: message
  integer,          intent(in)     :: mode65
  real(8),          intent(in)     :: samfac
  integer,          intent(out)    :: nsendingsh
  character(len=22), intent(out)   :: msgsent
  integer(2), intent(out) :: iwave(NMAX)
  integer,          intent(out)    :: nwave

  !--------------------------------------------------------------------
  ! Locals
  !--------------------------------------------------------------------
  character(len=3) :: cok
  real(8) :: dt, phi, f, f0, dfgen, dphi, twopi
  integer :: dgen(12)
  integer :: sent(63)
  logical :: first
  integer :: nprc(126)
  real    :: pr(126), flip
  integer :: nspecial
  integer :: nsym, nsps
  integer :: i, j, ii, k, itype
  real(8) :: xphi

  !--------------------------------------------------------------------
  ! Initialization
  !--------------------------------------------------------------------
  data nprc / &
     1,0,0,1,1,0,0,0,1,1,1,1,1,1,0,1,0,1,0,0, &
     0,1,0,1,1,0,0,1,0,0,0,1,1,1,0,0,1,1,1,1, &
     0,1,1,0,1,1,1,1,0,0,0,1,1,0,1,0,1,0,1,1, &
     0,0,1,1,0,1,0,1,0,1,0,0,1,0,0,0,0,0,0,1, &
     1,0,0,0,0,0,0,0,1,1,0,1,0,0,1,0,1,1,0,1, &
     0,1,0,1,0,0,1,1,0,0,1,0,0,1,0,0,0,0,1,1, &
     1,1,1,1,1,1 /
  data twopi / 6.283185307179586476d0 /
  data first /.true./
  save first, pr

  !--------------------------------------------------------------------
  ! Precompute PR sequence once
  !--------------------------------------------------------------------
  if (first) then
     do i = 1, 126
        pr(i) = 2*nprc(i) - 1
     end do
     first = .false.
  end if

  !--------------------------------------------------------------------
  ! Message classification
  !--------------------------------------------------------------------
  !write(*,*) 'GEN65 received message', message
  call chkmsg(message, cok, nspecial, flip)

  if (nspecial == 0) then
  
     call packmsg(message, dgen, itype)
     
     nsendingsh = 0
     if (iand(dgen(10), 8) /= 0) nsendingsh = -1

     call rs_encode_(dgen, sent)
     call interleave63(sent, 1)
     call graycode(sent, 63, 1)

     nsym = 126
     nsps = 4096
  else
     nsym = 32
     nsps = 16384
     nsendingsh = 1
  end if

  if (mode65 == 0) then
     nwave = 0
     return
  end if

  !--------------------------------------------------------------------
  ! Waveform generation
  !--------------------------------------------------------------------
  dt    = 1.d0 / (samfac * 11025.d0)
  f0    = 118 * 11025.d0 / 1024.d0
  dfgen = mode65 * 11025.d0 / 4096.d0
  phi   = 0.d0
  dphi  = twopi * dt * f0

  i = 0
  k = 0

  do j = 1, nsym
     if (message(1:5) /= '@TUNE') then
        f = f0
        if (nspecial /= 0 .and. mod(j,2) == 0) f = f0 + 10*nspecial*dfgen
        if (nspecial == 0 .and. flip*pr(j) < 0.0) then
           k = k + 1
           f = f0 + (sent(k) + 2)*dfgen
        end if
        dphi = twopi * dt * f
     end if

     do ii = 1, nsps
        phi = phi + dphi
        if (phi > twopi) phi = phi - twopi
        xphi = phi
        i = i + 1
        iwave(2*i-1) = 32767.0 * cos(xphi)
        iwave(2*i)   = 32767.0 * sin(xphi)
     end do
  end do

  !--------------------------------------------------------------------
  ! Finalize waveform
  !--------------------------------------------------------------------
  iwave(2*nsym*nsps+1:) = 0
  nwave = 2*nsym*nsps + 5512

  !--------------------------------------------------------------------
  ! Decode message for display
  !--------------------------------------------------------------------
   if (nspecial == 0) then
      call unpackmsg(dgen, msgsent)
      write(*,*) 'GEN65 msgsent is: ',msgsent
      if (flip < 0.0) then
         do i = 22, 1, -1
            if (msgsent(i:i) /= ' ') exit
         end do
         msgsent = msgsent(1:i) // ' OOO'
      end if
   else
      msgsent = '                      '   ! or leave as-is; will be overwritten below
   end if

   if (nsendingsh == 1) then
      if (nspecial == 2) msgsent = 'RO'
      if (nspecial == 3) msgsent = 'RRR'
      if (nspecial == 4) msgsent = '73'
   end if

end subroutine gen65_core

subroutine gen65_c(msg, mode65, samfac, nsendingsh, msgsent_c, iwave_c, nwave) &
  bind(C, name="gen65_")
  use iso_c_binding
  implicit none

  ! C-side arguments
  character(kind=c_char), dimension(*), intent(inout) :: msg
  integer(c_int),                      intent(in)     :: mode65
  real(c_double),                      intent(in)     :: samfac
  integer(c_int),                      intent(out)    :: nsendingsh
  character(kind=c_char), dimension(*),intent(out)    :: msgsent_c
  integer(c_short),  dimension(*),     intent(out)    :: iwave_c
  integer(c_int),                      intent(out)    :: nwave

  ! Fortran locals
  character(len=22) :: fmsg, fmsgsent
  integer(2), dimension(2*60*11025) :: fiwave
  integer :: i

  ! Copy C msg ? Fortran CHARACTER(22)
  fmsg = ' '
  do i = 1, 22
     if (msg(i) == c_null_char) exit
     fmsg(i:i) = achar(iachar(msg(i)))
  end do

  ! Call the real Fortran routine
  call gen65_core(fmsg, mode65, samfac, nsendingsh, fmsgsent, fiwave, nwave)

  ! Copy Fortran msgsent ? C buffer
  do i = 1, 22
     msgsent_c(i) = fmsgsent(i:i)
  end do
  msgsent_c(23) = c_null_char   ! null terminate

  ! Copy waveform
  do i = 1, nwave
     iwave_c(i) = fiwave(i)
  end do

end subroutine gen65_c


!does not require end module