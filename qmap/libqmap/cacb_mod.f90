module cacb_mod
  implicit none

  complex, allocatable :: ca(:)  ! FFT of raw I/Q data from Linrad

contains

  subroutine init_cacb(n)
    integer, intent(in) :: n

    if (allocated(ca)) then
      if (size(ca) >= n) return
      deallocate(ca)
    endif

    allocate(ca(n))
  end subroutine init_cacb

  subroutine free_cacb()
    if (allocated(ca)) deallocate(ca)
  end subroutine free_cacb

end module cacb_mod
