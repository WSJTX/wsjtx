program test_jtty_pack

  use jtty_mod
  character*80 msg0,msg
  character*32 c32(MAX_FRAMES)
  character*1 err

  open(10,file='jtty_msgs.txt',status='old')

  nerr=0
  do imsg=1,99
     read(10,'(a80)',end=100) msg0
     
     call pack_jtty(msg0,c32,nframes)
     write(*,1010) imsg,nframes,trim(msg0)
1010 format(i2,i3,4x,a)

     call unpack_jtty(c32,nframes,msg)
     iz=len(trim(msg))
     do i=1,iz
        if(msg(i:i).eq.'~') msg(i:i)=' '
     enddo
     err=' '
     if(msg.ne.msg0) then
        err='*'
        nerr=nerr+1
     endif
     write(*,1020) err,trim(msg)
1020 format(6x,a1,2x,a)
  enddo

100 nz=imsg
  write(*,1100) nz,nerr
1100 format('NZ:',i3,'   NERR:',i3)

end program test_jtty_pack
