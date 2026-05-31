module fftw3_f77_interfaces
  use iso_c_binding, only: c_int, c_intptr_t, c_float_complex, c_float
  implicit none

  interface
    subroutine sfftw_destroy_plan(plan)
      import :: c_intptr_t
      implicit none
      integer(c_intptr_t) :: plan
    end subroutine sfftw_destroy_plan

    subroutine sfftw_plan_dft_1d(plan, n, in, out, sign, flags)
      import :: c_int, c_intptr_t, c_float_complex
      implicit none
      integer(c_intptr_t) :: plan
      integer(c_int)      :: n, sign, flags
      complex(c_float_complex) :: in(*), out(*)
    end subroutine sfftw_plan_dft_1d

   subroutine sfftw_plan_dft_r2c_1d(plan, n, in, out, flags)
    import :: c_int, c_intptr_t, c_float_complex
    implicit none
    integer(c_intptr_t) :: plan
    integer(c_int)      :: n, flags
    complex(c_float_complex) :: in(*), out(*)
  end subroutine sfftw_plan_dft_r2c_1d

  subroutine sfftw_plan_dft_c2r_1d(plan, n, in, out, flags)
    import :: c_int, c_intptr_t, c_float_complex
    implicit none
    integer(c_intptr_t) :: plan
    integer(c_int)      :: n, flags
    complex(c_float_complex) :: in(*), out(*)
  end subroutine sfftw_plan_dft_c2r_1d

    subroutine sfftw_execute(plan)
      import :: c_intptr_t
      implicit none
      integer(c_intptr_t) :: plan
    end subroutine sfftw_execute
  end interface

end module fftw3_f77_interfaces

