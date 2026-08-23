program test_map65_jt65_handoff

  use iso_fortran_env, only: real32, real64
  use, intrinsic :: ieee_arithmetic, only: ieee_get_flag, ieee_invalid, ieee_is_finite, &
       ieee_quiet_nan, ieee_set_flag, ieee_value
  use decode1a_mod
  use filbig_mod
  use jt65_test_fixture
  use jt65_test_vectors
  use npar_ptrs_mod, only: nsmax_active
  implicit none

  real(real32), allocatable :: dd(:,:)
  real(real64), parameter :: map65_base_frequency = 118.0_real64 * 11025.0_real64 / 1024.0_real64

  allocate(dd(4, nsmax_active))
  call test_mono_filter_output(dd)
  call test_mode(dd, standard_tones, 1, 1, 'JT65A MAP65 handoff')
  call test_mode(dd, standard_tones, 2, 1, 'JT65B MAP65 handoff')
  call test_mode(dd, standard_tones, 4, 1, 'JT65C MAP65 handoff')
  call test_mode(dd, ooo_tones, 1, -1, 'JT65A OOO MAP65 handoff')
  call test_mode(dd, standard_tones, 2, 1, 'JT65B xpol MAP65 handoff', .true.)
  print '(a)', 'MAP65 JT65 handoff tests passed'

contains

  subroutine require(condition, description)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: description

    if (.not. condition) then
       print '(a)', 'FAIL: '//description
       error stop 1
    end if
  end subroutine require

  subroutine test_mono_filter_output(waveform)
    real(real32), intent(inout) :: waveform(:,:)
    complex(real32), allocatable :: cx(:), cy(:)
    integer :: newdat, n5
    logical :: invalid_raised

    allocate(cx(nsmax_active/64), cy(nsmax_active/64))
    call make_map65_wave(waveform, standard_tones, 2, map65_base_frequency)
    waveform(3:4,:) = ieee_value(0.0_real32, ieee_quiet_nan)
    newdat = 1
    call ieee_set_flag(ieee_invalid, .false.)
    call filbig(waveform, nsmax_active, map65_base_frequency, newdat, 96000, .false., cx, cy, n5)
    call ieee_get_flag(ieee_invalid, invalid_raised)

    call require(.not. invalid_raised, 'mono filter avoids invalid inactive-channel arithmetic')
    call require(all(ieee_is_finite(real(cy(:n5)))) .and. &
         all(ieee_is_finite(aimag(cy(:n5)))), 'mono filter output is finite')
    call require(all(abs(cy(:n5)) == 0.0), 'mono filter output is zero')
  end subroutine test_mono_filter_output

  subroutine test_mode(waveform, tones, mode65, polarity, description, cross_polarized)
    real(real32), intent(inout) :: waveform(:,:)
    integer, intent(in) :: tones(:)
    integer, intent(in) :: mode65
    integer, intent(in) :: polarity
    character(len=*), intent(in) :: description
    logical, intent(in), optional :: cross_polarized
    integer :: newdat, nflip, nfsample, neme, ndepth, nqd, ndphi
    integer :: nutc, nkhz, ndf, ipol, ntol, nkv, nhist, nsum, nsave
    logical :: invalid_raised, xpol
    character(len=12) :: mycall, hiscall
    character(len=6) :: hisgrid
    character(len=22) :: decoded
    real :: dphi, sync2, a(5), dt, pol, qual

    call make_map65_wave(waveform, tones, mode65, map65_base_frequency)
    xpol = .false.
    if (present(cross_polarized)) xpol = cross_polarized
    if (xpol) then
       waveform(3:4,:) = waveform(1:2,:)
    else
       waveform(3:4,:) = ieee_value(0.0_real32, ieee_quiet_nan)
    endif
    newdat = 1
    nflip = polarity
    nfsample = 96000
    mycall = 'K1ABC       '
    hiscall = 'W9XYZ       '
    hisgrid = 'FN42  '
    neme = 0
    ndepth = 3
    nqd = 1
    dphi = 0.0
    ndphi = 0
    nutc = 1
    nkhz = 1
    ndf = 0
    ipol = 0
    ntol = 100
    sync2 = 0.0
    a = 0.0
    dt = -1.7
    pol = 0.0
    nkv = -1
    nhist = -1
    nsum = 0
    nsave = 0
    qual = -999.0
    decoded = '                      '
    call ieee_set_flag(ieee_invalid, .false.)
    call decode1a(waveform, newdat, map65_base_frequency, nflip, mode65, nfsample, xpol, mycall, hiscall, &
         hisgrid, neme, ndepth, nqd, dphi, ndphi, nutc, nkhz, ndf, ipol, ntol, sync2, &
         a, dt, pol, nkv, nhist, nsum, nsave, qual, decoded)
    call ieee_get_flag(ieee_invalid, invalid_raised)

    call require(decoded == padded_message('K1ABC W9XYZ FN42'), description//' message')
    call require(.not. invalid_raised, description//' avoids invalid arithmetic')
    call require(newdat == 0, description//' consumes new data')
    call require(ieee_is_finite(sync2), description//' finite sync metric')
    call require(ieee_is_finite(dt), description//' finite timing')
    call require(ieee_is_finite(pol), description//' finite polarization')
    call require(ieee_is_finite(qual), description//' finite quality')
    call require(ieee_is_finite(a(1)) .and. ieee_is_finite(a(2)), description//' finite AFC')
  end subroutine test_mode

end program test_map65_jt65_handoff
