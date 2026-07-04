program test_packjt77sdvar_invariants

  use packjt77sdvar, only: pack77sdvar, unpack77sdvar
  implicit none

  integer :: ntests

  ntests=0

  call expect_free_text_round_trip()
  call expect_type1_grid_boundary(32399,.true.,'K1ABC W9XYZ RR99')
  call expect_type1_grid_boundary(32400,.false.,'')

  write(*,1000) ntests
1000 format('packjt77sdvar invariant tests passed: ',i0)

contains

  subroutine expect_free_text_round_trip()
    character(len=37) :: input, decoded
    character(len=77) :: c77
    integer :: i3, n3
    logical :: ok

    input='FREE TEXT MSG'
    i3=-1
    n3=-1
    c77=''
    call pack77sdvar(input,i3,n3,c77)
    call assert_int('sdvar free-text i3',0,i3)
    call assert_int('sdvar free-text n3',0,n3)

    decoded='                                     '
    ok=.false.
    call unpack77sdvar(c77,decoded,ok)
    call assert_true('sdvar free-text unpack succeeds',ok)
    call assert_text('sdvar free-text decode','FREE TEXT MSG',decoded)

    ntests=ntests+1
  end subroutine expect_free_text_round_trip

  subroutine expect_type1_grid_boundary(igrid4,should_succeed,expected)
    integer, intent(in) :: igrid4
    logical, intent(in) :: should_succeed
    character(len=*), intent(in) :: expected
    character(len=37) :: input, decoded
    character(len=77) :: c77
    integer :: i3, n3
    logical :: ok

    input='K1ABC W9XYZ FN42'
    i3=-1
    n3=-1
    c77=''
    call pack77sdvar(input,i3,n3,c77)
    call assert_int('sdvar Type 1 i3',1,i3)
    call assert_int('sdvar Type 1 n3',0,n3)

    write(c77(60:74),'(b15.15)') igrid4
    decoded='                                     '
    ok=.false.
    call unpack77sdvar(c77,decoded,ok)

    if(should_succeed) then
       call assert_true('sdvar grid boundary succeeds',ok)
       call assert_text('sdvar grid boundary decode',expected,decoded)
    else
       call assert_true('sdvar invalid grid boundary fails',.not.ok)
    endif

    ntests=ntests+1
  end subroutine expect_type1_grid_boundary

  subroutine assert_int(label,expected,got)
    character(len=*), intent(in) :: label
    integer, intent(in) :: expected, got

    if(got.ne.expected) then
       write(*,1010) trim(label), expected, got
1010   format(a,' failure; expected ',i0,' got ',i0)
       error stop 1
    endif
  end subroutine assert_int

  subroutine assert_true(label,condition)
    character(len=*), intent(in) :: label
    logical, intent(in) :: condition

    if(.not.condition) then
       write(*,1020) trim(label)
1020   format(a,' failure')
       error stop 1
    endif
  end subroutine assert_true

  subroutine assert_text(label,expected,got)
    character(len=*), intent(in) :: label, expected, got

    if(trim(got).ne.expected) then
       write(*,1030) trim(label), trim(expected), trim(got)
1030   format(a,' failure; expected "',a,'"; got "',a,'"')
       error stop 1
    endif
  end subroutine assert_text

end program test_packjt77sdvar_invariants
