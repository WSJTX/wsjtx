subroutine pack50(n1,n2,dat)

  integer*1 dat(11),i1

  i1=iand(ishft(n1,-20),255)                !8 bits
  dat(1)=i1
  i1=iand(ishft(n1,-12),255)                 !8 bits
  dat(2)=i1
  i1=iand(ishft(n1, -4),255)                 !8 bits
  dat(3)=i1
  i1=16*iand(n1,15)+iand(ishft(n2,-18),15)   !4+4 bits
  dat(4)=i1
  i1=iand(ishft(n2,-10),255)                 !8 bits
  dat(5)=i1
  i1=iand(ishft(n2, -2),255)                 !8 bits
  dat(6)=i1
  i1=64*iand(n2,3)                           !2 bits
  dat(7)=i1
  dat(8)=0
  dat(9)=0
  dat(10)=0
  dat(11)=0

  return
end subroutine pack50

