module m65a_mod
  implicit none
contains

subroutine m65a()

  use timer_module, only: timer
  use, intrinsic :: iso_c_binding, only: C_NULL_CHAR
  use npar_ptrs_mod
  use datcom_ptrs_mod, only: dd,ss,savg
  use m65_mod
  use FFTW3
  use ftninit_mod
  
  implicit none
  
  character(len=256) cwd

  cwd = trim(wsjtx_dir)

  if (len_trim(cwd) == 0) then
     call get_environment_variable("PWD", cwd)
  end if

  if (len_trim(cwd) == 0) then
     call get_environment_variable("CD", cwd)
  end if

  if (len_trim(cwd) == 0) then
     cwd = "."
  end if
  
  call ftninit(trim(cwd))
  
  if (.not. associated(savg)) then
  !  print *, 'ERROR:M65A savg is not associated!'
  end if
  
  if (.not. associated(dd)) then
  !  print *, 'ERROR: M65A dd is not associated!'
  end if
  
  if (.not. associated(ss)) then
  !  print *, 'ERROR: M65A ss is not associated!'
  end if
      
  call m65c()
  return

end subroutine m65a

end module m65a_mod
