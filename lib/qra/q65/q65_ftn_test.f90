program q65_ftn_test

  use packjt77
  parameter (LL=192,NN=63)
  integer x(13)              !User's 78-bit message as 13 six-bit integers
  integer y(63)              !Q65 codeword for x
  integer xdec(13)            !Decoded message
  integer APmask(13)
  integer APsymbols(13)
  real s3(0:LL-1,NN)
  real s3prob(0:LL-1,NN)
  character*37 msg0,msg,msgsent
  character*77 c77
  logical unpk77_success

  narg=iargc()
  if(narg.ne.1) then
     print*,'Usage:   q65_ftn_test "message"'
     print*,'Example: q65_ftn_test "K1ABC W9XYZ EN37"'
     go to 999
  endif
  call getarg(1,msg0)
  call pack77(msg0,i3,n3,c77)
  call unpack77(c77,0,msgsent,unpk77_success) !Unpack to get msgsent
  read(c77,1000) x
1000 format(12b6.6,b5.5)

  call q65_enc(x,y)                            !Encode message, x(1:13) ==> y(1:63)
  
  write(*,1010) x,msg0
1010 format('User message:'/13i3,2x,a)
  write(*,1020) y
1020 format(/'Generated codeword:'/(20i3))

  s3=0.
  s3prob=0.
  do j=1,NN
     s3(y(j)+64,j)=1.0
  enddo
  APmask=0
  APsymbols=0
  nsubmode=0
  b90=1.0
  nFadingModel=1
  call q65_dec(s3,APmask,APsymbols,nsubmode,b90,nFadingModel,s3prob,snr2500,xdec,irc)
  write(c77,1000) xdec
  call unpack77(c77,0,msg,unpk77_success) !Unpack to get msgsent
  write(*,1100) xdec,trim(msg)
1100 format(/'Decoded message:'/13i3,2x,a)

999 end program q65_ftn_test
