program nonstd_to_c58
  integer*8 n58
  character*11 call_nonstd
  character*38 c
  data c/' 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ/'/
  call_nonstd='PJ4/K1ABC'   !Redifine as needed
  n58=0
  do i=1,11
     n58=n58*38 + index(c,call_nonstd(i:i)) - 1
  enddo
  write(*,1000) call_nonstd,n58
1000 format('Callsign: ',a11,2x,'c58 as decimal integer:',i20)    
end program nonstd_to_c58
