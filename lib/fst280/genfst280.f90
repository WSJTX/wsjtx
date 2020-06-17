subroutine genfst280(msg0,ichk,msgsent,msgbits,i4tone,iwspr)

! Input:
!   - msg0     requested message to be transmitted
!   - ichk     if ichk=1, return only msgsent
!   - msgsent  message as it will be decoded
!   - i4tone   array of audio tone values, {0,1,2,3}
!   - iwspr    0: (280,101)/crc24, 1: (280,74)/crc24
!
! Frame structure:
! s8 d70 s8 d70 s8

! Message duration: TxT = 164*8400/12000 = 114.8 s

   use packjt77
   include 'fst280_params.f90'
   character*37 msg0
   character*37 message                    !Message to be generated
   character*37 msgsent                    !Message as it will be received
   character*77 c77
   character*24 c24
   integer*4 i4tone(NN),itmp(ND)
   integer*1 codeword(2*ND)
   integer*1 msgbits(101),rvec(77)
   integer isyncword(8)
   integer ncrc24
   logical unpk77_success
   data isyncword/0,1,3,2,1,0,2,3/
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
   iwspr=0
   if(i3.eq.0.and.n3.eq.6) then
      iwspr=1
      read(c77,'(50i1)') msgbits(1:50)
      call get_crc24(msgbits,74,ncrc24)
      write(c24,'(b24.24)') ncrc24
      read(c24,'(24i1)') msgbits(51:74)
   else
      read(c77,'(77i1)') msgbits(1:77)
      call get_crc24(msgbits,101,ncrc24)
      write(c24,'(b24.24)') ncrc24
      read(c24,'(24i1)') msgbits(78:101)
   endif

   if(ichk.eq.1) go to 999
   if(unpk77_success) go to 2
1  msgbits=0
   itone=0
   msgsent='*** bad message ***                  '
   go to 999

 entry get_fst280_tones_from_bits(msgbits,i4tone,iwspr)

2  continue

   if(iwspr.eq.0) then
      call encode280_101(msgbits,codeword)
   else
      call encode280_74(msgbits(1:74),codeword)
   endif

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

   i4tone(1:8)=isyncword
   i4tone(9:78)=itmp(1:70)
   i4tone(79:86)=isyncword
   i4tone(87:156)=itmp(71:140)
   i4tone(157:164)=isyncword

999 return

end subroutine genfst280
