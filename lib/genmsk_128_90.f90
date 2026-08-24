subroutine genmsk_128_90(msg0,ichk,msgsent,i4tone,itype)
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
!                 1 = standard message  "Call_1 Call_2 Grid/Rpt"
!                 2 = type 1 prefix
!                 3 = type 1 suffix
!                 4 = type 2 prefix
!                 5 = type 2 suffix
!                 6 = free text (up to 13 characters)
!                 7 = short message     "<Call_1 Call2> Rpt"

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
  integer*1 s8(8)
  real*8 pp(12)
  real*8 xi(864),xq(864),pi,twopi
  data s8/0,1,1,1,0,0,1,0/
  equivalence (ihash,i1hash)
  logical first,unpk77_success
  data first/.true./
  save

  if(first) then
    first=.false.
    nsym=128
    pi=4.0*atan(1.0)
    twopi=8.*atan(1.0)
    do i=1,12
      pp(i)=sin((i-1)*pi/12)
    enddo
  endif

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
     c77=' '
     unpk77_success=.false.
     call pack77_legacy_truncating_fallback(message,i3,n3,c77)
     if(i3.lt.0 .or. n3.lt.0) go to 998
     call unpack77(c77,0,msgsent,unpk77_success) !Unpack to get msgsent
     if(ichk.eq.1) go to 999
     read(c77,"(77i1)",err=998) msgbits
     call encode_128_90(msgbits,codeword)

!Create 144-bit channel vector:
!8-bit sync word + 48 bits + 8-bit sync word + 80 bits
     bitseq=0 
     bitseq(1:8)=s8
     bitseq(9:56)=codeword(1:48)
     bitseq(57:64)=s8
     bitseq(65:144)=codeword(49:128)
     bitseq=2*bitseq-1

     xq(1:6)=bitseq(1)*pp(7:12)   !first bit is mapped to 1st half-symbol on q
     do i=1,71
       is=(i-1)*12+7
       xq(is:is+11)=bitseq(2*i+1)*pp
     enddo 
     xq(864-5:864)=bitseq(1)*pp(1:6)   !last half symbol
     do i=1,72                                    
       is=(i-1)*12+1
       xi(is:is+11)=bitseq(2*i)*pp
     enddo
! Map I and Q  to tones. 
    i4tone=0 
    do i=1,72
      i4tone(2*i-1)=(bitseq(2*i)*bitseq(2*i-1)+1)/2;
      i4tone(2*i)=-(bitseq(2*i)*bitseq(mod(2*i,144)+1)-1)/2;
    enddo
  endif

! Flip polarity
  i4tone=-i4tone+1
  go to 999
998 msgbits=0
  bitseq=0
  i4tone=0
  msgsent='*** bad message ***                  '
  itype=-1
999 return
end subroutine genmsk_128_90
