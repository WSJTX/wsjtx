! SPDX-License-Identifier: GPL-3.0-or-later
! sfox_init: a call with SuperFox's values sets the channel model and nothing
! else; with an argument naming one parameter (mm, nn, kk, ns or fsample), a
! call that differs only in it stops.
program test_superfox_init

  use sfox_mod
  implicit none

  character(len=16) :: arg
  real :: fspread, delay

  call get_command_argument(1, arg)

  select case(trim(arg))
  case('mm')
     call sfox_init(8,127,50,'no',fspread,delay,12000.0,24)
  case('nn')
     call sfox_init(7,126,50,'no',fspread,delay,12000.0,24)
  case('kk')
     call sfox_init(7,127,49,'no',fspread,delay,12000.0,24)
  case('ns')
     call sfox_init(7,127,50,'no',fspread,delay,12000.0,23)
  case('fsample')
     call sfox_init(7,127,50,'no',fspread,delay,48000.0,24)
  end select
  if(len_trim(arg).gt.0) then
     write(*,'(a)') 'FAIL: a call with another '//trim(arg)//' returned'
     stop 1
  end if

  call sfox_init(7,127,50,'LQ',fspread,delay,12000.0,24)
  if(fspread.ne.0.5 .or. delay.ne.0.5) then
     write(*,'(a)') 'FAIL: the channel model did not follow the call'
     stop 1
  end if
  call sfox_init(7,127,50,'no',fspread,delay,12000.0,24)
  if(fspread.ne.0.0 .or. delay.ne.0.0) then
     write(*,'(a)') 'FAIL: itu "no" left a channel model'
     stop 1
  end if

end program test_superfox_init
