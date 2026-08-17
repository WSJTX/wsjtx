module decodes_mod

  use npar_ptrs_mod, only: nfft_active

  implicit none

  integer :: ndecodes = 0
  integer :: nhsym1 = 0, nhsym2 = 0
  logical, allocatable :: ldecoded(:)
  integer :: mcall3a = 0

contains

  subroutine decodes_init()
    ! (Re)allocate ldecoded to match the active symspec FFT length.
    if (allocated(ldecoded)) then
       if (size(ldecoded) /= nfft_active) then
          deallocate(ldecoded)
       end if
    end if

    if (.not. allocated(ldecoded)) then
       allocate(ldecoded(nfft_active))
       ldecoded = .false.
    end if
  end subroutine decodes_init

end module decodes_mod
