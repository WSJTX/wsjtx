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
  use npar_ptrs_mod, only: newdat, stop_m65, decoder_ready, t_start, nhsym, &
                            mycall, hiscall, hisgrid, neme
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
  integer :: iheartbeat

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

  ! Build the Deep Search CALL3.TXT candidate list here, before the decode
  ! loop below ever runs, instead of lazily inside the first real deep65()
  ! call -- see build_call3_candidates()'s own header comment in deep65.f90
  ! for why that cost can't run on the decoder thread during a live cycle.
  ! build_call3_candidates() still gets called again on demand from inside
  ! deep65() (gated on mcall3a; see decode0.f90) if mycall/hiscall/hisgrid/
  ! neme change later, or the user edits CALL3.TXT.
  call dbg('run_m65: eager build_call3_candidates() STARTING at t=' // rtoa(sec_midn()))
  call build_call3_candidates(mycall, hiscall, hisgrid, neme)
  call dbg('run_m65: eager build_call3_candidates() DONE at t=' // rtoa(sec_midn()))

  !print *, 'IN RUN_M65, just passed init_timer'
  ! TEMP diagnostic 2026-09-10 for the missing-final-pass / stuck-button
  ! investigation: a periodic heartbeat (~1/sec) of the raw trigger state,
  ! plus an explicit log every time m65a() actually fires or returns, so a
  ! long gap between decode cycles shows directly whether newdat/decoder_ready
  ! ever went to a state that should have triggered a call but didn't, versus
  ! genuinely sitting idle (both 0) the whole time.
  iheartbeat = 0
  do while (stop_m65 == 0)
     if (decoder_ready /=0 .and. newdat /= 0) then
        call dbg('run_m65: FIRING m65a() at t=' // rtoa(sec_midn()) // &
                 ' nhsym=' // itoa(nhsym) // ' newdat=' // itoa(newdat) // &
                 ' decoder_ready=' // itoa(decoder_ready))
        call system_clock(t_start, t_rate)

        call m65a()

        call system_clock(t_now)
        call dbg('run_m65: m65a() RETURNED at t=' // rtoa(sec_midn()) // &
                 ' nhsym=' // itoa(nhsym) // ' newdat=' // itoa(newdat) // &
                 ' decoder_ready=' // itoa(decoder_ready) // &
                 ' elapsed=' // rtoa(real(t_now - t_start)/real(t_rate)))
     else
        iheartbeat = iheartbeat + 1
        if (mod(iheartbeat, 20) == 0) then
           call dbg('run_m65: idle heartbeat at t=' // rtoa(sec_midn()) // &
                    ' nhsym=' // itoa(nhsym) // ' newdat=' // itoa(newdat) // &
                    ' decoder_ready=' // itoa(decoder_ready))
        end if
     end if
     call sleep_msec(50)
  end do

  call fini_timer()
  close(21)

end subroutine run_m65
end module run_m65_mod
