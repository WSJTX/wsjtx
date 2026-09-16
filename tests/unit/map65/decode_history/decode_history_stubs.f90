! These stand-ins supply one successful signal per mode. The production
! map65a loop owns all JT65 suppression and both modes' history resets.
module decode_history_observations
  implicit none
  integer :: jt65_attempts = 0, q65_successes = 0
  integer :: phase_attempts(0:12) = 0
  integer :: summary_count = 0, fitted_phase = -999
  logical :: phase_has_signal(0:12) = .true.
end module

module stdout_channel_mod
  use decode_history_observations
  implicit none
contains
  subroutine write_stdout(line)
    character(len=*), intent(in) :: line
    if (index(line, '!Best-fit Dphi =') == 1) then
      summary_count = summary_count + 1
      read(line(index(line, '=')+1:), *) fitted_phase
    endif
  end subroutine
end module

module wideband_sync
  implicit none
  integer, parameter :: MAX_CANDIDATES = 50
  integer :: nkhz_center
  type candidate
    real :: snr, f, xdt, pol
    integer :: ipol, iflip, indx
  end type
contains
  subroutine init_wideband_sync()
  end subroutine

  subroutine get_candidates(ss, savg, xpol, jz, nfa, nfb, nts_jt65, nts_q65, cand, ncand)
    use npar_ptrs_mod, only: nfft_active
    real, intent(in) :: ss(4,322,nfft_active), savg(4,nfft_active)
    logical, intent(in) :: xpol
    integer, intent(in) :: jz, nfa, nfb, nts_jt65, nts_q65
    type(candidate), intent(out) :: cand(MAX_CANDIDATES)
    integer, intent(out) :: ncand
    ncand = 1
    ! With nfcal=-1270, this places the Q65 signal at the zero-kHz cursor.
    cand(1) = candidate(10.0, 48.00046, 0.0, 0.0, 1, 0, 1)
  end subroutine
end module

module ccf65_legacy_mod
  implicit none
contains
  subroutine ccf65(ss_plane, nhsym, ssmax, sync1, ipol1, jpz, dt1, flipk, &
                   syncshort, snr2, ipol2, dt2)
    real, intent(in) :: ss_plane(4,322), ssmax
    integer, intent(in) :: nhsym, jpz
    real, intent(out) :: sync1, dt1, flipk, syncshort, snr2, dt2
    integer, intent(out) :: ipol1, ipol2
    sync1 = 10.0
    dt1 = 0.0
    flipk = 1.0
    syncshort = -99.0
    snr2 = 0.0
    dt2 = 0.0
    ipol1 = 1
    ipol2 = 1
  end subroutine
end module

module decode1a_mod
  use iso_fortran_env, only: real64
  use decode_history_observations
  implicit none
contains
  subroutine decode1a(dd,newdat,f0,nflip,mode65,nfsample,xpol, &
       mycall,hiscall,hisgrid,neme,ndepth,nqd,dphi,ndphi, &
       nutc,nkhz,ndf,ipol,ntol,sync2,a,dt,pol,nkv,nhist,nsum,nsave,qual,decoded)
    use npar_ptrs_mod, only: nsmax_active
    real, intent(in) :: dd(4,nsmax_active), dphi
    real(real64), intent(in) :: f0
    integer, intent(in) :: nflip, mode65, nfsample, neme, ndepth, nqd, ndphi, nutc, nkhz, ndf, ntol
    logical, intent(in) :: xpol
    character(len=12), intent(in) :: mycall, hiscall
    character(len=6), intent(in) :: hisgrid
    integer, intent(inout) :: newdat, ipol, nkv, nhist, nsum, nsave
    real, intent(inout) :: sync2, a(5), dt, pol, qual
    character(len=22), intent(out) :: decoded
    integer :: trial
    newdat = 0
    decoded = ' '
    if (mode65 == 0) return
    jt65_attempts = jt65_attempts + 1
    trial = nint(dphi * 57.2957795 / 30.0)
    if (ndphi == 1) then
      phase_attempts(trial) = phase_attempts(trial) + 1
      if (.not. phase_has_signal(trial)) return
    endif
    decoded = 'K1ABC W9XYZ FN42'
    sync2 = 100.0
    a = 0.0
    dt = 0.0
    pol = 0.0
    qual = 5.0 + 4.0*sin(dphi)
    nkv = 1
    nhist = 0
    nsum = 0
    nsave = 0
    ipol = 1
  end subroutine
end module

module q65b_mod
  use iso_fortran_env, only: real64
  use decode_history_observations
  implicit none
contains
  subroutine q65b(nutc,nqd,nxant,fcenter,nfcal,nfsample,ikhz,mousedf,ntol,xpol,configured_dphi_deg, &
                 mycall,mygrid,hiscall,hisgrid,mode_q65,f0,fqso,newdat,nagain,max_drift,ndop00,idec,cursor_fallback)
    use decodes_mod, only: ldecoded
    use q65_decode, only: nsnr0
    integer, intent(in) :: nutc,nqd,nxant,nfcal,nfsample,ikhz,mousedf,ntol,mode_q65
    integer, intent(in) :: newdat,nagain,max_drift,ndop00,configured_dphi_deg
    real(real64), intent(in) :: fcenter,f0
    real, intent(in) :: fqso
    logical, intent(in) :: xpol
    logical, optional, intent(in) :: cursor_fallback
    character(len=12), intent(in) :: mycall,hiscall
    character(len=6), intent(in) :: mygrid,hisgrid
    integer, intent(out) :: idec
    nsnr0 = -99
    idec = -1
    ! Model q65b's successful-bin guard, not its signal processing. This
    ! observes whether the caller retained or discarded the Q65 history.
    if (ldecoded(1)) return
    ldecoded(1) = .true.
    q65_successes = q65_successes + 1
    nsnr0 = -10
    idec = 1
  end subroutine
end module

module display_mod
  implicit none
contains
  subroutine display(nkeep, ftol)
    integer, intent(in) :: nkeep
    real, intent(in) :: ftol
  end subroutine
end module

module sec0_mod
  implicit none
contains
  subroutine sec0(mode, t)
    integer, intent(in) :: mode
    real, intent(out) :: t
    ! The tests cover state transitions, not elapsed-time abort policy.
    t = 0.0
  end subroutine
end module
