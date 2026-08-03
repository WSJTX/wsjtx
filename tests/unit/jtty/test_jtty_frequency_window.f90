program test_jtty_frequency_window
  use jtty_mdec, only: jtty_search_window
  implicit none

  integer, parameter :: first_bin=3,last_bin=4094
  real, parameter :: df=6000.0/8192.0
  integer :: ja,jb
  real :: fc
  logical :: usable

  fc=1500.0
  call jtty_search_window(fc,50.0,200,2800,.true.,df,first_bin,last_bin, &
       ja,jb,usable)
  call assert_true(usable, 'nominal window is usable')
  call assert_true(fc.eq.1500.0, 'nominal center is unchanged')
  call assert_true(ja.eq.int(1450.0/df), 'nominal lower bin is unchanged')
  call assert_true(jb.eq.int(1550.0/df), 'nominal upper bin is unchanged')

  fc=2500.0
  call jtty_search_window(fc,1000.0,0,5000,.true.,df,first_bin,last_bin, &
       ja,jb,usable)
  call assert_true(usable, 'partially overlapping upper window is usable')
  call assert_true(ja.eq.int(1500.0/df), 'partial upper window keeps its lower bin')
  call assert_true(jb.eq.last_bin, 'partial upper window is clipped to the spectrum')

  fc=5000.0
  call jtty_search_window(fc,1000.0,0,5000,.true.,df,first_bin,last_bin, &
       ja,jb,usable)
  call assert_true(.not.usable, 'window above the spectrum is rejected')

  fc=1500.0
  call jtty_search_window(fc,20.0,4900,5000,.true.,df,first_bin,last_bin, &
       ja,jb,usable)
  call assert_true(fc.eq.4900.0, 'center is clamped to the graph lower edge')
  call assert_true(.not.usable, 'graph above the spectrum is rejected')

  fc=1350.0
  call jtty_search_window(fc,150.0,1400,1600,.true.,df,first_bin,last_bin, &
       ja,jb,usable)
  call assert_true(usable, 'lower auxiliary edge is usable')
  call assert_true(fc.eq.1400.0, 'lower auxiliary center is clamped')
  call assert_true(ja.eq.ceiling(1400.0/df), 'lower graph edge clips the window')

  fc=1650.0
  call jtty_search_window(fc,150.0,1400,1600,.true.,df,first_bin,last_bin, &
       ja,jb,usable)
  call assert_true(usable, 'upper auxiliary edge is usable')
  call assert_true(fc.eq.1600.0, 'upper auxiliary center is clamped')
  call assert_true(jb.eq.floor(1600.0/df), 'upper graph edge clips the window')

  fc=1500.0
  call jtty_search_window(fc,50.0,1800,1200,.true.,df,first_bin,last_bin, &
       ja,jb,usable)
  call assert_true(.not.usable, 'inverted graph limits are rejected')

  fc=1500.0
  call jtty_search_window(fc,-1.0,200,2800,.true.,df,first_bin,last_bin, &
       ja,jb,usable)
  call assert_true(.not.usable, 'negative window width is rejected')

  print *, 'JTTY frequency window tests passed'

contains

  subroutine assert_true(condition,message)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: message

    if(.not.condition) then
       write(*,'(a)') message
       error stop 1
    endif
  end subroutine assert_true

end program test_jtty_frequency_window
