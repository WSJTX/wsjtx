subroutine gen65(msg00,ichk,msgsent0,itone,itype) BIND(c)

! Encodes a JT65 message to yieild itone(1:126)
! Temporarily, does not implement EME shorthands

  use packjt
  character*1 msg00(23),msgsent0(23)
  character*22 msg0
  character*22 message          !Message to be generated
  character*22 msgsent          !Message as it will be received
  integer itone(126)
  character*3 cok               !'   ' or 'OOO'
  integer dgen(13)
  integer sent(63)
  integer nprc(126)
  data nprc/1,0,0,1,1,0,0,0,1,1,1,1,1,1,0,1,0,1,0,0,  &
            0,1,0,1,1,0,0,1,0,0,0,1,1,1,0,0,1,1,1,1,  &
            0,1,1,0,1,1,1,1,0,0,0,1,1,0,1,0,1,0,1,1,  &
            0,0,1,1,0,1,0,1,0,1,0,0,1,0,0,0,0,0,0,1,  &
            1,0,0,0,0,0,0,0,1,1,0,1,0,0,1,0,1,1,0,1,  &
            0,1,0,1,0,0,1,1,0,0,1,0,0,1,0,0,0,0,1,1,  &
            1,1,1,1,1,1/
  save

  do i=1,22
     msg0(i:i)=msg00(i)
  enddo

  if(msg0(1:1).eq.'@') then
     read(msg0(2:5),*,end=1,err=1) nfreq
     go to 2
1    nfreq=1000
2    itone(1)=nfreq
  else
     message=msg0
     do i=1,22
        if(ichar(message(i:i)).eq.0) then
           message(i:)='                      '
           exit
        endif
     enddo

     do i=1,22                               !Strip leading blanks
        if(message(1:1).ne.' ') exit
        message=message(i+1:)
     enddo

     call chkmsg(message,cok,nspecial,flip)
     ntest=0
     if(flip.lt.0.0) ntest=1
     if(nspecial.eq.0) then
        call packmsg(message,dgen,itype)    !Pack message into 72 bits
        call unpackmsg(dgen,msgsent)        !Unpack to get message sent
        msgsent(20:22)=cok
        call fmtmsg(msgsent,iz)
        if(ichk.ne.0) go to 900             !Return if checking only

        call rs_encode(dgen,sent)           !Apply Reed-Solomon code
        call interleave63(sent,1)           !Apply interleaving
        call graycode65(sent,63,1)          !Apply Gray code
        nsym=126                            !Symbols per transmission
        k=0
        do j=1,nsym
           if(nprc(j).eq.ntest) then
              k=k+1
              itone(j)=sent(k)+2
           else
              itone(j)=0
           endif
        enddo
     else
        nsym=32
        k=0
        do j=1,nsym
           do n=1,4
              k=k+1
              if(iand(j,1).eq.1) itone(k)=0
              if(iand(j,1).eq.0) itone(k)=10*nspecial
              if(k.eq.126) go to 10
           enddo
        enddo
10      msgsent=message
        itype=7
     endif
  endif

900 do i=1,22
     msgsent0(i)=msgsent(i:i)
  enddo
  msgsent0(23)=char(0)

  return
end subroutine gen65
