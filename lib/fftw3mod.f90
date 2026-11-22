module FFTW3
  use, intrinsic :: iso_c_binding
  include 'fftw3.f03'

  interface
     function wsjt_fftwf_plan_dft_1d_inplace(length, buffer, direction, flags) bind(C) result(plan)
       import :: c_int, c_ptr
       integer(c_int), value :: length, direction, flags
       type(c_ptr), value :: buffer
       type(c_ptr) :: plan
     end function wsjt_fftwf_plan_dft_1d_inplace
  end interface
end module FFTW3
