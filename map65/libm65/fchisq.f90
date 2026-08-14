module fchisq_mod
  implicit none
contains

real function fchisq(cx, cy, npts, fsample, nflip, a, ccfmax, dtmax)
  use timer_module, only: timer
  use debug_log
  use ccf2_mod

  implicit none

  !==== Arguments ============================================================
  integer, intent(in)    :: npts, nflip
  complex, intent(in)    :: cx(npts), cy(npts)
  real,    intent(in)    :: fsample
  real,    intent(inout) :: a(5)
  real,    intent(out)   :: ccfmax, dtmax

  !==== Local variables ======================================================
  complex :: w, wstep, za, zb, z
  real,    allocatable :: ss(:)
  complex, allocatable :: csx(:), csy(:)

  integer :: i, j, k, lagpk
  integer :: ndiv, nout, nsph, nsps

  real :: dphi, dtstep, fac
  real :: p2, pol, s, twopi, x, x0, baud

  !==== Initialize outputs ===================================================
  ccfmax = 0.0
  dtmax  = 0.0

  !==== Constants ============================================================
  twopi = 6.283185307
  baud  = 11025.0 / 4096.0

  !==== Symbol timing ========================================================
  nsps   = nint(fsample / baud)
  nsph   = nsps / 2
  ndiv   = 16
  nout   = ndiv * npts / nsps
  dtstep = 1.0 / (ndiv * baud)

  !==== Allocate buffers =====================================================
  allocate(csx(0:npts), csy(0:npts))
  call timer('fchisq  ', 0)

  !===========================================================================
  !  MIX AND INTEGRATE (always recompute)
  !===========================================================================
  csx(0) = (0.0, 0.0)
  csy(0) = (0.0, 0.0)

  wstep = (1.0, 0.0)
  w     = (1.0, 0.0)
  x0    = 0.5*(npts + 1)
  s     = 2.0 / npts

  do i = 1, npts
     x = s*(i - x0)

     if (mod(i,100) == 1) then
        p2   = 1.5*x*x - 0.5
        dphi = (a(1) + x*a(2) + p2*a(3)) * (twopi/fsample)
        wstep = cmplx(cos(dphi), sin(dphi))
     end if

     w      = w * wstep
     csx(i) = csx(i-1) + w*cx(i)
     csy(i) = csy(i-1) + w*cy(i)
  end do

  !===========================================================================
  !  COMPUTE HALF-SYMBOL POWERS
  !===========================================================================
  fac = 1.0e-4
  pol = a(4) / 57.2957795
  allocate(ss(nout))
  ss = 0.0

  do i = 1, nout
     j = i*nsps/ndiv
     k = j - nsph

     if (k >= 1) then
        za = csx(j) - csx(k)
        zb = csy(j) - csy(k)
        z  = cos(pol)*za + sin(pol)*zb
        ss(i) = fac * (real(z)**2 + aimag(z)**2)
     end if
  end do

  !===========================================================================
  !  CORRELATE WITH SYNC PATTERN
  !===========================================================================
  call timer('ccf2    ', 0)
  call ccf2(ss, nout, nflip, ccfmax, lagpk)
  call timer('ccf2    ', 1)

  dtmax = lagpk * dtstep
  fchisq = -ccfmax

  !==== Cleanup ==============================================================
  deallocate(ss)
  deallocate(csx, csy)

  call timer('fchisq  ', 1)
end function fchisq

end module fchisq_mod

