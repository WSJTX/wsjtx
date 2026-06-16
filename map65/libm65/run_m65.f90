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
  use datcom_ptrs_mod, only: dd, ss, savg
  use npar_ptrs_mod, only: newdat, stop_m65, decoder_ready
  use stdout_channel_mod, only: write_stdout
  use decodes_mod, only: nhsym1,nhsym2
  use sleep_msec_mod

  implicit none

  integer(c_int), intent(in) :: pol, sample_rate_96000

  ! Local variables
  integer :: sample_rate
  character(len=128) :: line
  
  ! timestamp variables
  character(len=8)  :: d
  character(len=10) :: t
  integer           :: v(8)
  character(len=32) :: timestamp

  nhsym1=280
  nhsym2=302 
  
  ! call ensure_log_open() !uncomment this to enable logging to file

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

  !call date_and_time(d, t, values=v)

  !write(timestamp, '(I4.4,"-",I2.2,"-",I2.2," ",I2.2,":",I2.2,":",I2.2,".",I3.3)') &
  !     v(1), v(2), v(3), v(5), v(6), v(7), v(8)

  !print *, trim(timestamp), ' CALLING M65A'


  call init_timer()

  !print *, 'IN RUN_M65, just passed init_timer'
  do while (stop_m65 == 0)
     if (decoder_ready /=0 .and. newdat /= 0) then
      !  call date_and_time(d, t, values=v)

      !  write(timestamp, '(I4.4,"-",I2.2,"-",I2.2," ",I2.2,":",I2.2,":",I2.2,".",I3.3)') &
      !       v(1), v(2), v(3), v(5), v(6), v(7), v(8)

      !  print *, trim(timestamp), ' CALLING M65A'
        call m65a()
     end if
!print *, ' ENTERING SLEEP_MSEC'
call sleep_msec(50)
!print *, ' RETURNED FROM SLEEP_MSEC'
!print *, ' LOOP: stop_m65 =', stop_m65, &
!         ' decoder_ready=', decoder_ready, &
!         ' newdat=', newdat

! stop_m65 = .true.
  end do

!  print *, 'RUN_M65 exiting main loop.'
!  flush (6)
  call fini_timer()
  close(21)

end subroutine run_m65
end module run_m65_mod
