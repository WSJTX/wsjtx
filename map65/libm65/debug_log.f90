module debug_log
  implicit none
  integer, save :: dbg_unit = -1
  logical, save :: dbg_opened = .false.
contains
  subroutine ensure_log_open()
    if (.not. dbg_opened) then
      open(newunit=dbg_unit, file='w3sz_debug.log', status='unknown', &
           action='write', position='append')
      dbg_opened = .true.
    end if
  end subroutine ensure_log_open
  
  subroutine dbg(msg)
    character(len=*), intent(in) :: msg
    call ensure_log_open()
    write(dbg_unit,'(A)') trim(msg)
    flush(dbg_unit)
  end subroutine dbg

  pure function itoa(i) result(s)
    integer, intent(in) :: i
    character(len=32) :: s
    write(s,'(I0)') i
  end function itoa

  pure function rtoa(x) result(s)
    real, intent(in) :: x
    character(len=64) :: s
    write(s,'(G16.8)') x
  end function rtoa

  pure function itoa8(i) result(s)
    integer(8), intent(in) :: i
    character(len=32) :: s
    write(s,'(I0)') i
  end function itoa8


end module debug_log
