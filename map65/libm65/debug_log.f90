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
end module debug_log
