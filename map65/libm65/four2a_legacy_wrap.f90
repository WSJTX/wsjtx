module four2a_legacy_wrap_mod
  use iso_c_binding
  use four2a_mod, only: four2a
  implicit none
contains

subroutine r2c_legacy(s_real, cs_half, nfft)
  use iso_c_binding
  use four2a_mod, only: four2a
  implicit none

  integer, intent(in) :: nfft
  real,    intent(in)  :: s_real(nfft)
  complex, intent(out) :: cs_half(0:nfft/2)

  integer :: nh, k
  integer(c_int) :: nfft_c, ndim_c, isign_c, iform_c

  nh = nfft/2

  block
     ! full complex buffer: nfft complex → 2*nfft reals
     real(c_float), target :: work(2*nfft)
     complex(c_float_complex), pointer :: work_c(:)

     call c_f_pointer(c_loc(work), work_c, [nfft])

     ! real input → first nfft reals
     work(1:nfft) = real(s_real(1:nfft), kind=c_float)

     nfft_c  = nfft
     ndim_c  = 1
     isign_c = 1
     iform_c = 0
     call four2a(work_c, nfft_c, ndim_c, isign_c, iform_c)

     ! packed half-spectrum: cs_half(0..nh) ↔ work_c(1..nh+1)
     do k = 0, nh
        cs_half(k) = cmplx(real(work_c(k+1)), aimag(work_c(k+1)))
     enddo
  end block
end subroutine r2c_legacy

subroutine c2r_legacy(cs_half, s_real, nfft)
  use iso_c_binding
  use four2a_mod, only: four2a
  implicit none

  integer, intent(in) :: nfft
  complex, intent(in)  :: cs_half(0:nfft/2)
  real,    intent(out) :: s_real(nfft)

  integer :: nh, k
  integer(c_int) :: nfft_c, ndim_c, isign_c, iform_c

  nh = nfft/2

  block
     real(c_float), target :: work(2*nfft)
     complex(c_float_complex), pointer :: work_c(:)

     call c_f_pointer(c_loc(work), work_c, [nfft])

     ! load packed half-spectrum: cs_half(0..nh) → work_c(1..nh+1)
     do k = 0, nh
        work_c(k+1) = cmplx(real(cs_half(k), kind=c_float), &
                            aimag(cs_half(k)), kind=c_float)
     enddo

     nfft_c  = nfft
     ndim_c  = 1
     isign_c = 1
     iform_c = -1
     call four2a(work_c, nfft_c, ndim_c, isign_c, iform_c)

     ! real output in first nfft reals
     s_real(1:nfft) = real(work(1:nfft))
  end block
end subroutine c2r_legacy


end module four2a_legacy_wrap_mod
