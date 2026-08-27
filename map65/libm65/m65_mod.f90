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
     ! rewind alone only repositions to record 1 for writing; it does not
     ! shrink the file, so old history beyond whatever gets written this
     ! cycle would stay on disk and could resurface via display.f90's own
     ! rewind+read the same way the missing endfile there did (see
     ! set_map65RxLog in npar_ptrs_mod.f90 for how Erase reaches here).
     ! endfile makes this an immediate, real truncation to empty -- but it
     ! leaves the file positioned after the EOF marker it just wrote, and
     ! Fortran forbids further sequential I/O from there without
     ! repositioning first, so backspace right away to land back on that
     ! marker, ready for the next append.
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
