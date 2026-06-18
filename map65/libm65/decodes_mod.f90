module decodes_mod

    implicit none

    integer, parameter :: NLD = 32768
    ! from decodes
    integer :: ndecodes = 0
    
    !from early 
    integer :: nhsym1 = 0, nhsym2 = 0

    logical, allocatable :: ldecoded(:)
    
    !from c3com
    integer :: mcall3a = 0

    contains

      subroutine decodes_init()
        if (.not. allocated(ldecoded)) allocate(ldecoded(NLD))
      end subroutine decodes_init

end module decodes_mod
