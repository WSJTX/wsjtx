module getpfx2_mod
   implicit none
contains

subroutine getpfx2(k0,callsign)

  use pfx_data_mod
  use pfx_mod
  
  integer, intent(in) :: k0
  character(len=12), intent(inout) :: callsign
  integer :: iz,k

  k=k0
  if(k.gt.450) k=k-450
  if(k.ge.1 .and. k.le.NZ) then
     iz=index(pfx(k),' ') - 1
     callsign=pfx(k)(1:iz)//'/'//callsign
  else if(k.ge.401 .and. k.le.400+NZ2) then
     iz=index(callsign,' ') - 1
     callsign=callsign(1:iz)//'/'//sfx(k-400)
  else if(k.eq.449) then
     iz=index(addpfx,' ') - 1
     if(iz.lt.1) iz=8
     callsign=addpfx(1:iz)//'/'//callsign
  endif

  return
end subroutine getpfx2
end module getpfx2_mod

