program q65code
   use q65_encoding

   implicit none
   character*37 msg37,msgsent
   integer nargs
   integer codeword(65),tones(85)
   integer iargc
   logical success

   nargs=iargc()
   if(nargs .ne. 1) then
      print*,'Usage: q65code "msg"'
      goto 999
   endif
   call getarg(1,msg37)

   call get_q65_tones(msg37,codeword,tones,msgsent,success)
   if(.not.success) then
      print*,'Cannot encode message: ',trim(msg37)
      stop 1
   endif

   write(*,*) 'Message sent: ',trim(msgsent)
   write(*,*) 'Generated message plus CRC (90 bits)'
   write(*,'(a8,15i4)') '6 bit : ',codeword(1:15)
   write(*,'(a8,15b6.6)') 'binary: ',codeword(1:15)
   write(*,*) ' '
   write(*,*) 'Codeword with CRC symbols (65 symbols)'
   write(*,'(20i3)') codeword

   write(*,*) ' '
   write(*,*) 'Channel symbols (85 total)'
   write(*,'(20i3)') tones

999 end program q65code
