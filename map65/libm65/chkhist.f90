module chkhist_mod
  implicit none
contains

subroutine chkhist(mrsym, nmax, ipk)
  implicit none

  !--------------------------------------------------------------------
  ! Arguments
  !--------------------------------------------------------------------
  integer, intent(in)  :: mrsym(63)
  integer, intent(out) :: nmax
  integer, intent(out) :: ipk

  !--------------------------------------------------------------------
  ! Locals
  !--------------------------------------------------------------------
  integer :: hist(0:63)
  integer :: i, j

  !--------------------------------------------------------------------
  ! Initialize histogram
  !--------------------------------------------------------------------
  do i = 0, 63
     hist(i) = 0
  enddo

  !--------------------------------------------------------------------
  ! Count occurrences
  !--------------------------------------------------------------------
  do j = 1, 63
     i = mrsym(j)
     hist(i) = hist(i) + 1
  enddo

  !--------------------------------------------------------------------
  ! Find maximum bin and its index
  !--------------------------------------------------------------------
  nmax = 0
  do i = 0, 63
     if (hist(i) > nmax) then
        nmax = hist(i)
        ipk  = i + 1
     endif
  enddo

end subroutine chkhist

end module chkhist_mod
