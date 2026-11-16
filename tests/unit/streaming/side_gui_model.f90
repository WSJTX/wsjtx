! SPDX-License-Identifier: GPL-3.0-or-later
! The GUI's side of the Q65 caller list, tsil.3q, for test_streaming_side:
!   side_gui_model list FILE         the records q65_load_callers reads from
!                                    FILE, as get_q3list loads them
!                                    (lib/get_q3list.f90): "count N", then one
!                                    "call|grid|nsec|nfreq|moonel" a record
!   side_gui_model remove FILE CALL  rm_q3list (lib/get_q3list.f90), as the
!                                    GUI's Ctrl-click and logging call it
!                                    (widgets/mainwindow.cpp)
program side_gui_model
  use types, only: q3list
  use q65_callers, only: Q65_MAX_CALLERS, q65_load_callers
  implicit none
  character(len=512) :: command, fname, dxcall
  type(q3list) :: callers(Q65_MAX_CALLERS)
  integer :: count, status, i

  call get_command_argument(1, command)
  call get_command_argument(2, fname)
  select case (trim(command))
  case ('list')
     call q65_load_callers(trim(fname), callers, count, status)
     if (status .ne. 0) then
        write(*, '(a,i0)') 'status ', status
        stop 1
     end if
     write(*, '(a,i0)') 'count ', count
     do i = 1, count
        write(*, '(a,"|",a,"|",i0,"|",i0,"|",i0)') trim(callers(i)%call), trim(callers(i)%grid), &
             callers(i)%nsec, callers(i)%nfreq, callers(i)%moonel
     end do
  case ('remove')
     call get_command_argument(3, dxcall)
     call rm_q3list(trim(fname), trim(dxcall))
  case default
     write(*, '(a)') 'usage: side_gui_model list FILE | remove FILE CALL'
     stop 2
  end select
end program side_gui_model
