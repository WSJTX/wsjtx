subroutine gen_EchoCall(callsign,itone)

  character*6 callsign
  integer itone(6)
  
  itone=0                                             !Default character is blank
  do i=1,len(trim(callsign))
     m=ichar(callsign(i:i))
     if(m.ge.48 .and. m.le.57) itone(i)=m-47       !0-9
     if(m.ge.65 .and. m.le.90) itone(i)=m-54       !A-Z
     if(m.ge.97 .and. m.le.122) itone(i)=m-86      !a-z
  enddo

  return
end subroutine gen_EchoCall
