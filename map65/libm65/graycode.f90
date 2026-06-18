module graycode_mod
  implicit none
  interface
    integer function igray(n, idir)
      integer, intent(in) :: n, idir
    end function igray
  end interface
contains
  subroutine graycode(dat, n, idir)
    integer, intent(in)    :: n, idir
    integer, intent(inout) :: dat(n)
    integer :: i

    do i = 1, n
       dat(i) = igray(dat(i), idir)
    enddo
  end subroutine graycode
end module graycode_mod
