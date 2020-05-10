program test28

  use packjt77
  parameter (NTOKENS=2063592,MAX22=4194304)
  character*13 arg,call_00,call_0,call_1
  character*1 cerr
  logical unpk28_success

  nargs=iargc()
  n28=-1
  if(nargs.eq.1) then
     call getarg(1,arg)
     read(arg,'(i13)',err=2) n28
  endif
  if(n28.ge.0) go to 100
  
2 open(10,file='test28.txt',status='old')

  write(*,1000)
1000 format('Encoded text   Recovered text      n28 Err?   Type'/60('-'))
  
  do iline=1,999999
     if(nargs.eq.0) then
        read(10,'(a13)',end=999) call_0
     else
        call_0=arg
     endif
     if(call_0.eq.'             ') exit
     if(call_0(1:3).eq.'CQ ' .and. call_0(4:4).ne.' ') call_0(3:3)='_'
     call_1='             '
     call_00=call_0
     call pack28(call_00,n28)
     call unpack28(n28,call_1,unpk28_success)
     cerr=' '
     if(call_0.ne.call_1) cerr='*'
     if(call_0(1:3).eq.'CQ_') call_0(3:3)=' '
     if(call_1(1:3).eq.'CQ_') call_1(3:3)=' '
     if(n28.lt.NTOKENS) write(*,1010) call_0,call_1,n28,cerr
1010 format(a13,2x,a13,i10,2x,a1,2x,'Special token')
     if(n28.ge.NTOKENS .and. n28.lt.NTOKENS+MAX22) then
        call_00=call_0
        call save_hash_call(call_00,n10,n12,n22)
        write(*,1012) call_0,call_1,n28,cerr,n22
1012    format(a13,2x,a13,i10,2x,a1,2x,'22-bit hash',i15)
     endif
     if(n28.ge.NTOKENS+MAX22) write(*,1014) call_0,call_1,n28,cerr
1014 format(a13,2x,a13,i10,2x,a1,2x,'Standard callsign')
     if(nargs.gt.0) exit
  enddo
  go to 999
  
100 call unpack28(n28,call_1,unpk28_success)
  cerr=' '
  if(.not.unpk28_success) cerr='*'
  if(call_1(1:3).eq.'CQ_') call_1(3:3)=' '
  if(n28.lt.NTOKENS) write(*,2010) n28,call_1,cerr
2010 format(i10,2x,a13,2x,a1,2x,'Special token')
  if(n28.ge.NTOKENS .and. n28.lt.NTOKENS+MAX22) write(*,2012) n28,call_1,cerr
2012 format(i10,2x,a13,2x,a1,2x,'22-bit hash')
  if(n28.ge.NTOKENS+MAX22) write(*,2014) n28,call_1,cerr
2014 format(i10,2x,a13,2x,a1,2x,'Standard callsign')
  
999 end program test28
