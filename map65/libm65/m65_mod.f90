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
  if(iand(nrxlog,2).ne.0) rewind(21)
  if(iand(nrxlog,4).ne.0) then
     if(nhsym.eq.nhsym1) rewind(26)
     if(nhsym.eq.nhsym2) backspace(26)
  endif

  nstandalone=0

      call decode0(nstandalone)

end subroutine m65c
end module m65_mod
