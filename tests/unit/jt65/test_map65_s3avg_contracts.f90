program test_map65_s3avg_contracts
  use encode65_mod, only: encode65
  use s3avg_mod, only: s3avg
  implicit none

  call test_group_isolation_and_tolerance
  call test_timing_and_duplicates
  print '(a)', 'MAP65 s3avg contract tests passed.'

contains

  subroutine test_group_isolation_and_tolerance
    real :: s3(64,63)
    character(len=22) :: target, decoded
    integer :: code(63), nsave, nsum, nkv

    target='K1ABC W9XYZ FN42'
    call make_spectrum(target,code,s3)
    nsave=0

    call next_slot(nsave)
    nsum=-77
    call s3avg(nsave,1,1000,1000,0.0,1,100,s3,nsum,nkv,decoded)
    call require(nsum == 1 .and. nkv == 0 .and. len_trim(decoded) == 0, &
                 'first frequency observation is isolated')

    call next_slot(nsave)
    call s3avg(nsave,1,1002,1000,0.0,2,100,s3,nsum,nkv,decoded)
    call require(nsum == 2 .and. nkv == 2 .and. decoded(1:len_trim(target)) == target, &
                 'same-parity frequency observations decode as a pair')

    call next_slot(nsave)
    call s3avg(nsave,1,1003,3000,0.0,1,100,s3,nsum,nkv,decoded)
    call require(nsum == 1 .and. nkv == 0 .and. len_trim(decoded) == 0, &
                 'frequency-separated observations do not join the group')

    call next_slot(nsave)
    call s3avg(nsave,1,1101,5000,0.0,1,100,s3,nsum,nkv,decoded)
    call require(nsum == 1 .and. nkv == 0, 'dynamic tolerance starts a new group')

    call next_slot(nsave)
    call s3avg(nsave,1,1103,5050,0.0,1,50,s3,nsum,nkv,decoded)
    call require(nsum == 2 .and. nkv == 2 .and. decoded(1:len_trim(target)) == target, &
                 'dynamic tolerance includes its inclusive boundary')

    call next_slot(nsave)
    call s3avg(nsave,1,1105,5000,0.0,1,10,s3,nsum,nkv,decoded)
    call require(nsum == 2 .and. nkv == 2 .and. decoded(1:len_trim(target)) == target, &
                 'dynamic tolerance excludes the saved 5050-Hz frame')

    call next_slot(nsave)
    call s3avg(nsave,1,1107,5011,0.0,1,10,s3,nsum,nkv,decoded)
    call require(nsum == 1 .and. nkv == 0 .and. len_trim(decoded) == 0, &
                 'dynamic tolerance rejects frequencies outside its boundary')
  end subroutine test_group_isolation_and_tolerance

  subroutine test_timing_and_duplicates
    real :: s3(64,63)
    character(len=22) :: target, decoded
    integer :: code(63), nsave, nsum, nkv, duplicate_slot

    target='K1ABC W9XYZ FN42'
    call make_spectrum(target,code,s3)
    nsave=7

    call next_slot(nsave)
    call s3avg(nsave,1,1200,7000,0.00,1,100,s3,nsum,nkv,decoded)
    call require(nsum == 1 .and. nkv == 0, 'timing fixture starts alone')

    call next_slot(nsave)
    call s3avg(nsave,1,1202,7000,0.19,1,100,s3,nsum,nkv,decoded)
    call require(nsum == 2 .and. nkv == 2 .and. decoded(1:len_trim(target)) == target, &
                 'nearby timing observations join the group')

    call next_slot(nsave)
    call s3avg(nsave,1,1204,7000,0.50,1,100,s3,nsum,nkv,decoded)
    call require(nsum == 1 .and. nkv == 0 .and. len_trim(decoded) == 0, &
                 'distant timing observations remain isolated')

    call next_slot(nsave)
    call s3avg(nsave,1,1300,9000,0.00,1,100,s3,nsum,nkv,decoded)
    call require(nsum == 1 .and. nkv == 0, 'duplicate fixture starts alone')
    duplicate_slot=nsave

    call next_slot(nsave)
    call s3avg(nsave,1,1300,9000,0.01,1,100,s3,nsum,nkv,decoded)
    call require(nsum == 1 .and. nkv == 0 .and. nsave == duplicate_slot, &
                 'same-UTC duplicate is suppressed and ring position rolls back')

    call next_slot(nsave)
    call s3avg(nsave,1,1302,9000,0.02,1,100,s3,nsum,nkv,decoded)
    call require(nsum == 2 .and. nkv == 2 .and. decoded(1:len_trim(target)) == target, &
                 'next same-parity observation forms exactly a pair after duplicate')
  end subroutine test_timing_and_duplicates

  subroutine make_spectrum(message,code,s3)
    character(len=*), intent(in) :: message
    integer, intent(out) :: code(63)
    real, intent(out) :: s3(64,63)
    integer :: j

    call encode65(message,code)
    s3=1.0
    do j=1,63
       s3(code(j)+1,j)=100.0
    enddo
  end subroutine make_spectrum

  subroutine next_slot(nsave)
    integer, intent(inout) :: nsave
    nsave=mod(nsave,64)+1
  end subroutine next_slot

  subroutine require(condition, description)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: description
    if (.not. condition) then
       print '(a)', 'FAIL: '//description
       error stop 1
    endif
  end subroutine require

end program test_map65_s3avg_contracts
