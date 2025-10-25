
subroutine genft8sdvar(msg,i3,n3,msgsent,msgbits,itone)

! Encode an FT8 message, producing array itone().
  
  use packjt77sdvar
  use ft8_mod1, only : icos7,graymap
  character msg*37,msgsent*37,c77*77
  integer*1 msgbits(77),codeword(174)
  integer itone(79)
  logical unpk77_successvar

  i3=-1
  n3=-1
  call pack77sdvar(msg,i3,n3,c77)
  call unpack77sdvar(c77,msgsent,unpk77_successvar)
  read(c77,'(77i1)',err=1) msgbits
  if(unpk77_successvar) go to 2
1 msgbits=0
  itone=0
  msgsent='*** bad message ***                  '
  go to 900

2  call encode174_91var(msgbits,codeword)      !Encode the test message

! Message structure: S7 D29 S7 D29 S7
  itone(1:7)=icos7; itone(37:43)=icos7; itone(73:79)=icos7
  k=7
  do j=1,58
     i=3*j -2
     k=k+1
     if(j.eq.30) k=k+7
     indx=codeword(i)*4 + codeword(i+1)*2 + codeword(i+2)
     itone(k)=graymap(indx)
  enddo

900 return
end subroutine genft8sdvar
