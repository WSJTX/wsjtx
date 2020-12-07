subroutine genqra64(msg0,ichk,msgsent,itone,itype)

! Encodes a QRA64 message to yield itone(1:84) or a QRA65 msg, itone(1:85)

  use packjt
  character*22 msg0
  character*22 message            !Message to be generated
  character*22 msgsent            !Message as it will be received
  integer itone(85)               !QRA64 uses only 84
  character*3 cok                 !'   ' or 'OOO'
  integer dgen(13)
  integer sent(63)
  integer isync(22)
  integer icos7(0:6)
  data icos7/2,5,6,0,4,1,3/       !Defines a 7x7 Costas array
  data isync/1,9,12,13,15,22,23,26,27,33,35,38,46,50,55,60,62,66,69,74,76,85/
  save

  if(msg0(1:1).eq.'@') then
     read(msg0(2:5),*,end=1,err=1) nfreq
     go to 2
1    nfreq=1000
2    itone(1)=nfreq
     write(msgsent,1000) nfreq
1000 format(i5,' Hz')
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
     call packmsg(message,dgen,itype)    !Pack message into 72 bits
     call unpackmsg(dgen,msgsent)        !Unpack to get message sent
     if(ichk.eq.1) go to 999             !Return if checking only
     call qra64_enc(dgen,sent)           !Encode using QRA64

     if(ichk.eq.65) then
! Experimental QRA65 mode
        j=1
        k=0
        do i=1,85
           if(i.eq.isync(j)) then
              j=j+1                      !Index for next sync symbol
              itone(i)=0                 !Insert a sync symbol
           else
              k=k+1
              itone(i)=sent(k) + 1
           endif
        enddo
     else
! Original QRA64 mode
        itone(1:7)=10*icos7              !Insert 7x7 Costas array in 3 places
        itone(8:39)=sent(1:32)
        itone(40:46)=10*icos7
        itone(47:77)=sent(33:63)
        itone(78:84)=10*icos7
     endif
  endif

999 return
end subroutine genqra64
