! Headless stub for jt9a(): the GUI shared-memory worker loop (lib/jt9a.f90)
! is excluded from headless builds so jt9 links zero Qt (the real jt9a
! depends on QSharedMemory). The headless jt9 is driven by `--stream`
! or a WAV file; the GUI-worker entry is unreachable in normal use. If reached
! (bare `jt9` with no --stream and no input file), fail clean with a diagnostic
! instead of attaching shared memory (which the real jt9a aborts on).
subroutine jt9a()
  use, intrinsic :: iso_fortran_env, only: error_unit
  write(error_unit,'(a)') 'jt9: GUI shared-memory worker mode is not available '// &
       'in the headless build. Use `jt9 --stream` or `jt9 <file.wav>`.'
  stop 2
end subroutine jt9a
