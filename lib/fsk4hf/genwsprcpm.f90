subroutine genwsprcpm(msg,msgsent,itone)

! Encode a WSPRCPM message, producing array itone().
!
   use crc
   include 'wsprcpm_params.f90'

   character*22 msg,msgsent
   character*64 cbits
   character*32 sbits
   character c1*1,c4*4
   character*31 cseq
   integer*1,target :: idat(9)
   integer*1 msgbits(68),codeword(ND)
   logical first
   integer icw(ND)
   integer id(NS+ND)
   integer jd(NS+ND)
!   integer ipreamble(16)                      !Freq estimation preamble
   integer isyncword(16)
   integer isync(200)                          !Long sync vector
   integer itone(NN)
   data cseq /'9D9F C48B 797A DD60 58CB 2EBC 6'/
!   data ipreamble/1,1,1,1,0,0,0,0,0,0,0,0,1,1,1,1/
   data isyncword/0,1,3,2,1,0,2,3,2,3,1,0,3,2,0,1/
   data first/.true./
   save first,isync,ipreamble,isyncword

   if(first) then
      k=0
      do i=1,31
         c1=cseq(i:i)
         if(c1.eq.' ') cycle
         read(c1,'(z1)') n
         write(c4,'(b4.4)') n
         do j=1,4
            k=k+1
            isync(k)=0
            if(c4(j:j).eq.'1') isync(k)=1
         enddo
         isync(101:200)=isync(1:100)
      enddo
      first=.false.
   endif

   idat=0
   call wqencode(msg,ntype0,idat)             !Source encoding
   id7=idat(7)
   if(id7.lt.0) id7=id7+256
   id7=id7/64
   write(*,*) 'idat ',idat
   icrc=crc14(c_loc(idat),9)
   write(*,*) 'icrc: ',icrc
   write(*,'(a6,b16.16)') 'icrc: ',icrc
   call wqdecode(idat,msgsent,itype)
   print*,msgsent,itype
   write(cbits,1004) idat(1:6),id7,iand(icrc,z'3FFF')
1004 format(6b8.8,b2.2,b14.14)
   msgbits=0
   read(cbits,1006) msgbits(1:64)
1006 format(64i1)

   write(*,'(50i1,1x,14i1,1x,4i1)') msgbits

   call encode204(msgbits,codeword)      !Encode the test message

! Message structure:
! d100 p16 d100
   itone(1:100)=isync(1:100)+2*codeword(1:100)
   itone(101:116)=isyncword
   itone(117:216)=isync(101:200)+2*codeword(101:200)
   itone=2*itone-3
   

   return
end subroutine genwsprcpm
