module jtty_mod

  ! These variables are accessible from outside via "use pack_jtty"

  parameter (MAX_WORDS=40)              !Max words in message
  parameter (MAX_FRAMES=16)             !Max frames for the encoded message
  logical verbose

contains

subroutine pack_jtty(message,c32,nframes)

! Input:   character*80   message     !JTTY message, as it appears to a user
! Output:  character*32   c32         !32-bit payload
!          integer        nframes     !Frames in this message (max = 16)

  use packjt77
  character*80 message,msg
  character*32 c32(MAX_FRAMES)
  
  character*13 w(MAX_WORDS)      !Individual message words
  integer nw(MAX_WORDS)          !Sizes of message words
  logical bw(MAX_WORDS)          !bw(i) is True if w(i) is a standard callsign
  integer*4 nwords               !Number of words in message
  integer*4 n32
  logical btext,eom
  data nmsg/0/
  save

  nmsg=nmsg+1
! Convert message to upper case; collapse multiple blanks; parse into words.
  call split_jtty(message,w,nw,bw,nwords)

  msg=message
  ntext=80
  k1=80
  btext=.false.
  eom=.false.
  do iframe=1,MAX_FRAMES
     i2=-1
     n2=-1
     nz=0
     c32(iframe)=''
     if(trim(w(1)).eq.'CQ' .and. bw(2) .and. trim(w(3)).eq.'CQ' &
          .and. .not.btext) then
        i2=0
        n2=0
        nz=3
        call pack28(w(2),n28)
        n32=shiftl(n28,4) + 4*n2 + i2
        write(c32(iframe),1002) n32
1002    format(b32.32)
        eom=.true.
     else if(bw(1) .and. nwords.eq.1 .and. .not.btext) then
        i2=0
        n2=1
        nz=1
        call pack28(w(1),n28)
        n32=shiftl(n28,4) + 4*n2 + i2
        write(c32(iframe),1002) n32
        eom=.true.
     else if(trim(w(1)) .eq. 'TU' .and. bw(2) .and. trim(w(3)).eq.'CQ' &
           .and. .not.btext) then
        i2=0
        n2=2
        nz=3
        call pack28(w(2),n28)
        n32=shiftl(n28,4) + 4*n2 + i2
        write(c32(iframe),1002) n32
        eom=.true.
     else if(bw(1) .and. trim(w(2)).eq.'TU' .and. .not.btext) then
        i2=0
        n2=3
        nz=2
        call pack28(w(1),n28)
        n32=shiftl(n28,4) + 4*n2 + i2
        write(c32(iframe),1002) n32
        if(nwords.eq.2) then
           eom=.true.
        else
           k1=nw(1) + nw(2) + 2
           msg=msg(k1:)
           w(1:nwords-2) = w(3:nwords)
           bw(1:nwords-2) = bw(3:nwords)
           nw(1:nwords-2) = nw(3:nwords)
           nwords = nwords - 2
        endif
     else if(bw(1) .and. trim(w(2)).eq.'AGN?' .and. .not.btext) then
        i2=1
        n2=0
        nz=2
        call pack28(w(1),n28)
        n32=shiftl(n28,4) + 4*n2 + i2
        write(c32(iframe),1002) n32
        eom=.true.
     else if(bw(1)  .and. .not.btext) then
        i2=0
        n2=1
        nz=nwords
        call pack28(w(1),n28)
        n32=shiftl(n28,4) + 4*n2 + i2
        write(c32(iframe),1002) n32
        k1=nw(1)+2
        msg=message(k1:)
        if(verbose) print*,'aa',k1,trim(msg)
     else if(trim(w(1)).eq.'TU' .and. trim(w(2)).eq.'NOW' .and. bw(3) &
           .and. .not.btext) then
        i2=1
        n2=1
        nz=3
        call pack28(w(3),n28)
        n32=shiftl(n28,4) + 4*n2 + i2
        write(c32(iframe),1002) n32
        k1=len(trim(w(3))) + 9
        msg=message(k1:)
     else if(msg(1:4).eq.'599 ') then
        i2=2
        btext=.true.
        nz=nwords
        i0=index(msg,'599 ') + 4
        n30=0
        do i=i0,i0+4
           n30=64*n30 + jchar(msg(i:i))
        enddo
        n32=ishft(n30,2) + i2
        write(c32(iframe),1002) n32
        eom=.true.
     else
        i2=3                             !Use plain text, 5 characters per frame
        ntext=len(trim(msg))
        btext=.true.
        nz=nwords
        n30=0
        do i=1,5
           n30=64*n30 + jchar(msg(i:i))
        enddo
        n32=ishft(n30,2) + i2
        write(c32(iframe),1002) n32
        msg=msg(6:)
        ntext=ntext-5
     endif

!### Temporary
     if(verbose) then
        if(iframe.eq.1) then
           print*,'PACK'
           if(i2.ne.3) write(*,5010) nmsg,iframe,i2,n2,trim(message(1:k1-2))
5010       format(i2,i6,i6,'.',i1,3x,a)
           if(i2.eq.3) write(*,5011) nmsg,iframe,i2,trim(msg)
5011       format(i2,i6,i6'.',4x,a)
        else
           if(i2.ne.3) write(*,5012) iframe,i2,n2,trim(msg)
5012       format(2x,i6,i6,'.',i1,3x,a)
           if(i2.eq.3) write(*,5013) iframe,i2,trim(msg)
5013       format(2x,i6,i6'.',4x,a)     
        endif
     endif
!### 

     nframes=iframe
     if(eom) exit
     if(ntext.le.0) exit
     if(nz.ge.MAX_WORDS) exit
     if(nz.lt.1) exit
     if(nz.le.MAX_WORDS-1 .and. w(nz+1)(1:1).eq.' ' .and. .not.btext &
          .and. nwords.eq.1) exit
     do k=1,MAX_WORDS-nz
        w(k)=w(k+nz)
        nw(k)=nw(k+nz)
        bw(k)=bw(k+nz)
     enddo
  enddo

  return
end subroutine pack_jtty

subroutine unpack_jtty(c32,nframes,message)
  
! Input:   character*32   c32         !32-bit payload
!          integer        nframes     !Frames in this message (max = 16)
! Output:  character*80   message     !JTTY message, as it appears to a user

  use packjt77
  character*80 message
  character*32 c32(MAX_FRAMES)

  character*13 c13
  integer*4 n32
  logical success

  message=''
  k=1
  do iframe=1,nframes
     if(k.gt.len(message)) exit             !Output buffer full; stop decoding
     read(c32(iframe),1002) n28,n2,i2
1002 format(b28.28,b2.2,b2.2)
     call unpack28(n28,c13,success)
     n=0
     if(success) n=len(trim(c13))
     read(c32(iframe),1004) n32
1004 format(b32.32)
     n32=shiftl(n28,4) + 4*n2 + i2
     if(i2.eq.2 .or. i2.eq.3) then
        read(c32(iframe),1006) n30
1006    format(b30.30)
        if(i2.eq.2) then
           message(k:min(k+3,len(message)))='599 '
           k=k+4
        endif
        n30=ishftc(n30,2)
        do i=1,5
           n30=ishftc(n30,6)
           if(k.le.len(message)) then
              message(k:k)=charj(iand(n30,63))
              if(message(k:k).eq.' ') message(k:k)='~'
           endif
           k=k+1
        enddo
     else
        if(success) then
           if(i2.eq.0 .and. n2.eq.0) then
              message(k:min(k+n+5,len(message))) = 'CQ '//trim(c13)//' CQ'
              k=k+n+7
           else if(i2.eq.0 .and. n2.eq.1) then
              message(k:min(k+n-1,len(message))) = trim(c13)
              k=k+n+1
           else if(i2.eq.0 .and. n2.eq.2) then
              message(k:min(k+n+5,len(message))) = 'TU '//trim(c13)//' CQ'
              k=k+n+7
           else if(i2.eq.0 .and. n2.eq.3) then
              message(k:min(k+n+5,len(message))) = trim(c13)//' TU'
              k=k+n+4
           else if(i2.eq.1 .and. n2.eq.0) then
              message(k:min(k+n+5,len(message))) = trim(c13)//' AGN?'
              k=k+n+6
           else if(i2.eq.1 .and. n2.eq.1) then
              message(k:min(k+n+6,len(message))) = 'TU NOW '//trim(c13)
              k=k+n+8
           endif
        endif
     endif

!### Temporary
!     if(iframe.eq.1) print*,'UNPACK'
!     if(i2.lt.3) write(*,5001) iframe,i2,n2,trim(message)
!5001 format(6x,i2,i6,'.',i1,3x,a)
!     if(i2.eq.3) write(*,5002) iframe,i2,trim(message)
!5002 format(6x,i2,i6,5x,a)
!###
     
  enddo

end subroutine unpack_jtty


subroutine split_jtty(message,w,nw,bw,nwords)

! Convert message to upper case; collapse multiple blanks; parse into words.

! Input:  character*80 message
! Output: character*13 w(1:nw)     words (of maximum length 13) in message
!         nw(1:nwords)             length of each parsed word
!         nwords                   number of words in message

  character*80 message
  character*13 w(MAX_WORDS)      !Individual message words
  character*1 c,c0
  character*6 bcall_1
  logical bw(MAX_WORDS)
  logical ok1
  integer nw(MAX_WORDS)
    
  iz=len(trim(message))
  j=0
  k=0
  n=0
  c0=' '
  w='             '
  bw=.false.
  nw=0
  do i=1,iz
     if(ichar(message(i:i)).eq.0) message(i:i)=' '
     c=message(i:i)                             !Single character
     if(c.eq.' ' .and. c0.eq.' ') cycle         !Skip leading/repeated blanks
     if(c.ne.' ' .and. c0.eq.' ') then
        k=k+1                                   !New word
        n=0
     endif
     j=j+1                                      !Index in message
     n=n+1                                      !Index in word
     if(c.ge.'a' .and. c.le.'z') c=char(ichar(c)-32)  !Force upper case
     message(j:j)=c
     if(n.le.13) w(k)(n:n)=c                    !Copy character c into word
     c0=c
  enddo
  iz=j                                          !Message length
  nwords=k                                      !Number of words in message
  if(nwords.le.0) go to 900
  do i=1,nwords
     nw(i)=len(trim(w(i)))
     call chkcall(w(i),bcall_1,ok1)
     bw(i)=ok1 .and. (index(w(i),'/').le.0)
  enddo
  message(iz+1:)='                                     '
  if(nwords.lt.3) go to 900
  call chkcall(w(3),bcall_1,ok1)
  if(ok1 .and. w(1)(1:3).eq.'CQ ') then
     w(1)='CQ_'//w(2)(1:10)             !Make "CQ " into "CQ_"
     w(2:12)=w(3:13)                    !Move all remaining words down by one
     nwords=nwords-1
  endif

900 return
end subroutine split_jtty

character*1 function charj(j)

! Returns the printable character corresponding to JTTY index j (0-63),

  character*64 c
!                   1         2         3         4         5         6
! j       0123456789012345678901234567890123456789012345678901234567890123
  data c/"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ +-./?!@#$%,&*()_'=[]{}<>|:;"/
!                                                    "  
  c(44:44)='"'                                !use " rather than @

  charj=c(j+1:j+1)

  return
end function charj

integer function jchar(c0)

! Returns the JTTY index (0-63) corresponding to character c0.
  
  character*1 c0
  character*64 c
!                   1         2         3         4         5         6
! j       0123456789012345678901234567890123456789012345678901234567890123
  data c/"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ +-./?!@#$%,&*()_'=[]{}<>|:;"/
!                                                    "  
  c(44:44)='"'                                !use " rather than @

  jchar=index(c,c0)-1
  
  return
end function jchar

end module jtty_mod
