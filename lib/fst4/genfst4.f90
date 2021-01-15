subroutine genfst4(msg0,ichk,msgsent,msgbits,i4tone,iwspr)

! Input:
!   - msg0     requested message to be transmitted
!   - ichk     if ichk=1, return only msgsent
!   - msgsent  message as it will be decoded
!   - i4tone   array of audio tone values, {0,1,2,3}
!   - iwspr    in: 0: FST4 1: FST4W
!              out 0: (240,101)/crc24, 1: (240,74)/crc24
!
! Frame structure:
! s8 d30 s8 d30 s8 d30 s8 d30 s8

   use packjt77
   include 'fst4_params.f90'
   character*37 msg0
   character*37 message                    !Message to be generated
   character*37 msgsent                    !Message as it will be received
   character*77 c77
   character*24 c24
   integer*4 i4tone(NN),itmp(ND)
   integer*1 codeword(2*ND)
   integer*1 msgbits(101),rvec(77)
   integer isyncword1(8),isyncword2(8)
   integer ncrc24
   logical unpk77_success
   data isyncword1/0,1,3,2,1,0,2,3/
   data isyncword2/2,3,1,0,3,2,0,1/
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
   if(iwspr.eq.1) then
      i3=0
      n3=6
   endif
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
      msgbits(1:77)=mod(msgbits(1:77)+rvec,2)
      call get_crc24(msgbits,101,ncrc24)
      write(c24,'(b24.24)') ncrc24
      read(c24,'(24i1)') msgbits(78:101)
   endif

   if(ichk.eq.1) go to 999
   if(unpk77_success) go to 2
   msgbits=0
   itone=0
   msgsent='*** bad message ***                  '
   go to 999

 entry get_fst4_tones_from_bits(msgbits,i4tone,iwspr)

2  continue
   if(iwspr.eq.0) then 
      call encode240_101(msgbits,codeword)
   else
      call encode240_74(msgbits(1:74),codeword)
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

   i4tone(  1:  8)=isyncword1
   i4tone(  9: 38)=itmp(  1: 30)
   i4tone( 39: 46)=isyncword2
   i4tone( 47: 76)=itmp( 31: 60)
   i4tone( 77: 84)=isyncword1
   i4tone( 85:114)=itmp( 61: 90)
   i4tone(115:122)=isyncword2
   i4tone(123:152)=itmp( 91:120)
   i4tone(153:160)=isyncword1

999 return

end subroutine genfst4
