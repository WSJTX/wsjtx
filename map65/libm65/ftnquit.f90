subroutine ftnquit
  use filbig_mod
  implicit none
  
  ! Local copy of filbig's MAXFFT2 for dummy arrays
  integer, parameter :: MAXFFT2 = 77175


  ! Dummy placeholders for filbig
  real*4        :: dd_dummy(1,1)
  integer       :: nmax_dummy = -1
  real*8        :: f0_dummy = 0.0d0
  integer       :: newdat_dummy = 0
  integer       :: nfsample_dummy = 0
  logical       :: xpol_dummy = .false.
  complex       :: c4a_dummy(MAXFFT2), c4b_dummy(MAXFFT2)
  integer       :: n4_dummy = 0

  ! Dummy placeholder for four2a
  complex :: a_dummy(1)

  ! Destroy FFTW plans for four2a
  call four2a(a_dummy, -1, 1, 1, 1)

  ! Destroy FFTW plans for filbig
  call filbig(dd_dummy, nmax_dummy, f0_dummy, newdat_dummy, &
              nfsample_dummy, xpol_dummy, c4a_dummy, c4b_dummy, n4_dummy)

end subroutine ftnquit


