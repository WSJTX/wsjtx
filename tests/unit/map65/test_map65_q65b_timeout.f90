program test_map65_q65b_timeout
  use iso_fortran_env, only: real64
  use npar_ptrs_mod, only: abort_decode, t_start
  use q65_decode, only: cq0, msg0, nfreq0, nsnr0, xdt0
  use q65b_mod, only: q65b
  implicit none

  integer :: idec, now, rate

  call system_clock(now, rate)
  t_start = now - 41*rate
  abort_decode = .false.
  idec = 123
  nsnr0 = 7
  msg0 = 'STALE MESSAGE'
  cq0 = 'q1 '
  xdt0 = 2.5
  nfreq0 = 1000

  call q65b(1, 0, 0, 144.125_real64, 0, 96000, 125, 0, 100, .false., 0, &
            'K1ABC       ', 'FN42  ', 'W9XYZ       ', 'EN50  ', 2, 1000.0_real64, 125.0, &
            1, 0, 0, 0, idec)

  if (.not. abort_decode) then
    print '(a)', 'FAIL: expired decode budget sets the abort flag'
    error stop 1
  end if
  call require(idec == -1, 'timeout returns a defined failure status')
  call require(nsnr0 == -99, 'timeout clears stale Q65 SNR state')
  call require(len_trim(msg0) == 0 .and. len_trim(cq0) == 0, &
               'timeout clears stale Q65 message state')
  call require(xdt0 == 0.0 .and. nfreq0 == 0, 'timeout clears stale Q65 signal state')
  print '(a)', 'MAP65 Q65 timeout test passed.'

contains

  subroutine require(condition, description)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: description

    if (.not. condition) then
      print '(a)', 'FAIL: '//description
      error stop 1
    end if
  end subroutine require
end program test_map65_q65b_timeout
