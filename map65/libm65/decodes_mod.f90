module decodes_mod

  use npar_ptrs_mod, only: nfft_active

  implicit none

  integer :: ndecodes = 0
  integer :: nhsym1 = 0, nhsym2 = 0
  logical, allocatable :: ldecoded(:)
  ! JT65 bins already decoded in this accumulation cycle. Retaining them
  ! across early/final calls avoids reporting the same signal twice;
  ! select_unique_decodes() only compares results within one call.
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
