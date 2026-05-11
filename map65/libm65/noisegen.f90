module noisegen_mod
  implicit none
contains
  subroutine noisegen(d4,nmax)
    use iso_c_binding, only: c_ptr, c_loc, c_float
    use gran_interface
    use iso_fortran_env, only: real32
    implicit none
  
    integer, intent(in) :: nmax
    real(real32), intent(out) :: d4(4,nmax)
    integer :: i
  
    do i=1,nmax
       d4(1,i)=gran()
       d4(2,i)=gran()
       d4(3,i)=gran()
       d4(4,i)=gran()
    enddo
  
  end subroutine noisegen
end module noisegen_mod
