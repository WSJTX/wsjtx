subroutine genft2(msg0,ichk,msgsent,i4tone,itype)
! s8 + 48bits + s8 + 80 bits = 144 bits (72ms message duration)
!
! Encode an MSK144 message
! Input:
!   - msg0     requested message to be transmitted
!   - ichk     if ichk=1, return only msgsent
!              if ichk.ge.10000, set imsg=ichk-10000 for short msg
!   - msgsent  message as it will be decoded
!   - i4tone   array of audio tone values, 0 or 1
!   - itype    message type 
!                 1 = 77 bit message 
!                 7 = 16 bit message     "<Call_1 Call2> Rpt"

  use iso_c_binding, only: c_loc,c_size_t
  use packjt77
  character*37 msg0
  character*37 message                    !Message to be generated
  character*37 msgsent                    !Message as it will be received
  character*77 c77
  integer*4 i4tone(144)
  integer*1 codeword(128)
  integer*1 msgbits(77) 
  integer*1 bitseq(144)                   !Tone #s, data and sync (values 0-1)
  integer*1 s16(16)
  real*8 xi(864),xq(864),pi,twopi
  data s16/0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0/
  equivalence (ihash,i1hash)
  logical unpk77_success

  nsym=128
  pi=4.0*atan(1.0)
  twopi=8.*atan(1.0)

  message(1:37)=' ' 
  itype=1
  if(msg0(1:1).eq.'@') then                    !Generate a fixed tone
     read(msg0(2:5),*,end=1,err=1) nfreq       !at specified frequency
     go to 2
1    nfreq=1000
2    i4tone(1)=nfreq
  else
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

     if(message(1:1).eq.'<') then
        i2=index(message,'>')
        i1=0
        if(i2.gt.0) i1=index(message(1:i2),' ')
        if(i1.gt.0) then
           call genmsk40(message,msgsent,ichk,i4tone,itype)
           if(itype.lt.0) go to 999
           i4tone(41)=-40
           go to 999
        endif
     endif

     i3=-1
     n3=-1
     call pack77(message,i3,n3,c77)
     call unpack77(c77,0,msgsent,unpk77_success) !Unpack to get msgsent

     if(ichk.eq.1) go to 999
     read(c77,"(77i1)") msgbits
     call encode_128_90(msgbits,codeword)

!Create 144-bit channel vector:
     bitseq=0 
     bitseq(1:16)=s16
     bitseq(17:144)=codeword

     i4tone=bitseq
  endif

999 return
end subroutine genft2
