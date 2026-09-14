module m65_mod

use iso_c_binding, only: c_int8_t,c_int,c_loc, c_ptr
use decodes_mod, only: nhsym1,nhsym2,ldecoded

implicit none

contains

subroutine m65c() bind(C)
  use decode0_mod
  use npar_ptrs_mod, only: nhsym, nrxlog, datetime
  integer :: npatience, nstandalone
  
  npatience=1
  if(nhsym.eq.nhsym1 .and. iand(nrxlog,1).ne.0) then
     write(21,1000) datetime(1:17)
1000 format(/'UTC Date: ', a17, /78('-'))
     flush(21)
  endif
  ! rewind alone doesn't truncate; endfile does, but backspace is needed after it to allow further appends.
  ! Guarded to nhsym1, same as unit 26 below: nrxlog stays set for the whole
  ! nhsym1..nhsym2 window (it's only cleared on the next C++ decode() call),
  ! and m65c() fires once per hsym in that window, so an unguarded truncate
  ! here would also wipe out decode records map65a.f90/q65b.F90 write to
  ! unit 21 later in the same window.
  if(nhsym.eq.nhsym1 .and. iand(nrxlog,2).ne.0) then
     rewind(21)
     endfile(21)
     backspace(21)
  endif
  if(iand(nrxlog,4).ne.0) then
     if(nhsym.eq.nhsym1) then
        rewind(26)
        endfile(26)
        backspace(26)
     endif
     if(nhsym.eq.nhsym2) backspace(26)
  endif

  nstandalone=0

      call decode0(nstandalone)

end subroutine m65c
end module m65_mod
