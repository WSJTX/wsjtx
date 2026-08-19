program test_map65_jt65_handoff

  use iso_fortran_env, only: real32, real64
  use, intrinsic :: ieee_arithmetic, only: ieee_is_finite
  use decode1a_mod
  use jt65_test_fixture
  use jt65_test_vectors
  use npar_ptrs_mod, only: nsmax_active
  implicit none

  real(real32), allocatable :: dd(:,:)
  real(real64), parameter :: map65_base_frequency = 118.0_real64 * 11025.0_real64 / 1024.0_real64

  allocate(dd(4, nsmax_active))
  call test_mode(dd, standard_tones, 1, 1, 'JT65A MAP65 handoff')
  call test_mode(dd, standard_tones, 2, 1, 'JT65B MAP65 handoff')
  call test_mode(dd, standard_tones, 4, 1, 'JT65C MAP65 handoff')
  call test_mode(dd, ooo_tones, 1, -1, 'JT65A OOO MAP65 handoff')
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

  subroutine test_mode(waveform, tones, mode65, polarity, description)
    real(real32), intent(inout) :: waveform(:,:)
    integer, intent(in) :: tones(:)
    integer, intent(in) :: mode65
    integer, intent(in) :: polarity
    character(len=*), intent(in) :: description
    integer :: newdat, nflip, nfsample, neme, ndepth, nqd, ndphi
    integer :: nutc, nkhz, ndf, ipol, ntol, nkv, nhist, nsum, nsave
    logical :: xpol
    character(len=12) :: mycall, hiscall
    character(len=6) :: hisgrid
    character(len=22) :: decoded
    real :: dphi, sync2, a(5), dt, pol, qual

    call make_map65_wave(waveform, tones, mode65, map65_base_frequency)
    newdat = 1
    nflip = polarity
    nfsample = 96000
    xpol = .false.
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
    call decode1a(waveform, newdat, map65_base_frequency, nflip, mode65, nfsample, xpol, mycall, hiscall, &
         hisgrid, neme, ndepth, nqd, dphi, ndphi, nutc, nkhz, ndf, ipol, ntol, sync2, &
         a, dt, pol, nkv, nhist, nsum, nsave, qual, decoded)

    call require(decoded == padded_message('K1ABC W9XYZ FN42'), description//' message')
    call require(newdat == 0, description//' consumes new data')
    call require(ieee_is_finite(sync2), description//' finite sync metric')
    call require(ieee_is_finite(dt), description//' finite timing')
    call require(ieee_is_finite(pol), description//' finite polarization')
    call require(ieee_is_finite(qual), description//' finite quality')
    call require(ieee_is_finite(a(1)) .and. ieee_is_finite(a(2)), description//' finite AFC')
  end subroutine test_mode

end program test_map65_jt65_handoff
