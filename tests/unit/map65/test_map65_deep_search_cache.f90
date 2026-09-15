program test_map65_deep_search_cache
  use decodes_mod, only: mcall3a
  use deep65_mod, only: build_call3_candidates, testmsg, ncode, ntot
  use encode65_mod, only: encode65
  implicit none

  character(len=12), parameter :: mycall = 'K1ABC', hiscall = 'W9XYZ', no_call = ''
  character(len=6), parameter :: hisgrid = 'EN37'
  integer :: fixture_unit, ios
  logical :: fixture_owned = .false.

  ! Run in a dedicated test directory; never overwrite an existing callsign database.
  open(newunit=fixture_unit, file='CALL3.TXT', status='new', action='write', iostat=ios)
  call require(ios == 0, 'create an isolated CALL3.TXT fixture')
  fixture_owned = .true.
  call write_fixture(fixture_unit, 'G4XYZ,IO91,NONE,')
  close(fixture_unit)

  call test_startup_transition
  call test_mycall_transition
  call test_hiscall_transition
  call test_grid_transition
  call test_eme_transition
  call test_file_invalidation

  call remove_fixture
  print '(a)', 'MAP65 Deep Search cache tests passed.'

contains

  subroutine build_fresh(local_call, dx_call, dx_grid, eme_only)
    character(len=12), intent(in) :: local_call, dx_call
    character(len=6), intent(in) :: dx_grid
    integer, intent(in) :: eme_only

    mcall3a = 1
    call build_call3_candidates(local_call, dx_call, dx_grid, eme_only)
    call require(mcall3a == 0, 'building consumes file invalidation')
  end subroutine build_fresh

  subroutine test_startup_transition
    call build_fresh(mycall, no_call, hisgrid, 0)
    call require(.not. any(testmsg(1:ntot) == 'K1ABC W9XYZ R-01'), &
                 'startup without a DX call has no directed report candidates')
    call build_call3_candidates(mycall, hiscall, hisgrid, 0)
    call require(ntot == 132, 'a supplied DX call adds all report variants and file candidates')
    call require_message('K1ABC W9XYZ EN37')
    call require_message('K1ABC W9XYZ -01')
    call require_message('K1ABC W9XYZ R-30')
    call require_message('K1ABC W9XYZ 73')
    call require_message('CQ K2ABC FN20')
    call require_message('CQ G4XYZ IO91')
    call require_fresh_equivalent(mycall, hiscall, hisgrid, 0, 'startup DX entry')
    call build_call3_candidates(mycall, hiscall, hisgrid, 0)
    call require_fresh_equivalent(mycall, hiscall, hisgrid, 0, 'unchanged settings')
  end subroutine test_startup_transition

  subroutine test_mycall_transition
    character(len=12), parameter :: changed_call = 'K3ABC'

    call build_fresh(mycall, hiscall, hisgrid, 0)
    call build_call3_candidates(changed_call, hiscall, hisgrid, 0)
    call require_message('K3ABC W9XYZ EN37')
    call require_message('K3ABC K2ABC FN20')
    call require(.not. any(index(testmsg(1:ntot), 'K1ABC ') == 1), 'old MyCall candidates disappear')
    call require_fresh_equivalent(changed_call, hiscall, hisgrid, 0, 'MyCall change')
  end subroutine test_mycall_transition

  subroutine test_hiscall_transition
    character(len=12), parameter :: changed_call = 'W8XYZ'

    call build_fresh(mycall, hiscall, hisgrid, 0)
    call build_call3_candidates(mycall, changed_call, hisgrid, 0)
    call require_message('K1ABC W8XYZ EN37')
    call require_message('K1ABC W8XYZ R-01')
    call require(.not. any(index(testmsg(1:ntot), 'W9XYZ') > 0), 'old DX call candidates disappear')
    call require_fresh_equivalent(mycall, changed_call, hisgrid, 0, 'DX call change')
  end subroutine test_hiscall_transition

  subroutine test_grid_transition
    character(len=6), parameter :: changed_grid = 'FN42'

    call build_fresh(mycall, hiscall, hisgrid, 0)
    call build_call3_candidates(mycall, hiscall, changed_grid, 0)
    call require_message('K1ABC W9XYZ FN42')
    call require_message('CQ W9XYZ FN42')
    call require(.not. any(testmsg(1:ntot) == 'K1ABC W9XYZ EN37'), 'old DX grid candidate disappears')
    call require_fresh_equivalent(mycall, hiscall, changed_grid, 0, 'DX grid change')
  end subroutine test_grid_transition

  subroutine test_eme_transition
    call build_fresh(mycall, hiscall, hisgrid, 0)
    call build_call3_candidates(mycall, hiscall, hisgrid, 1)
    call require(ntot == 130, 'EME filtering removes only the non-EME file entry')
    call require_message('K1ABC K2ABC FN20')
    call require_message('K1ABC W9XYZ R-01')
    call require(.not. any(index(testmsg(1:ntot), 'G4XYZ') > 0), 'non-EME candidates disappear')
    call require_fresh_equivalent(mycall, hiscall, hisgrid, 1, 'enable EME filter')
    call build_call3_candidates(mycall, hiscall, hisgrid, 0)
    call require_message('CQ G4XYZ IO91')
    call require_fresh_equivalent(mycall, hiscall, hisgrid, 0, 'disable EME filter')
  end subroutine test_eme_transition

  subroutine test_file_invalidation
    call build_fresh(mycall, hiscall, hisgrid, 0)
    close(23)
    open(newunit=fixture_unit, file='CALL3.TXT', status='replace', action='write')
    call write_fixture(fixture_unit, 'G3ABC,IO92,NONE,')
    close(fixture_unit)

    call build_call3_candidates(mycall, hiscall, hisgrid, 0)
    call require_message('CQ G4XYZ IO91')
    call require(.not. any(testmsg(1:ntot) == 'CQ G3ABC IO92'), &
                 'unchanged settings reuse the cache until explicit file invalidation')
    mcall3a = 1
    call build_call3_candidates(mycall, hiscall, hisgrid, 0)
    call require(mcall3a == 0, 'a file reload consumes its invalidation flag')
    call require_message('CQ G3ABC IO92')
    call require(.not. any(index(testmsg(1:ntot), 'G4XYZ') > 0), 'file reload removes deleted stations')
    call require_fresh_equivalent(mycall, hiscall, hisgrid, 0, 'CALL3.TXT reload')
  end subroutine test_file_invalidation

  subroutine require_fresh_equivalent(local_call, dx_call, dx_grid, eme_only, description)
    character(len=12), intent(in) :: local_call, dx_call
    character(len=6), intent(in) :: dx_grid
    integer, intent(in) :: eme_only
    character(len=*), intent(in) :: description
    character(len=22), allocatable :: cached_messages(:)
    integer, allocatable :: cached_symbols(:, :)
    integer :: cached_count

    cached_count = ntot
    cached_messages = testmsg(1:ntot)
    cached_symbols = ncode(:, 1:ntot)
    call build_fresh(local_call, dx_call, dx_grid, eme_only)
    call require(ntot == cached_count, description//': cached and fresh candidate counts match')
    call require(all(testmsg(1:ntot) == cached_messages), description//': cached and fresh messages match')
    call require(all(ncode(:, 1:ntot) == cached_symbols), description//': cached and fresh encoded symbols match')
  end subroutine require_fresh_equivalent

  subroutine require_message(message)
    character(len=*), intent(in) :: message
    character(len=22) :: padded_message
    integer :: i, expected_symbols(63)

    padded_message = message
    call encode65(padded_message, expected_symbols)
    do i = 1, ntot
      if (testmsg(i) /= message) cycle
      call require(all(ncode(:, i) == expected_symbols), 'matching encoded symbols for '//message)
      return
    end do
    call require(.false., 'expected candidate '//message)
  end subroutine require_message

  subroutine write_fixture(unit, terrestrial_entry)
    integer, intent(in) :: unit
    character(len=*), intent(in) :: terrestrial_entry

    write(unit, '(a)') '// Controlled Deep Search candidates', 'K2ABC,FN20,EME,', terrestrial_entry, 'ZZZZ'
  end subroutine write_fixture

  subroutine remove_fixture
    logical :: opened
    integer :: unit, status

    if (.not. fixture_owned) return
    inquire(file='CALL3.TXT', opened=opened, number=unit, iostat=status)
    if (status /= 0) return
    if (.not. opened) then
      open(newunit=unit, file='CALL3.TXT', status='old', iostat=status)
      if (status /= 0) return
    end if
    close(unit, status='delete', iostat=status)
    if (status == 0) fixture_owned = .false.
  end subroutine remove_fixture

  subroutine require(condition, description)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: description

    if (.not. condition) then
      print '(a)', 'FAIL: '//description
      call remove_fixture
      error stop 1
    end if
  end subroutine require
end program test_map65_deep_search_cache
