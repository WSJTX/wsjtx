subroutine partintft8(ndelay,nutc,render_legacy)

  use ft8_mod1, only : dd8
  use streaming_emit, only: streaming_emit_enabled
  use, intrinsic :: iso_fortran_env, only: error_unit
  real rnd
  logical, optional, intent(in) :: render_legacy
  logical :: emit_line

  emit_line=.true.
  if(present(render_legacy)) emit_line=render_legacy

  if(ndelay.gt.120) ndelay=120
  numsamp=nint(float(ndelay)*1200)
  dd8(numsamp+1:180000)=dd8(1:(180000-numsamp))
  do i=1,numsamp
     call random_number(rnd)
     dd8(i)=10.0*rnd-5.
  enddo
  ! The stream's stdout carries only JSON events.
  if(emit_line .and. streaming_emit_enabled()) then
     write(error_unit,'(a)') 'partial loss of data'
  else if(emit_line) then
     write(*,2) nutc,'partial loss of data','d'
2    format(i6.6,2x,a20,21x,a1)
     call flush(6)
  endif

  return
end subroutine partintft8
