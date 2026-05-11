module interleave63_mod
  implicit none
contains
  subroutine interleave63(d1,idir)
    use set_mod
    implicit none
    integer, intent(inout) :: d1(0:6,0:8)
    integer, intent(in) :: idir
    integer :: d2(0:8,0:6)
    integer :: i, j

    if(idir.ge.0) then
       do i=0,6
          do j=0,8
             d2(j,i)=d1(i,j)
          enddo
       enddo
       call move(d2,d1,63)
    else
       call move(d1,d2,63)
       do i=0,6
          do j=0,8
             d1(i,j)=d2(j,i)
          enddo
       enddo
    endif
           
  end subroutine interleave63
end module interleave63_mod
