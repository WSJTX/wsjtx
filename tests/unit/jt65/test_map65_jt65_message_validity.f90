program test_map65_jt65_message_validity
  use packjt, only: is_valid_jt65_codeword, packmsg, packcall, packgrid
  implicit none

  integer, parameter :: NBASE = 37*36*10*27*27*27
  integer :: dat(12), itype, nc1, ng
  logical :: text
  character(len=4) :: grid
  character(len=6) :: call
  character(len=22) :: message

  message = 'K1ABC W9XYZ FN42'
  call packmsg(message, dat, itype)
  call require(is_valid_jt65_codeword(dat), 'a standard structured message is valid')

  message = 'THIS IS FREE TEXT'
  call packmsg(message, dat, itype)
  call require(is_valid_jt65_codeword(dat), 'a free-text message is valid')

  call = '498IVA'
  call packcall(call, nc1, text)
  grid = 'EE84'
  call packgrid(grid, ng, text)
  call pack_fields(nc1, 265779995, ng, dat)
  call require(.not. is_valid_jt65_codeword(dat), &
               'the reproduced false decode has an impossible second callsign')

  call pack_fields(NBASE + 1, NBASE - 1, 0, dat)
  call require(is_valid_jt65_codeword(dat), 'a reserved first-field message is valid')

  call pack_fields(267796945, NBASE - 1, 0, dat)
  call require(is_valid_jt65_codeword(dat), 'the final JT65v2 first-field value is valid')

  call pack_fields(NBASE, 0, 0, dat)
  call require(.not. is_valid_jt65_codeword(dat), &
               'the unassigned first-field value is invalid')

  call pack_fields(267796946, 0, 0, dat)
  call require(.not. is_valid_jt65_codeword(dat), &
               'a first field above the JT65v2 range is invalid')

  call pack_fields(0, NBASE, 0, dat)
  call require(.not. is_valid_jt65_codeword(dat), &
               'a non-callsign second field is invalid')

  print '(a)', 'MAP65 JT65 message validity tests passed.'

contains

  subroutine pack_fields(first_call, second_call, packed_grid, symbols)
    integer, intent(in) :: first_call, second_call, packed_grid
    integer, intent(out) :: symbols(12)

    symbols(1) = iand(ishft(first_call,-22),63)
    symbols(2) = iand(ishft(first_call,-16),63)
    symbols(3) = iand(ishft(first_call,-10),63)
    symbols(4) = iand(ishft(first_call,-4),63)
    symbols(5) = 4*iand(first_call,15) + iand(ishft(second_call,-26),3)
    symbols(6) = iand(ishft(second_call,-20),63)
    symbols(7) = iand(ishft(second_call,-14),63)
    symbols(8) = iand(ishft(second_call,-8),63)
    symbols(9) = iand(ishft(second_call,-2),63)
    symbols(10) = 16*iand(second_call,3) + iand(ishft(packed_grid,-12),15)
    symbols(11) = iand(ishft(packed_grid,-6),63)
    symbols(12) = iand(packed_grid,63)
  end subroutine pack_fields

  subroutine require(condition, description)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: description

    if (.not. condition) then
      print '(a)', 'FAIL: '//description
      error stop 1
    endif
  end subroutine require
end program test_map65_jt65_message_validity
