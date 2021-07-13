subroutine read_tf2(k)

  parameter (NFFT=32768)
  integer*8 ms                              !ms since epoch
  integer k                                 !data sample pointer
  integer*2 id2(4,174)
  logical lopen
  real*8 fcenter
  common/datcom/dd(4,5760000),ss(4,322,NFFT),savg(4,NFFT),fcenter,nutc,junk(38)
  data lopen/.false./
  save lopen

  if(k.eq.0) then
     inquire(27,opened=lopen)
     if(lopen) then
        rewind 27
     else
        open(27,file='000000_0000.tf2',status='old',access='stream')
     endif
     read(27) fcenter
  endif
  
  read(27,end=999) id2
  do i=1,174
     k=k+1
     dd(1,k)=id2(1,i)
     dd(2,k)=id2(2,i)
     dd(3,k)=id2(3,i)
     dd(4,k)=id2(4,i)
  enddo

999 return
end subroutine read_tf2
