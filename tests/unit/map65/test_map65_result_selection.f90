program test_map65_result_selection
  use trimlist_mod, only: select_unique_decodes
  implicit none

  integer, parameter :: MAXMSG = 1000
  real :: sig(MAXMSG, 30)
  character(len=22) :: msg(MAXMSG)
  integer :: indx(MAXMSG), nz

  call test_result_identity
  call test_empty_and_full_lists
  print '(a)', 'MAP65 result selection tests passed.'

contains

  subroutine test_result_identity
    sig = 0.0
    msg = ' '

    call set_result(1, 1, 100.000, 0.20, 1, 'K1ABC W9XYZ FN42')
    call set_result(2, 1, 100.005, 0.25, 1, 'K1ABC W9XYZ FN42')
    call set_result(3, 1, 100.005, 0.20, 1, 'W1AAA K2BBB EM00')
    call set_result(4, 1, 100.005, 0.50, 1, 'K1ABC W9XYZ FN42')
    call set_result(5, 1, 100.005, 0.20, -1, 'K1ABC W9XYZ FN42')
    call set_result(6, 1, 100.020, 0.20, 1, 'K1ABC W9XYZ FN42')
    call set_result(7, 1, 100.005, 0.20, 1, ' ')
    call set_result(8, 2, 100.005, 0.20, 1, 'K1ABC W9XYZ FN42')

    call select_unique_decodes(sig, msg, 8, 0.010, 0.2, indx, nz)
    call require(nz == 6, 'only a matching message and signal identity is deduplicated')
    call require(selected_count('W1AAA K2BBB EM00', 1, 1) == 1, &
                 'a different message at the same frequency survives')
    call require(selected_count('K1ABC W9XYZ FN42', 1, -1) == 1, &
                 'opposite-sync results survive')
    call require(selected_count('K1ABC W9XYZ FN42', 2, 1) == 1, &
                 'results from another UTC survive')
  end subroutine test_result_identity

  subroutine test_empty_and_full_lists
    integer :: i

    sig = 0.0
    msg = ' '
    call select_unique_decodes(sig, msg, 0, 0.010, 0.2, indx, nz)
    call require(nz == 0, 'an empty result list stays empty')

    do i = 1, MAXMSG
      sig(i, 2) = 1.0
      sig(i, 3) = 0.020*i
      sig(i, 5) = 0.2
      sig(i, 7) = 1.0
      write(msg(i), '("MSG",I4.4)') i
    end do
    call select_unique_decodes(sig, msg, MAXMSG, 0.010, 0.2, indx, nz)
    call require(nz == MAXMSG, 'the maximum result list does not overrun selection storage')
  end subroutine test_empty_and_full_lists

  subroutine set_result(index, utc, frequency, dt, flip, message)
    integer, intent(in) :: index, utc, flip
    real, intent(in) :: frequency, dt
    character(len=*), intent(in) :: message

    sig(index, 2) = utc
    sig(index, 3) = frequency
    sig(index, 5) = dt
    sig(index, 7) = flip
    msg(index) = message
  end subroutine set_result

  integer function selected_count(message, utc, flip)
    character(len=*), intent(in) :: message
    integer, intent(in) :: utc, flip
    integer :: i, source

    selected_count = 0
    do i = 1, nz
      source = indx(i)
      if (msg(source) == message .and. nint(sig(source, 2)) == utc .and. &
          nint(sig(source, 7)) == flip) selected_count = selected_count + 1
    end do
  end function selected_count

  subroutine require(condition, description)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: description

    if (.not. condition) then
      print '(a)', 'FAIL: '//description
      error stop 1
    end if
  end subroutine require
end program test_map65_result_selection
