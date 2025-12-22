program twq

  character*22 msg0,msg
  integer*1 data0(11)

  open(10,file='wqmsg.txt',status='old')
  write(*,1000)
1000 format(4x,'Encoded message',9x,'Decoded as',12x,'itype'/55('-'))
  
  do line=1,9999
     read(10,*,end=999) msg0
     call wqenc(msg0,itype,data0)
     call wqdec(data0,msg,ntype)
     write(*,1100) line,msg0,msg,ntype
1100 format(i2,'.',1x,a22,2x,a22,i3)
  enddo
  
999 end program twq
