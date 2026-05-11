module cgen65_mod
  implicit none
contains

subroutine cgen65(message,mode65,samfac,nsendingsh,msgsent,cwave,nwave)

  use iso_fortran_env, only: int8, real64
  use packjt
  use chkmsg_mod
  use graycode_mod
  use rs_mod
  use interleave63_mod, only: interleave63
  !--------------------------------------------------------------------
  ! Parameters and arguments
  !--------------------------------------------------------------------
  integer, parameter :: NMAX = 60*96000

  character(len=22),intent(inout)  :: message
  integer,          intent(in)  :: mode65
  real(real64),     intent(in)  :: samfac

  integer,          intent(out) :: nsendingsh
  character(len=22),intent(out) :: msgsent
  complex,          intent(out) :: cwave(NMAX)
  integer,          intent(out) :: nwave

  !--------------------------------------------------------------------
  ! Local variables (explicit typing)
  !--------------------------------------------------------------------
  character(len=3) :: cok
  real(real64) :: t, dt, phi, f, f0, dfgen, dphi, twopi, tsymbol
  real(real64) :: xphi
  real   :: pr(126), flip

  integer :: dgen(12)
  integer :: sent(63)
  integer :: nprc(126)
  integer :: nspecial, itype
  integer :: nsym, ndata
  integer :: i, j, j0, k

  logical :: first

  !--------------------------------------------------------------------
  ! DATA initialization (preserved exactly)
  !--------------------------------------------------------------------
  data nprc/1,0,0,1,1,0,0,0,1,1,1,1,1,1,0,1,0,1,0,0,  &
            0,1,0,1,1,0,0,1,0,0,0,1,1,1,0,0,1,1,1,1,  &
            0,1,1,0,1,1,1,1,0,0,0,1,1,0,1,0,1,0,1,1,  &
            0,0,1,1,0,1,0,1,0,1,0,0,1,0,0,0,0,0,0,1,  &
            1,0,0,0,0,0,0,0,1,1,0,1,0,0,1,0,1,1,0,1,  &
            0,1,0,1,0,0,1,1,0,0,1,0,0,1,0,0,0,0,1,1,  &
            1,1,1,1,1,1/
  data twopi /6.283185307179586476d0/
  data first /.true./
  save

  !--------------------------------------------------------------------
  ! First‑time initialization of pr()
  !--------------------------------------------------------------------
  if (first) then
     do i = 1, 126
        pr(i) = 2*nprc(i) - 1
     enddo
     first = .false.
  endif

  !--------------------------------------------------------------------
  ! Message packing / shorthand detection
  !--------------------------------------------------------------------
  call chkmsg(message, cok, nspecial, flip)

  if (nspecial .eq. 0) then
     call packmsg(message, dgen, itype)
     nsendingsh = 0
     if (iand(dgen(10),8) .ne. 0) nsendingsh = -1   ! Plain text flag

     call rs_encode_(dgen, sent)
     call interleave63(sent, 1)
     call graycode(sent, 63, 1)

     nsym    = 126
     tsymbol = 4096.d0 / 11025.d0
  else
     nsendingsh = 1
     nsym    = 32
     tsymbol = 16384.d0 / 11025.d0
  endif

  !--------------------------------------------------------------------
  ! Waveform generation constants
  !--------------------------------------------------------------------
  dt    = 1.d0 / (samfac * 96000.d0)
  f0    = 118 * 11025.d0 / 1024.d0
  dfgen = mode65 * 11025.d0 / 4096.d0

  t     = 0.d0
  phi   = 0.d0
  k     = 0
  j0    = 0

  ndata = nsym * 96000.d0 * samfac * tsymbol

  !--------------------------------------------------------------------
  ! Main waveform synthesis loop
  !--------------------------------------------------------------------
  do i = 1, ndata
     t = t + dt
     j = int(t / tsymbol) + 1

     if (j .ne. j0) then
        f = f0

        if (nspecial .ne. 0 .and. mod(j,2) .eq. 0) then
           f = f0 + 10 * nspecial * dfgen
        endif

        if (nspecial .eq. 0 .and. flip * pr(j) .lt. 0.d0) then
           k = k + 1
           f = f0 + (sent(k) + 2) * dfgen
        endif

        dphi = twopi * dt * f
        j0   = j
     endif

     phi = phi + dphi
     if (phi .gt. twopi) phi = phi - twopi

     xphi = phi
     cwave(i) = cmplx(cos(xphi), -sin(xphi))
  enddo

  !--------------------------------------------------------------------
  ! Pad and finalize
  !--------------------------------------------------------------------
  cwave(ndata+1:) = (0.0, 0.0)
  nwave = ndata + 48000

  call unpackmsg(dgen, msgsent)

  if (flip .lt. 0.d0) then
     do i = 22, 1, -1
        if (msgsent(i:i) .ne. ' ') exit
     enddo
     msgsent = msgsent(1:i) // ' OOO'
  endif

  if (nsendingsh .eq. 1) then
     if (nspecial .eq. 2) msgsent = 'RO'
     if (nspecial .eq. 3) msgsent = 'RRR'
     if (nspecial .eq. 4) msgsent = '73'
  endif

end subroutine cgen65

end module cgen65_mod
