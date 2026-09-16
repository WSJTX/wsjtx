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
  ! Consume the erase request once, independent of the decode pass.
  if(iand(nrxlog,2).ne.0) then
     rewind(21)
     endfile(21)
     backspace(21)
     nrxlog = ibclr(nrxlog,1)
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
