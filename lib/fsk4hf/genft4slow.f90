subroutine genft4slow(msg0,ichk,msgsent,msgbits,i4tone)

! Encode an FT4  message
! Input:
!   - msg0     requested message to be transmitted
!   - ichk     if ichk=1, return only msgsent
!   - msgsent  message as it will be decoded
!   - i4tone   array of audio tone values, {0,1,2,3} 

! Frame structure:
! s4 d24 s4 d24 s4 d24 s4 d24 s4 d24 s4 

! Message duration: TxT = 144*9600/12000 = 115.2 s
  
  use packjt77
  include 'ft4s_params.f90'  
  character*37 msg0
  character*37 message                    !Message to be generated
  character*37 msgsent                    !Message as it will be received
  character*77 c77
  character*24 c24
  integer*4 i4tone(NN),itmp(ND)
  integer*1 codeword(2*ND)
  integer*1 msgbits(101),rvec(77) 
  integer icos4a(4),icos4b(4),icos4c(4),icos4d(4),icos4e(4),icos4f(4)
  integer ncrc24
  logical unpk77_success
  data icos4a/0,1,3,2/
  data icos4b/1,0,2,3/
  data icos4c/2,3,1,0/
  data icos4d/3,2,0,1/
  data icos4e/0,2,3,1/
  data icos4f/1,2,0,3/
  data rvec/0,1,0,0,1,0,1,0,0,1,0,1,1,1,1,0,1,0,0,0,1,0,0,1,1,0,1,1,0, &
            1,0,0,1,0,1,1,0,0,0,0,1,0,0,0,1,0,1,0,0,1,1,1,1,0,0,1,0,1, &
            0,1,0,1,0,1,1,0,1,1,1,1,1,0,0,0,1,0,1/
  message=msg0

  do i=1, 37
     if(ichar(message(i:i)).eq.0) then
        message(i:37)=' '
        exit
     endif
  enddo
  do i=1,37                               !Strip leading blanks
     if(message(1:1).ne.' ') exit
     message=message(i+1:)
  enddo

  i3=-1
  n3=-1
  call pack77(message,i3,n3,c77)
  call unpack77(c77,0,msgsent,unpk77_success) !Unpack to get msgsent
  msgbits=0
  read(c77,'(77i1)') msgbits(1:77)
  call get_crc24(msgbits,101,ncrc24)
  write(c24,'(b24.24)') ncrc24
  read(c24,'(24i1)') msgbits(78:101)

  if(ichk.eq.1) go to 999
  if(unpk77_success) go to 2
1 msgbits=0
  itone=0
  msgsent='*** bad message ***                  '
  go to 999

entry get_ft4slow_tones_from_101bits(msgbits,i4tone)

2  call encode240_101(msgbits,codeword)

! Grayscale mapping:
! bits   tone
! 00     0
! 01     1
! 11     2
! 10     3

  do i=1,ND
    is=codeword(2*i)+2*codeword(2*i-1)
    if(is.le.1) itmp(i)=is
    if(is.eq.2) itmp(i)=3
    if(is.eq.3) itmp(i)=2
  enddo

  i4tone(1:4)=icos4a
  i4tone(5:28)=itmp(1:24)
  i4tone(29:32)=icos4b
  i4tone(33:56)=itmp(25:48)
  i4tone(57:60)=icos4c
  i4tone(61:84)=itmp(49:72)
  i4tone(85:88)=icos4d
  i4tone(89:112)=itmp(73:96)
  i4tone(113:116)=icos4e
  i4tone(117:140)=itmp(97:120)
  i4tone(141:144)=icos4f

999 return
end subroutine genft4slow
