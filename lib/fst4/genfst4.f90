module fst4_tx
  use packjt77, only: pack77_state,pack77,pack77_for_state,pack77_options,  &
       pack77_legacy_truncating_fallback,                                     &
       pack77_legacy_truncating_fallback_for_state,unpack77,unpack77_for_state
  private
  public :: genfst4_for_state

contains

! genfst4, packing with state's hash tables, or without state packjt77's own.
subroutine genfst4_for_state(msg0,ichk,msgsent,msgbits,i4tone,iwspr,state)

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

   include 'fst4_params.f90'
   type(pack77_state), optional, target, intent(inout) :: state
   character*37 msg0
   character*37 message                    !Message to be generated
   character*37 msgsent                    !Message as it will be received
   character*77 c77
   character*24 c24
   integer*4 i4tone(NN)
   integer*1 msgbits(101),rvec(77)
   integer ncrc24
   logical requested_wspr,encoded_wspr,unpk77_success
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

   requested_wspr=iwspr.eq.1
   i3=-1
   n3=-1
   c77=' '
   if(requested_wspr) then
      if(present(state)) then
         call pack77_for_state(state,message,i3,n3,c77, &
              pack77_options(prefer_wspr_50bit=.true.))
      else
         call pack77(message,i3,n3,c77, &
              pack77_options(prefer_wspr_50bit=.true.))
      endif
   else
      if(present(state)) then
         call pack77_legacy_truncating_fallback_for_state(state,message,i3,n3,c77)
      else
         call pack77_legacy_truncating_fallback(message,i3,n3,c77)
      endif
   endif
   encoded_wspr=i3.eq.0.and.n3.eq.6
   unpk77_success=.false.
   if(i3.ge.0.and.n3.ge.0) then
      if(present(state)) then
         call unpack77_for_state(state,c77,0,msgsent,unpk77_success)
      else
         call unpack77(c77,0,msgsent,unpk77_success) !Unpack to get msgsent
      endif
   endif
   msgbits=0
   iwspr=0
   if(requested_wspr.and..not.encoded_wspr) then
      iwspr=1
      go to 1
   endif
   if(encoded_wspr) then
      iwspr=1
      read(c77,'(50i1)') msgbits(1:50)
      call get_crc24(msgbits,74,ncrc24)
      write(c24,'(b24.24)') ncrc24
      read(c24,'(24i1)') msgbits(51:74)
   else
      read(c77,'(77i1)',err=1) msgbits(1:77)
      msgbits(1:77)=mod(msgbits(1:77)+rvec,2)
      call get_crc24(msgbits,101,ncrc24)
      write(c24,'(b24.24)') ncrc24
      read(c24,'(24i1)') msgbits(78:101)
   endif

   if(ichk.eq.1) go to 999
   if(unpk77_success) then
      call get_fst4_tones_from_bits(msgbits,i4tone,iwspr)
      go to 999
   endif
1  msgbits=0
   i4tone=0
   if(requested_wspr) iwspr=1
   msgsent='*** bad message ***                  '

999 return

end subroutine genfst4_for_state

end module fst4_tx

subroutine genfst4(msg0,ichk,msgsent,msgbits,i4tone,iwspr)
   use fst4_tx, only: genfst4_for_state
   character*37 msg0,msgsent
   integer*1 msgbits(101)
   integer*4 i4tone(160)
   call genfst4_for_state(msg0,ichk,msgsent,msgbits,i4tone,iwspr)
end subroutine genfst4

subroutine get_fst4_tones_from_bits(msgbits,i4tone,iwspr)
   include 'fst4_params.f90'
   integer*4 i4tone(NN),itmp(ND)
   integer*1 codeword(2*ND)
   integer*1 msgbits(101)
   integer isyncword1(8),isyncword2(8)
   data isyncword1/0,1,3,2,1,0,2,3/
   data isyncword2/2,3,1,0,3,2,0,1/

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

end subroutine get_fst4_tones_from_bits
