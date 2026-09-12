module m65_mod

use iso_c_binding, only: c_int8_t,c_int,c_loc, c_ptr
use decodes_mod, only: nhsym1,nhsym2,ldecoded

implicit none

contains

subroutine m65c() bind(C)
  use decode0_mod
  use npar_ptrs_mod, only: nhsym, nrxlog, datetime
  use debug_log, only: dbg, itoa, rtoa
  use sec_midn_mod, only: sec_midn
  integer :: npatience, nstandalone
  ! 2026-09-12: mirrors map65a.f90's nhsym_prev_call pattern for
  ! ldecoded/ljt65decoded resets -- see the fix note below.
  integer, save :: nhsym_prev_m65c = -1

  npatience=1
  call dbg('m65c: ENTRY at t=' // rtoa(sec_midn()) // ' nhsym=' // itoa(nhsym) // &
           ' nrxlog=' // itoa(nrxlog) // ' bit4_set=' // itoa(merge(1,0,iand(nrxlog,4).ne.0)))
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
     !
     ! 2026-09-12 fix: only truncate on a genuinely NEW early-pass cycle
     ! (nhsym_prev_m65c.ne.nhsym1), not on every repeat automatic firing at
     ! the same nhsym1 -- confirmed via live-UDP instrumentation that
     ! nrxlog bit 4 (set once by "Erase Band Map and Messages") can still
     ! read as set across several internal repeat firings within the same
     ! cycle (mainwindow.cpp only clears it on the NEXT EarlyFinished/
     ! DecodeFinished, which can be several firings later), causing this
     ! branch to re-truncate unit 26 mid-cycle repeatedly. That wiped out
     ! an already-written Q65 decode (correctly never rewritten thanks to
     ! ldecoded) before the cycle's final consolidated display() call could
     ! show it -- observed as a Q65 signal decoding correctly, appearing
     ! briefly, then permanently vanishing from the Messages window despite
     ! being correctly present in map65_rx.log the whole time.
     if(nhsym.eq.nhsym1 .and. nhsym_prev_m65c.ne.nhsym1) then
        call dbg('m65c: UNIT26 TRUNCATED (endfile) at t=' // rtoa(sec_midn()) // &
                 ' nhsym=' // itoa(nhsym) // ' nrxlog=' // itoa(nrxlog))
        rewind(26)
        endfile(26)
        backspace(26)
     else if (nhsym.eq.nhsym1) then
        call dbg('m65c: UNIT26 truncation SKIPPED (repeat call at nhsym1) at t=' // &
                 rtoa(sec_midn()) // ' nrxlog=' // itoa(nrxlog))
     endif
     if(nhsym.eq.nhsym2) backspace(26)
  endif
  nhsym_prev_m65c = nhsym

  nstandalone=0

      call decode0(nstandalone)

end subroutine m65c
end module m65_mod
