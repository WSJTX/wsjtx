module decodes_mod

    implicit none

    integer, parameter :: NLD = 32768
    ! from decodes
    integer :: ndecodes
    
    !from early 
    integer :: nhsym1, nhsym2

    logical, allocatable :: ldecoded(:)
    
    !from c3com
    integer :: mcall3a

    contains

      subroutine decodes_init()
        if (.not. allocated(ldecoded)) allocate(ldecoded(NLD))
      end subroutine decodes_init

end module decodes_mod
