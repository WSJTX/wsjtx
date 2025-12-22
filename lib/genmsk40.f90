subroutine genmsk40(msg,msgsent,ichk,itone,itype)

  use hashing
  character*37 msg,msgsent,hashmsg
  character*4 crpt,rpt(0:15)
  logical first
  integer*4 itone(144)
  integer*1 message(16),codeword(32),bitseq(40)
  integer*1 s8r(8)
  data s8r/1,0,1,1,0,0,0,1/   ! Sync word is reversed wrt msk144 sync word.
  data first/.true./
  data rpt/"-03 ","+00 ","+03 ","+06 ","+10 ","+13 ","+16 ", &
           "R-03","R+00","R+03","R+06","R+10","R+13","R+16", &
           "RRR ","73  "/
  save first,rpt

  itype=-1
  msgsent='*** bad message ***'
  itone=0
  i1=index(msg,'>')
  if(i1.lt.9) go to 900
  call fmtmsg(msg,iz)
  crpt=msg(i1+2:i1+5)
  do i=0,15
     if(crpt.eq.rpt(i)) go to 10
  enddo
  go to 900

10 irpt=i                                   !Report index, 0-15
  if(ichk.lt.10000) then
     hashmsg=msg(2:i1-1)
     call hash(hashmsg,37,ihash)          
     ihash=iand(ihash,4095)                 !12-bit hash 
     ig=16*ihash + irpt                     !4-bit report 
  else
     ig=ichk-10000
  endif

  do i=1,16
    message(i)=iand(1,ishft(ig,1-i))
  enddo

  call encode_msk40(message,codeword)
!  write(*,'(a6,i6,2x,a6,i6,2x,a6,i6)') ' msg: ',ig,'rprt: ',irpt,'hash: ',ihash
!  write(*,'(a6,32i1)') '  cw: ',codeword

  bitseq(1:8)=s8r
  bitseq(9:40)=codeword
  bitseq=2*bitseq-1

! Map I and Q  to tones.
  itone=0
  do i=1, 20
    itone(2*i-1)=(bitseq(2*i)*bitseq(2*i-1)+1)/2;
    itone(2*i)=-(bitseq(2*i)*bitseq(mod(2*i,40)+1)-1)/2;
  enddo

! Flip polarity
  itone=-itone+1 

  msgsent=msg
  itype=7

900 return
end subroutine genmsk40

