module run_m65_mod
   use iso_c_binding
   implicit none
contains

subroutine run_m65(pol, sample_rate_96000) bind(C, name='run_m65_')
  use iso_c_binding
  use timer_module, only: timer
  use timer_impl, only: init_timer, fini_timer
  use debug_log
  use m65a_mod
  use npar_ptrs_mod, only: t_start, nhsym, mycall, hiscall, hisgrid, neme, &
                            initialize_decode_parameters, claim_decode_request, get_stop_m65, &
                            active_request_id, active_request_expired
  use stdout_channel_mod, only: write_stdout
  use decodes_mod, only: nhsym1,nhsym2
  use deep65_mod, only: build_call3_candidates
  use sleep_msec_mod
  use sec_midn_mod, only: sec_midn

  implicit none

  integer(c_int), intent(in) :: pol, sample_rate_96000

  ! Local variables
  integer :: sample_rate
  character(len=128) :: line
  integer :: t_rate
  integer :: t_now

  nhsym1=280
  nhsym2=302

  dbg_enabled = .false.   ! flip to .true. to enable dbg() logging to w3sz_debug.log

  if (sample_rate_96000 /=0) then
     sample_rate = 96000
  else
     sample_rate = 95238   ! 96000 / 1.008, legacy WSJT slow-96k correction
  endif
  call write_stdout('STARTING RUN_M65'//new_line('a'))

  ! and one of the others, e.g.
  write (line, '(A, I0)') ' ********** IN RUN_M65 sample_rate is: ', sample_rate
  call write_stdout(trim(line)//new_line('a'))

!  if (.not. associated(savg)) then
  !   print *, 'ERROR:RUN_M65 savg is not associated!'
!  else
  !   print *, 'RUN_M65 savg is associated. Shape =', shape(savg), " loc:", loc(savg)
!  end if

!  if (.not. associated(dd)) then
  !   print *, 'ERROR: RUN_M65 dd is not associated!'
!  else
  !   print *, 'RUN_M65 dd is associated. Shape =', shape(dd), " loc:", loc(dd)
!  end if

!  if (.not. associated(ss)) then
  !   print *, 'ERROR: RUN_M65 ss is not associated!'
!  else
  !   print *, 'RUN_M65 ss is associated. Shape =', shape(ss), " loc:", loc(ss)
!  end if

!  print *, ' ********** IN RUN_M65 sample_rate_96000 is: ', sample_rate
!  flush (6)
!  print *, ' ********** IN RUN_M65 pol is: ', pol
!  flush (6)

  !print *, 'IN RUN_M65, initial stop_m65 =', stop_m65
  !flush(6)

  call init_timer()
  call initialize_decode_parameters()

  ! Encode the CALL3.TXT candidates before the first decode's time budget starts.
  ! deep65 rebuilds them when calls, grids, the EME filter, or CALL3.TXT change.
  call dbg('run_m65: eager build_call3_candidates() STARTING at t=' // rtoa(sec_midn()))
  call build_call3_candidates(mycall, hiscall, hisgrid, neme)
  call dbg('run_m65: eager build_call3_candidates() DONE at t=' // rtoa(sec_midn()))

  do while (get_stop_m65() == 0)
     if (claim_decode_request()) then
        if (active_request_expired) then
           write(line, '(A,1X,I0)') '<DecodeSkipped>', active_request_id
           call write_stdout(trim(line)//new_line('a'))
        else
           call system_clock(t_start, t_rate)
           call m65a()
           call system_clock(t_now)
           call dbg('run_m65: request completed, elapsed=' // rtoa(real(t_now - t_start)/real(t_rate)))
        endif
     endif
     call sleep_msec(50)
  end do

  call fini_timer()
  close(21)

end subroutine run_m65
end module run_m65_mod
