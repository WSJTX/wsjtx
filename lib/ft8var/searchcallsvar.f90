subroutine searchcallsvar(callsign1,callsign2,lfound)
  use jt65_mod9, only: callsign_database_available, callsign_in_database
  implicit none
  character(len=12) :: callsign0,callsign1,callsign2
  logical(1) :: lfound
  integer :: nlencall1,nlencall2,ncycles,j

  if(.not.callsign_database_available()) then
    lfound=.true.
    return
  endif

  nlencall1=len_trim(callsign1)
  nlencall2=len_trim(callsign2)
  if(nlencall1.gt.7 .and. callsign2.eq.'') then
    lfound=.true.
    return
  endif
  if(nlencall2.gt.7) callsign2=''
  ncycles=2
  if(callsign2.eq.'') ncycles=1
  do j=1,ncycles
    if(j.eq.1) then
      callsign0=callsign1
    else
      callsign0=callsign2
    endif
    if(callsign0.eq.'TU;') cycle
    if(.not.callsign_in_database(callsign0)) cycle
    lfound=.true.
    return
  enddo
end subroutine searchcallsvar
