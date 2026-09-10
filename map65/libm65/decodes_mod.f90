module decodes_mod

  use npar_ptrs_mod, only: nfft_active

  implicit none

  integer :: ndecodes = 0
  integer :: nhsym1 = 0, nhsym2 = 0
  logical, allocatable :: ldecoded(:)
  ! 2026-09-10: JT65's analog of ldecoded above. ldecoded tracks which bins
  ! Q65 has already decoded THIS accumulation cycle, so a repeat automatic
  ! call (run_m65 legitimately re-firing map65a() before nhsym itself has
  ! advanced -- see map65a.f90's nhsym_prev_call handling) doesn't
  ! re-discover and re-report the same signal. JT65 candidates never had an
  ! equivalent: their only dedup is select_unique_decodes(), which only
  ! looks WITHIN one map65a() call's own accumulated candidates, blind to
  ! what an earlier, separate call already emitted. Confirmed in testing:
  ! live UDP streaming's repeat early-pass firings duplicate a JT65 decode
  ! ("!" write_stdout line, hence a duplicate in the Messages window) the
  ! same way ldecoded was protecting Q65 from before this fix.
  logical, allocatable :: ljt65decoded(:)
  integer :: mcall3a = 0

contains

  subroutine decodes_init()
    ! (Re)allocate ldecoded/ljt65decoded to match the active symspec FFT length.
    if (allocated(ldecoded)) then
       if (size(ldecoded) /= nfft_active) then
          deallocate(ldecoded)
       end if
    end if

    if (.not. allocated(ldecoded)) then
       allocate(ldecoded(nfft_active))
       ldecoded = .false.
    end if

    if (allocated(ljt65decoded)) then
       if (size(ljt65decoded) /= nfft_active) then
          deallocate(ljt65decoded)
       end if
    end if

    if (.not. allocated(ljt65decoded)) then
       allocate(ljt65decoded(nfft_active))
       ljt65decoded = .false.
    end if
  end subroutine decodes_init

end module decodes_mod
