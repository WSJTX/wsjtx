program test_jtty_pack

  use jtty_mod
  character*80 msg0,msg
  character*32 c32(MAX_FRAMES)
  character*17 cparms
  character*1 err
  integer, parameter :: expected_errors = 1

  open(10,file='jtty_msgs.txt',status='old')
  write(*,1000)
1000 format('i2.n2 i2.n2 NC NF err Message'/87('-'))
  nerr=0
  nz=0
  c32=''
  do imsg=1,99
     read(10,'(a80)',end=100) msg0
     nlength=len_trim(msg0)
     if(nlength.eq.0) then
        write(*,*)
        cycle
     endif
     nz=nz+1
     call pack_jtty(msg0,c32,nframes)
     call unpack_jtty(c32,nframes,msg)
     iz=len(trim(msg))
     do i=1,iz
        if(msg(i:i).eq.'~') msg(i:i)=' '
     enddo
     err=' '
     if(msg.ne.msg0) then
        err='*'
        nerr=nerr+1
        write(12,1010) trim(msg0),trim(msg)
1010    format(a/a)
     endif
     read(c32(1),1012) n2a,i2a
     read(c32(2),1012) n2b,i2b
1012 format(28x,2b2)
     write(cparms,1018) i2a,n2a,i2b,n2b,nlength,nframes
1018 format(i2,'.',i1,2x,i2,'.',i1,i4,i3)
     if(i2a.ge.2) cparms(3:4)='  '
     if(i2b.ge.2) cparms(9:10)='  '
     if(nframes.eq.1) cparms(7:10)='   '
     write(*,1020) cparms,err,trim(msg)
1020 format(a17,2x,a1,2x,a)
  enddo

100  write(*,1100) nz,nerr
1100 format(/'Total messages:',i3,'   Number of errors:',i3)
  if(nerr.ne.expected_errors) error stop 1

end program test_jtty_pack
