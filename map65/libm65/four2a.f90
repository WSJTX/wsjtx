module four2a_mod
  use fftw3
  use iso_c_binding
  use fftw3_f77_interfaces

  implicit none
  
contains

  function loc_cplx(x) result(addr)
    use iso_c_binding, only: c_float_complex, c_intptr_t, c_loc
    implicit none
    complex(c_float_complex), intent(in), target :: x(*)
    integer(c_intptr_t) :: addr
    addr = transfer(c_loc(x), addr)
  end function loc_cplx

  recursive subroutine four2a(a, nfft, ndim, isign, iform) bind(C, name='four2a_')

    implicit none

    !==== Arguments ============================================================
    complex(c_float_complex), dimension(*), intent(inout) :: a
    integer(c_int), intent(in) :: nfft, ndim, isign, iform

    !==== Parameters ===========================================================
    integer, parameter :: NPMAX = 2100
    integer, parameter :: NSMALL = 16384

    !==== Local variables ======================================================
    complex(c_float_complex) :: aa(NSMALL)
    integer :: nn(NPMAX), ns(NPMAX), nf(NPMAX)
    integer(c_intptr_t) :: nl(NPMAX), nloc
    integer(c_intptr_t) :: plan(NPMAX)
    logical :: found_plan
    integer :: i, jz = 0, nflags
    integer :: nplan

    !==== Saved state ==========================================================
    save plan, nplan, nn, ns, nf, nl
    data nplan / 0 /

    integer :: dummy_unused
    dummy_unused = ndim


    !==== Early exit: destroy all plans =======================================
    if (nfft < 0) then
       !$omp critical(four2a)
       do i = 1, nplan
          !$omp critical(fftw)
          call sfftw_destroy_plan(plan(i))
          !$omp end critical(fftw)
       enddo
       nplan = 0
       !$omp end critical(four2a)
       return
    endif

    !==== Locate or create plan ===============================================
    nloc = loc_cplx(a)
    found_plan = .false.

    !$omp critical(four2a_setup)
    do i = 1, nplan
       if (nfft == nn(i) .and. isign == ns(i) .and. iform == nf(i) .and. nloc == nl(i)) then
          found_plan = .true.
          exit
       endif
    enddo

    if (.not. found_plan) then
       nplan = nplan + 1
       i = nplan

       nn(i) = nfft
       ns(i) = isign
       nf(i) = iform
       nl(i) = nloc

       nflags = FFTW_ESTIMATE

       if (nfft <= NSMALL) then
          jz = merge(nfft/2, nfft, iform == 0)
          aa(1:jz) = a(1:jz)
       endif

       !$omp critical(fftw)
       select case (iform)
       case (1)
          if (isign == -1) then
             call sfftw_plan_dft_1d(plan(i), nfft, a, a, FFTW_FORWARD, nflags)
          else
             call sfftw_plan_dft_1d(plan(i), nfft, a, a, FFTW_BACKWARD, nflags)
          endif
       case (0)
          call sfftw_plan_dft_r2c_1d(plan(i), nfft, a, a, nflags)
       case (-1)
          call sfftw_plan_dft_c2r_1d(plan(i), nfft, a, a, nflags)
       case default
          stop 'Unsupported request in four2a'
       end select
       !$omp end critical(fftw)

       if (nfft <= NSMALL) then
          a(1:jz) = aa(1:jz)
       endif
    endif
    !$omp end critical(four2a_setup)

    call sfftw_execute(plan(i))

  end subroutine four2a

end module four2a_mod
