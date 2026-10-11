subroutine stdcall_suffix(callsign,std)

! Like stdcall, but a trailing /R or /P does not make a standard callsign
! nonstandard: such a call is still sent as a Type 1 message with the
! ipa/ipb flag set, so candidate messages must use the plain form.

  character*12 callsign,c12
  logical std

  c12=callsign
  n=len_trim(c12)
  if(n.gt.3) then
     if(c12(n-1:n).eq.'/R' .or. c12(n-1:n).eq.'/P') c12(n-1:n)='  '
  endif
  call stdcall(c12,std)

  return
end subroutine stdcall_suffix
