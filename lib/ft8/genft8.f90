subroutine genft8(msg,i3,n3,msgsent,msgbits,itone)

! Encode an FT8 message, producing array itone().
  
  use packjt77
  include 'ft8_params.f90'
  character msg*37,msgsent*37
  character*77 c77
  integer*1 msgbits(77),codeword(174)
  integer itone(79)
  integer icos7(0:6)
  integer graymap(0:7)
  logical unpk77_success
  data icos7/3,1,4,0,6,5,2/                   !Costas 7x7 tone pattern
  data graymap/0,1,3,2,5,6,4,7/

  i3=-1
  n3=-1
  call pack77(msg,i3,n3,c77)
  call unpack77(c77,0,msgsent,unpk77_success)
  read(c77,'(77i1)',err=1) msgbits
  if(unpk77_success) go to 2
1 msgbits=0
  itone=0
  msgsent='*** bad message ***                  '
  go to 900

entry get_ft8_tones_from_77bits(msgbits,itone) 

2  call encode174_91(msgbits,codeword)      !Encode the test message

! Message structure: S7 D29 S7 D29 S7
  itone(1:7)=icos7
  itone(36+1:36+7)=icos7
  itone(NN-6:NN)=icos7
  k=7
  do j=1,ND
     i=3*j -2
     k=k+1
     if(j.eq.30) k=k+7
     indx=codeword(i)*4 + codeword(i+1)*2 + codeword(i+2)
     itone(k)=graymap(indx)
  enddo

900 return
end subroutine genft8
