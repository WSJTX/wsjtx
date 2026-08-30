program test_jpl_resilience
  use iso_c_binding, only: c_bool, c_char, c_double, c_int, c_loc,     &
       c_null_char
  use iso_fortran_env, only: int8, real64
  use ieee_arithmetic, only: ieee_is_finite, ieee_value,              &
       ieee_quiet_nan, ieee_positive_inf
  use astro_module, only: astrosub
  use jpl_ephemeris_status
  implicit none

  character(len=64) :: scenario
  character(len=1024) :: source_path

  call get_command_argument(1,scenario)
  call get_command_argument(2,source_path)
  if(len_trim(scenario).eq.0 .or. len_trim(source_path).eq.0) error stop 1

  select case(trim(scenario))
  case('valid_reference')
     call test_valid_reference(trim(source_path))
  case('unit_collision')
     call test_unit_collision(trim(source_path))
  case('missing_fallback')
     call test_missing_fallback()
  case('analytic_failure')
     call test_analytic_failure()
  case('truncated_header')
     call test_truncated_header(trim(source_path))
  case('truncated_coefficients')
     call test_truncated_coefficients(trim(source_path))
  case('absurd_coefficients')
     call test_absurd_coefficients(trim(source_path))
  case('invalid_header')
     call test_invalid_header(trim(source_path))
  case('out_of_range')
     call test_out_of_range(trim(source_path))
  case('nonfinite_epoch')
     call test_nonfinite_epoch(trim(source_path))
  case('invalid_input')
     call test_invalid_input()
  case('blank_grid_compatibility')
     call test_blank_grid_compatibility(trim(source_path))
  case('optional_sections')
     call test_optional_sections(trim(source_path))
  case('source_transition')
     call test_source_transition(trim(source_path))
  case default
     error stop 2
  end select

  print '(a)', 'PASS '//trim(scenario)

contains

  subroutine test_valid_reference(path)
    character(len=*), intent(in) :: path
    real(real64) :: vector(6)
    real(real64), parameter :: expected(6) = [                         &
         -6.141033948345347e-4_real64,-2.105916028962251e-3_real64,    &
         -9.713000936360162e-4_real64, 6.128034786394336e-4_real64,    &
         -1.145724331397477e-4_real64,-1.097685197949237e-4_real64]
    integer :: status

    call configure(path)
    call pleph(2459580.5_real64,10,3,vector,status)
    call require(status.eq.JPL_STATUS_OK,'valid JPL status')
    call require(maxval(abs(vector-expected)).lt.1.0e-14_real64,      &
         'fixed reference vector')
  end subroutine test_valid_reference

  subroutine test_unit_collision(path)
    character(len=*), intent(in) :: path
    real(real64) :: vector(6)
    integer :: status
    logical :: opened

    open(12,status='scratch',action='readwrite')
    call configure(path)
    call pleph(2459580.5_real64,10,3,vector,status)
    inquire(unit=12,opened=opened)
    call require(status.eq.JPL_STATUS_OK,'JPL with occupied unit 12')
    call require(opened,'unit 12 preserved')
    close(12)
  end subroutine test_unit_collision

  subroutine test_missing_fallback()
    real(real64) :: fallback(8),forced(8)
    real(real64) :: vector(6),pvsun(6)
    integer :: result,status
    logical :: km,bary
    common/stcomx/km,bary,pvsun

    call configure('missing-jpleph')
    call ephem_values(60310.5_real64,0,fallback,result)
    call require(result.eq.EPHEMERIS_ANALYTIC_FALLBACK,              &
         'missing file fallback status')
    call ephem_values(60310.5_real64,8,forced,result)
    call require(result.eq.EPHEMERIS_ANALYTIC_FALLBACK,              &
         'forced analytic status')
    call require(maxval(abs(fallback-forced)).lt.1.0e-10_real64,      &
         'missing file matches analytic calculation')

    bary=.false.
    km=.false.
    call configure('another-missing-jpleph')
    call pleph(2459580.5_real64,10,3,vector,status)
    call require(status.eq.JPL_STATUS_IO_ERROR,'missing file status')
    call require(all(vector.eq.0.0_real64),'failed PLEPH zero output')
    call require(.not.bary,'failed PLEPH restores BARY')
  end subroutine test_missing_fallback

  subroutine test_analytic_failure()
    real(real64) :: outputs(8)
    integer :: result

    call configure('missing-analytic-failure.jpl')
    call ephem_values(1.0e200_real64,0,outputs,result)
    call require(result.eq.EPHEMERIS_UNAVAILABLE,                    &
         'analytic failure status')
    call require(all(outputs.eq.0.0_real64),                         &
         'analytic failure zero outputs')
  end subroutine test_analytic_failure

  subroutine test_truncated_header(path)
    character(len=*), intent(in) :: path
    integer(int8) :: bytes(128)
    integer :: input,output,ios,result
    real(real64) :: values(8)

    open(newunit=input,file=path,access='stream',form='unformatted',  &
         status='old',action='read')
    read(input,iostat=ios) bytes
    call require(ios.eq.0,'read source header prefix')
    close(input)
    open(newunit=output,file='truncated-header.jpl',access='stream',  &
         form='unformatted',status='replace',action='write')
    write(output) bytes
    close(output)
    call configure('truncated-header.jpl')
    call ephem_values(60310.5_real64,0,values,result)
    call require(result.eq.EPHEMERIS_ANALYTIC_FALLBACK,              &
         'truncated header fallback')
  end subroutine test_truncated_header

  subroutine test_truncated_coefficients(path)
    character(len=*), intent(in) :: path
    character(len=6) :: names(1000)
    integer :: result,status,n
    real(real64) :: values(8),constants(1000),range(3),vector(6)

    call copy_records(path,'header-only.jpl',2)
    call configure('header-only.jpl')
    call ephem_values(60310.5_real64,0,values,result)
    call require(result.eq.EPHEMERIS_ANALYTIC_FALLBACK,              &
         'missing coefficient record fallback')

    call configure(path)
    call const(names,constants,range,n)
    call require(n.gt.0,'read source ephemeris range')
    call copy_records(path,'truncated-coefficients.jpl',3)
    call truncate_last_record('truncated-coefficients.jpl')
    call configure('truncated-coefficients.jpl')
    call pleph(range(1)+0.5_real64*range(3),10,3,vector,status)
    call require(status.eq.JPL_STATUS_IO_ERROR,                      &
         'partial third coefficient record status')
    call require(all(vector.eq.0.0_real64),                          &
         'partial coefficient read zero output')
  end subroutine test_truncated_coefficients

  subroutine test_absurd_coefficients(path)
    character(len=*), intent(in) :: path
    real(real64) :: fallback(8),forced(8),mjd
    integer :: result

    call copy_records(path,'absurd-coefficients.jpl',3)
    call rewrite_absurd_lunar_coefficients('absurd-coefficients.jpl',mjd)
    call configure('absurd-coefficients.jpl')
    call ephem_values(mjd,0,fallback,result)
    call require(result.eq.EPHEMERIS_ANALYTIC_FALLBACK,              &
         'absurd finite coefficients trigger fallback')
    call require(all(ieee_is_finite(fallback)),                     &
         'absurd coefficient fallback remains finite')
    call ephem_values(mjd,8,forced,result)
    call require(maxval(abs(fallback-forced)).lt.1.0e-10_real64,    &
         'absurd coefficient fallback is atomic')
  end subroutine test_absurd_coefficients

  subroutine test_invalid_header(path)
    character(len=*), intent(in) :: path
    integer :: result,status
    real(real64) :: values(8),vector(6),record_epoch

    call copy_records(path,'invalid-header.jpl',3)
    call rewrite_header('invalid-header.jpl',.true.,.false.,.false.)
    call configure('invalid-header.jpl')
    call ephem_values(60310.5_real64,0,values,result)
    call require(result.eq.EPHEMERIS_ANALYTIC_FALLBACK,              &
         'invalid header fallback')

    call copy_records(path,'invalid-ncon.jpl',3)
    call rewrite_ncon('invalid-ncon.jpl')
    call configure('invalid-ncon.jpl')
    call ephem_values(60310.5_real64,0,values,result)
    call require(result.eq.EPHEMERIS_ANALYTIC_FALLBACK,              &
         'oversized constant count fallback')

    call copy_records(path,'invalid-coefficient-count.jpl',3)
    call rewrite_header('invalid-coefficient-count.jpl',.false.,     &
         .false.,.true.)
    call configure('invalid-coefficient-count.jpl')
    call ephem_values(60310.5_real64,0,values,result)
    call require(result.eq.EPHEMERIS_ANALYTIC_FALLBACK,              &
         'oversized interpolation coefficient count fallback')

    call copy_records(path,'invalid-record-step.jpl',3)
    call rewrite_header('invalid-record-step.jpl',.false.,.false.,   &
         .false.,.true.,record_epoch)
    call configure('invalid-record-step.jpl')
    call pleph(record_epoch,10,3,vector,status)
    call require(status.eq.JPL_STATUS_INVALID_HEADER,                &
         'unrepresentable record index status')
    call require(all(vector.eq.0.0_real64),                          &
         'unrepresentable record index zero output')
  end subroutine test_invalid_header

  subroutine test_out_of_range(path)
    character(len=*), intent(in) :: path
    real(real64) :: vector(6)
    integer :: status

    call configure(path)
    call pleph(1.0_real64,10,3,vector,status)
    call require(status.eq.JPL_STATUS_OUT_OF_RANGE,'early range status')
    call pleph(1.0e9_real64,10,3,vector,status)
    call require(status.eq.JPL_STATUS_OUT_OF_RANGE,'late range status')
    call pleph(2459580.5_real64,16,3,vector,status)
    call require(status.eq.JPL_STATUS_INVALID_REQUEST,               &
         'invalid target status')
    call require(all(vector.eq.0.0_real64),'invalid target zero output')
    call pleph(2459580.5_real64,16,16,vector,status)
    call require(status.eq.JPL_STATUS_INVALID_REQUEST,               &
         'equal invalid target status')
  end subroutine test_out_of_range

  subroutine test_nonfinite_epoch(path)
    character(len=*), intent(in) :: path
    real(real64) :: vector(6),epoch
    integer :: status

    call configure(path)
    epoch=ieee_value(0.0_real64,ieee_quiet_nan)
    call pleph(epoch,10,3,vector,status)
    call require(status.eq.JPL_STATUS_INVALID_REQUEST,'NaN epoch status')
    call require(all(vector.eq.0.0_real64),'NaN epoch zero output')

    epoch=ieee_value(0.0_real64,ieee_positive_inf)
    call pleph(epoch,10,3,vector,status)
    call require(status.eq.JPL_STATUS_INVALID_REQUEST,               &
         'infinite epoch status')
    call require(all(vector.eq.0.0_real64),'infinite epoch zero output')
  end subroutine test_nonfinite_epoch

  subroutine test_invalid_input()
    real :: outputs(8),astro_outputs(19)
    real :: uth
    integer :: result,values(8),year,month,day,ntsky

    call moon_values(2024,2,30,12.0,outputs,result)
    call require(result.eq.EPHEMERIS_INVALID_INPUT,'invalid date status')
    call require(all(outputs.eq.0.0),'invalid date zero outputs')
    call moon_values(2024,1,1,24.0,outputs,result)
    call require(result.eq.EPHEMERIS_INVALID_INPUT,'invalid UT status')
    call require(all(outputs.eq.0.0),'invalid UT zero outputs')
    call direct_astro_values(24.0,astro_outputs,ntsky,result)
    call require(result.eq.EPHEMERIS_INVALID_INPUT,                  &
         'astro propagates integer invalid status')
    call require(ntsky.eq.0 .and. all(astro_outputs.eq.0.0),         &
         'astro clears invalid derived outputs')

    values=[2024,1,1,60,0,15,0,0]
    call normalize_utc_calendar(values,year,month,day,uth)
    call require(year.eq.2023 .and. month.eq.12 .and. day.eq.31,     &
         'UTC calendar borrows across year')
    call require(abs(uth-23.25).lt.1.0e-5,'UTC hour after borrow')

    values=[2023,12,31,-60,23,45,0,0]
    call normalize_utc_calendar(values,year,month,day,uth)
    call require(year.eq.2024 .and. month.eq.1 .and. day.eq.1,       &
         'UTC calendar carries across year')
    call require(abs(uth-0.75).lt.1.0e-5,'UTC hour after carry')

    values=[2024,3,1,60,0,30,0,0]
    call normalize_utc_calendar(values,year,month,day,uth)
    call require(year.eq.2024 .and. month.eq.2 .and. day.eq.29,      &
         'UTC calendar handles leap day')
  end subroutine test_invalid_input

  subroutine test_blank_grid_compatibility(path)
    character(len=*), intent(in) :: path
    character(len=6) :: blank_grid='      '
    character(len=6) :: legacy_grid='BB44mm'
    character(len=6) :: valid_grid='FN20qi'
    character(len=6) :: dx_grid='JN18du'
    real(real64) :: blank_values(20),legacy_values(20),blank_dx_values(20)
    real(real64) :: both_blank_values(20)
    real(c_double) :: blank_sub_values(14),legacy_sub_values(14)
    real(c_double) :: blank_dx_sub_values(14)
    integer :: blank_integers(3),legacy_integers(3),blank_dx_integers(3)
    integer :: both_blank_integers(3),result
    integer(c_int) :: blank_sub_integers(3),legacy_sub_integers(3)
    integer(c_int) :: blank_dx_sub_integers(3),sub_result

    call configure(path)
    call grid_astro_values(blank_grid,dx_grid,blank_values,           &
         blank_integers,result)
    call require(result.eq.EPHEMERIS_JPL,'blank local grid status')

    call grid_astro_values(legacy_grid,dx_grid,legacy_values,         &
         legacy_integers,result)
    call require(result.eq.EPHEMERIS_JPL,'legacy local grid status')
    call require(maxval(abs(blank_values(1:15)-                      &
         legacy_values(1:15))).lt.1.0e-10_real64,                    &
         'blank local grid legacy values')
    call require(maxval(abs(blank_values(17:20)-                     &
         legacy_values(17:20))).lt.1.0e-10_real64,                   &
         'blank local grid legacy widths and delay')
    call require(all(blank_integers.eq.legacy_integers),             &
         'blank local grid legacy integer values')

    call astrosub_values(blank_grid,dx_grid,path,blank_sub_values,    &
         blank_sub_integers,sub_result)
    call require(sub_result.eq.EPHEMERIS_JPL,                        &
         'blank local grid astrosub status')
    call astrosub_values(legacy_grid,dx_grid,path,legacy_sub_values,  &
         legacy_sub_integers,sub_result)
    call require(sub_result.eq.EPHEMERIS_JPL,                        &
         'legacy local grid astrosub status')
    call require(maxval(abs(blank_sub_values-legacy_sub_values)).lt. &
         1.0e-10_real64,'blank local grid astrosub legacy values')
    call require(all(blank_sub_integers.eq.legacy_sub_integers),     &
         'blank local grid astrosub legacy integer values')

    call grid_astro_values(valid_grid,blank_grid,blank_dx_values,     &
         blank_dx_integers,result)
    call require(result.eq.EPHEMERIS_JPL,'blank DX grid status')
    call require(blank_dx_values(18).eq.blank_dx_values(17),         &
         'blank DX grid uses self width')
    call require(blank_dx_values(14).eq.0.0_real64,                  &
         'blank DX grid clears MNR')

    call astrosub_values(valid_grid,blank_grid,path,                 &
         blank_dx_sub_values,blank_dx_sub_integers,sub_result)
    call require(sub_result.eq.EPHEMERIS_JPL,                        &
         'blank DX grid astrosub status')
    call require(all(blank_dx_sub_values([5,6,11,14]).eq.            &
         0.0_real64),'blank DX grid clears astrosub DX values')
    call require(blank_dx_sub_integers(2).eq.0,                      &
         'blank DX grid clears astrosub Doppler')
    call require(maxval(abs(blank_dx_sub_values(1:4)-                &
         blank_dx_values(1:4))).lt.1.0e-10_real64,                  &
         'blank DX grid preserves local azimuth and elevation')
    call require(blank_dx_sub_values(13).eq.blank_dx_values(17),     &
         'blank DX grid preserves self width')
    call require(blank_dx_sub_integers(1).eq.blank_dx_integers(1)   &
         .and. blank_dx_sub_integers(3).eq.blank_dx_integers(3),    &
         'blank DX grid preserves local integer values')

    call grid_astro_values(blank_grid,blank_grid,both_blank_values,   &
         both_blank_integers,result)
    call require(result.eq.EPHEMERIS_JPL,'both grids blank status')
    call require(all(ieee_is_finite(both_blank_values)),             &
         'both grids blank finite values')
    call require(both_blank_values(20).gt.0.0_real64,                &
         'both grids blank echo delay')

    call configure('missing-blank-grid.jpl')
    call grid_astro_values(blank_grid,blank_grid,both_blank_values,   &
         both_blank_integers,result)
    call require(result.eq.EPHEMERIS_ANALYTIC_FALLBACK,              &
         'blank grids preserve analytic fallback')
    call astrosub_values(blank_grid,blank_grid,                       &
         'missing-blank-grid.jpl',blank_dx_sub_values,               &
         blank_dx_sub_integers,sub_result)
    call require(sub_result.eq.EPHEMERIS_ANALYTIC_FALLBACK,          &
         'blank grids preserve astrosub analytic fallback')
    call require(all(blank_dx_sub_values([5,6,11,14]).eq.            &
         0.0_real64) .and. blank_dx_sub_integers(2).eq.0,            &
         'blank grids preserve astrosub DX clearing')
  end subroutine test_blank_grid_compatibility

  subroutine test_optional_sections(path)
    character(len=*), intent(in) :: path
    real(real64) :: vector(6)
    integer :: status

    call copy_records(path,'no-optional-sections.jpl',3)
    call rewrite_header('no-optional-sections.jpl',.false.,.true.,   &
         .false.)
    call configure('no-optional-sections.jpl')
    call pleph(2459580.5_real64,14,0,vector,status)
    call require(status.eq.JPL_STATUS_NOT_AVAILABLE,                 &
         'absent nutation status')
    call pleph(2459580.5_real64,15,0,vector,status)
    call require(status.eq.JPL_STATUS_NOT_AVAILABLE,                 &
         'absent libration status')
  end subroutine test_optional_sections

  subroutine test_source_transition(path)
    character(len=*), intent(in) :: path
    real(real64) :: dfdt,dfdt0
    integer :: result
    real :: fspread_self,fspread_dx
    common/echocom2/fspread_self,fspread_dx

    call configure(path)
    call astro_values(12.0_real64,dfdt,dfdt0,result)
    call require(result.eq.EPHEMERIS_JPL,'transition starts with JPL')
    call astro_values(12.001_real64,dfdt,dfdt0,result)
    call require(abs(dfdt)+abs(dfdt0).gt.0.0_real64,                 &
         'transition establishes derivative history')
    call configure(path)
    call astro_values(12.002_real64,dfdt,dfdt0,result)
    call require(result.eq.EPHEMERIS_JPL,'reader reset retains JPL')
    call require(dfdt.eq.0.0_real64 .and. dfdt0.eq.0.0_real64,       &
         'reader generation resets derivative')
    call astro_values(24.0_real64,dfdt,dfdt0,result)
    call require(result.eq.EPHEMERIS_INVALID_INPUT,                  &
         'transition rejects invalid input')
    call require(fspread_self.eq.0.0 .and. fspread_dx.eq.0.0,        &
         'invalid input clears saved echo spreads')
    call astro_values(12.003_real64,dfdt,dfdt0,result)
    call require(result.eq.EPHEMERIS_JPL,'transition recovers JPL')
    call require(dfdt.eq.0.0_real64 .and. dfdt0.eq.0.0_real64,       &
         'invalid input resets derivative history')
    call configure('missing-transition.jpl')
    call astro_values(12.004_real64,dfdt,dfdt0,result)
    call require(result.eq.EPHEMERIS_ANALYTIC_FALLBACK,              &
         'transition enters fallback')
    call require(dfdt.eq.0.0_real64 .and. dfdt0.eq.0.0_real64,       &
         'source transition resets derivative')
    call astro_values(12.005_real64,dfdt,dfdt0,result)
    call require(result.eq.EPHEMERIS_ANALYTIC_FALLBACK,              &
         'failed configuration remains in fallback')
    call configure(path)
    call astro_values(12.006_real64,dfdt,dfdt0,result)
    call require(result.eq.EPHEMERIS_JPL,                            &
         'configuration reset retries JPL')
    call require(dfdt.eq.0.0_real64 .and. dfdt0.eq.0.0_real64,       &
         'configuration reset clears derivative history')
  end subroutine test_source_transition

  subroutine configure(path)
    character(len=*), intent(in) :: path
    character(len=256) :: configured

    configured=path
    call jpl_setup(configured)
  end subroutine configure

  subroutine ephem_values(mjd,nspecial,values,result)
    real(real64), intent(in) :: mjd
    integer, intent(in) :: nspecial
    real(real64), intent(out) :: values(8)
    integer, intent(out) :: result

    call ephem(mjd,-0.460_real64,-1.8326_real64,0.7106_real64,      &
         40.0_real64,nspecial,values(1),values(2),values(3),        &
         values(4),values(5),values(6),values(7),values(8),result)
  end subroutine ephem_values

  subroutine moon_values(year,month,day,uth,outputs,result)
    integer, intent(in) :: year,month,day
    real, intent(in) :: uth
    real, intent(out) :: outputs(8)
    integer, intent(out) :: result

    call MoonDopJPL(year,month,day,uth,-105.0,40.0,outputs(1),       &
         outputs(2),outputs(3),outputs(4),outputs(5),outputs(6),    &
         outputs(7),outputs(8),result)
  end subroutine moon_values

  subroutine direct_astro_values(uth,outputs,ntsky,result)
    real, intent(in) :: uth
    real, intent(out) :: outputs(19)
    integer, intent(out) :: ntsky,result
    character(len=6) :: grid='FN20qi'

    call astro(2024,1,1,uth,144000000.0_real64,grid,1,1,            &
         outputs(1),outputs(2),outputs(3),outputs(4),ntsky,         &
         outputs(5),outputs(6),outputs(7),outputs(8),outputs(9),   &
         outputs(10),outputs(11),outputs(12),outputs(13),          &
         outputs(14),outputs(15),outputs(16),outputs(17),          &
         outputs(18),outputs(19),result)
  end subroutine direct_astro_values

  subroutine astro_values(uth,dfdt,dfdt0,result)
    real(real64), intent(in) :: uth
    real(real64), intent(out) :: dfdt,dfdt0
    integer, intent(out) :: result
    real(real64) :: output(17),width1,width2,techo
    integer :: ntsky,ndop,ndop00
    character(len=6) :: mygrid='FN20qi',hisgrid='JN18du'

    call astro0(2024,1,1,uth,144000000.0_real64,mygrid,hisgrid,      &
         output(1),output(2),output(3),output(4),output(5),          &
         output(6),ntsky,ndop,ndop00,output(7),output(8),output(9), &
         output(10),output(11),output(12),output(13),output(14),    &
         dfdt,dfdt0,width1,width2,output(15),techo,result)
  end subroutine astro_values

  subroutine grid_astro_values(mygrid,hisgrid,values,integer_values,  &
       result)
    character(len=6), intent(in) :: mygrid,hisgrid
    real(real64), intent(out) :: values(20)
    integer, intent(out) :: integer_values(3),result

    call astro0(2024,1,1,12.0_real64,144000000.0_real64,mygrid,      &
         hisgrid,values(1),values(2),values(3),values(4),values(5),  &
         values(6),integer_values(1),integer_values(2),              &
         integer_values(3),values(7),values(8),values(9),values(10),&
         values(11),values(12),values(13),values(14),values(15),    &
         values(16),values(17),values(18),values(19),values(20),    &
         result)
  end subroutine grid_astro_values

  subroutine astrosub_values(mygrid,hisgrid,jpl_path,values,          &
       integer_values,result)
    character(len=*), intent(in) :: mygrid,hisgrid,jpl_path
    real(c_double), intent(out) :: values(14)
    integer(c_int), intent(out) :: integer_values(3),result
    character(kind=c_char), target :: mygrid_c(7),hisgrid_c(7)
    character(kind=c_char), target :: azel_file_c(64),jpl_path_c(1025)

    call assign_c_string(mygrid,mygrid_c)
    call assign_c_string(hisgrid,hisgrid_c)
    call assign_c_string('blank-grid-azel.dat',azel_file_c)
    call assign_c_string(jpl_path,jpl_path_c)
    call astrosub(2024_c_int,1_c_int,1_c_int,12.0_c_double,           &
         144000000.0_c_double,c_loc(mygrid_c),c_loc(hisgrid_c),      &
         values(1),values(2),values(3),values(4),values(5),          &
         values(6),integer_values(1),integer_values(2),              &
         integer_values(3),values(7),values(8),values(9),values(10),&
         values(11),.false._c_bool,values(12),values(13),values(14), &
         .false._c_bool,c_loc(azel_file_c),c_loc(jpl_path_c),result)
  end subroutine astrosub_values

  subroutine assign_c_string(value,buffer)
    character(len=*), intent(in) :: value
    character(kind=c_char), intent(out) :: buffer(:)
    integer :: i,count

    buffer=c_null_char
    count=min(len_trim(value),size(buffer)-1)
    do i=1,count
       buffer(i)=value(i:i)
    enddo
  end subroutine assign_c_string

  subroutine copy_records(source,destination,count)
    character(len=*), intent(in) :: source,destination
    integer, intent(in) :: count
    integer(int8) :: record(8144)
    integer :: input,output,index,ios

    open(newunit=input,file=source,access='stream',form='unformatted',&
         status='old',action='read')
    open(newunit=output,file=destination,access='stream',             &
         form='unformatted',status='replace',action='write')
    do index=1,count
       read(input,iostat=ios) record
       call require(ios.eq.0,'copy source record')
       write(output) record
    enddo
    close(input)
    close(output)
  end subroutine copy_records

  subroutine truncate_last_record(path)
    character(len=*), intent(in) :: path
    integer(int8) :: bytes(2*8144+256)
    integer :: input,output,ios

    open(newunit=input,file=path,access='stream',form='unformatted', &
         status='old',action='read')
    read(input,iostat=ios) bytes
    call require(ios.eq.0,'read truncated coefficient prefix')
    close(input)
    open(newunit=output,file=path,access='stream',form='unformatted',&
         status='replace',action='write')
    write(output) bytes
    close(output)
  end subroutine truncate_last_record

  subroutine rewrite_absurd_lunar_coefficients(path,mjd)
    character(len=*), intent(in) :: path
    real(real64), intent(out) :: mjd
    character(len=6) :: ttl(14,3),cnam(1000)
    real(real64) :: ss(3),au,emrat,coefficients(1018)
    integer :: unit,ios,ncon,numde,ipt(3,13),index,j,set_index,first,ncf

    open(newunit=unit,file=path,access='direct',form='unformatted',   &
         recl=8144,status='old',action='readwrite')
    read(unit,rec=1,iostat=ios) ttl,(cnam(index),index=1,400),ss,     &
         ncon,au,emrat,((ipt(index,j),index=1,3),j=1,12),numde,      &
         (ipt(index,13),index=1,3)
    call require(ios.eq.0,'read absurd coefficient header')
    read(unit,rec=3,iostat=ios) coefficients
    call require(ios.eq.0,'read coefficient record for corruption')
    ncf=ipt(2,10)
    do set_index=1,ipt(3,10)
       first=ipt(1,10)+(set_index-1)*3*ncf
       coefficients(first:first+3*ncf-1)=0.0_real64
       coefficients(first)=1.0e200_real64
       coefficients(first+ncf)=1.0e200_real64
       coefficients(first+2*ncf)=1.0e200_real64
    enddo
    write(unit,rec=3,iostat=ios) coefficients
    call require(ios.eq.0,'write absurd lunar coefficients')
    close(unit)
    mjd=ss(1)+0.5_real64*ss(3)-2400000.5_real64
  end subroutine rewrite_absurd_lunar_coefficients

  subroutine rewrite_header(path,invalid_range,remove_optional,     &
       invalid_coefficient_count,tiny_record_step,record_epoch)
    character(len=*), intent(in) :: path
    logical, intent(in) :: invalid_range,remove_optional
    logical, intent(in) :: invalid_coefficient_count
    logical, intent(in), optional :: tiny_record_step
    real(real64), intent(out), optional :: record_epoch
    character(len=6) :: ttl(14,3),cnam(1000)
    real(real64) :: ss(3),au,emrat
    integer :: unit,ios,ncon,numde,ipt(3,13),index,j

    open(newunit=unit,file=path,access='direct',form='unformatted',   &
         recl=8144,status='old',action='readwrite')
    read(unit,rec=1,iostat=ios) ttl,(cnam(index),index=1,400),ss,     &
         ncon,au,emrat,((ipt(index,j),index=1,3),j=1,12),numde,      &
         (ipt(index,13),index=1,3)
    call require(ios.eq.0,'read structured header')
    if(invalid_range) ss(3)=0.0_real64
    if(present(tiny_record_step)) then
       if(tiny_record_step) then
          ss(3)=0.25_real64/(4.0_real64*real(huge(0),real64))
          if(present(record_epoch)) record_epoch=ss(1)+0.25_real64
       endif
    endif
    if(remove_optional) ipt(:,12:13)=0
    if(invalid_coefficient_count) ipt(2,1)=19
    if(ncon.le.400) then
       write(unit,rec=1,iostat=ios) ttl,(cnam(index),index=1,400),ss,&
            ncon,au,emrat,((ipt(index,j),index=1,3),j=1,12),numde,   &
            (ipt(index,13),index=1,3)
    else
       write(unit,rec=1,iostat=ios) ttl,(cnam(index),index=1,400),ss,&
            ncon,au,emrat,((ipt(index,j),index=1,3),j=1,12),numde,   &
            (ipt(index,13),index=1,3),                              &
            (cnam(index),index=401,ncon)
    endif
    call require(ios.eq.0,'write structured header')
    close(unit)
  end subroutine rewrite_header

  subroutine rewrite_ncon(path)
    character(len=*), intent(in) :: path
    character(len=6) :: ttl(14,3),cnam(400)
    real(real64) :: ss(3),au,emrat
    integer :: unit,ios,ncon,numde,ipt(3,13),index,j

    open(newunit=unit,file=path,access='direct',form='unformatted',   &
         recl=8144,status='old',action='readwrite')
    read(unit,rec=1,iostat=ios) ttl,(cnam(index),index=1,400),ss,     &
         ncon,au,emrat,((ipt(index,j),index=1,3),j=1,12),numde,      &
         (ipt(index,13),index=1,3)
    call require(ios.eq.0,'read constant count header')
    ncon=1001
    write(unit,rec=1,iostat=ios) ttl,(cnam(index),index=1,400),ss,    &
         ncon,au,emrat,((ipt(index,j),index=1,3),j=1,12),numde,      &
         (ipt(index,13),index=1,3)
    call require(ios.eq.0,'write oversized constant count')
    close(unit)
  end subroutine rewrite_ncon

  subroutine require(condition,message)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: message

    if(.not.condition) then
       print '(a)', 'FAIL '//trim(message)
       error stop 1
    endif
  end subroutine require

end program test_jpl_resilience
