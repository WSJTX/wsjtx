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
  use datcom_ptrs_mod, only: dd,ss,savg
  use npar_ptrs_mod, only: newdat, stop_m65, decoder_ready
  use stdout_channel_mod, only: write_stdout
use decodes_mod, only: nhsym1,nhsym2
  
  implicit none

  logical(c_bool):: pol, sample_rate_96000 !(bools for XPOL and 96000Hz)

  ! Local variables
  integer :: sample_rate
  character(len=128) :: line
   
  nhsym1=280
  nhsym2=302 
   
  if (sample_rate_96000) then
    sample_rate = 96000
  else
    sample_rate = 95430
  endif
  call write_stdout('STARTING RUN_M65'//new_line('a'))

  ! and one of the others, e.g.
  write(line, '(A, I0)') ' ********** IN RUN_M65 sample_rate_96000 is: ', sample_rate
  call write_stdout(trim(line)//new_line('a'))
  
  if (.not. associated(savg)) then
    print *, 'ERROR:RUN_M65 savg is not associated!'
  else
    print *, 'RUN_M65 savg is associated. Shape =', shape(savg)," loc:", loc(savg)
  end if
  
  if (.not. associated(dd)) then
    print *, 'ERROR: RUN_M65 dd is not associated!'
  else
    print *, 'RUN_M65 dd is associated. Shape =', shape(dd)," loc:", loc(dd)
  end if
  
  if (.not. associated(ss)) then
    print *, 'ERROR: RUN_M65 ss is not associated!'
  else
    print *, 'RUN_M65 ss is associated. Shape =', shape(ss)," loc:", loc(ss)
  end if
  
  print*, ' ********** IN RUN_M65 sample_rate_96000 is: ', sample_rate
  flush(6)
  print*, ' ********** IN RUN_M65 pol is: ', pol
  flush(6)
    
  call init_timer() 
  
  do while (.not. stop_m65) 
    if (decoder_ready .and. newdat .ne. 0) then
      call m65a() 
    end if
    call sleep_msec(50) 
  end do 
  
  print *, 'RUN_M65 exiting main loop.' 
  flush(6)   
  call fini_timer()

end subroutine run_m65
end module run_m65_mod
