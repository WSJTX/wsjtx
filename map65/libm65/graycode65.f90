module graycode65_mod
  implicit none

  ! Fortran-only interface to igray
  interface
    integer function igray(n, idir)
      integer, intent(in) :: n, idir
    end function igray
  end interface

contains

  subroutine graycode65(dat, n, idir)
    integer, intent(in)    :: n, idir
    integer, intent(inout) :: dat(n)
    integer :: i

    do i = 1, n
       dat(i) = igray(dat(i), idir)
    enddo
  end subroutine graycode65

end module graycode65_mod
