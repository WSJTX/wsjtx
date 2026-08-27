module trimlist_mod
  implicit none
contains
  subroutine select_unique_decodes(sig, msg, km, freq_tolerance, dt_tolerance, indx, nz)
    use indexx_mod
    implicit none

    integer, parameter :: MAXMSG = 1000
    real, intent(in) :: sig(MAXMSG, 30)
    character(len=22), intent(in) :: msg(MAXMSG)
    integer, intent(in) :: km
    real, intent(in) :: freq_tolerance, dt_tolerance
    integer, intent(out) :: indx(MAXMSG)
    integer, intent(out) :: nz

    integer :: sorted(MAXMSG)
    integer :: i, j, k
    logical :: duplicate

    nz = 0
    if (km <= 0) return

    call indexx(sig(:, 3), km, sorted)
    do k = 1, km
      i = sorted(k)
      if (len_trim(msg(i)) == 0) cycle

      duplicate = .false.
      do j = 1, nz
        if (nint(sig(i, 2)) /= nint(sig(indx(j), 2))) cycle
        if (msg(i) /= msg(indx(j))) cycle
        if (nint(sig(i, 7)) /= nint(sig(indx(j), 7))) cycle
        if (abs(sig(i, 3) - sig(indx(j), 3)) > freq_tolerance) cycle
        if (abs(sig(i, 5) - sig(indx(j), 5)) > dt_tolerance) cycle
        duplicate = .true.
        if (sig(i, 8) > sig(indx(j), 8) .or. &
            (sig(i, 8) == sig(indx(j), 8) .and. sig(i, 4) > sig(indx(j), 4)) .or. &
            (sig(i, 8) == sig(indx(j), 8) .and. sig(i, 4) == sig(indx(j), 4) .and. &
             i > indx(j))) indx(j) = i
        exit
      end do

      if (.not. duplicate) then
        nz = nz + 1
        indx(nz) = i
      end if
    end do
  end subroutine select_unique_decodes
end module trimlist_mod
