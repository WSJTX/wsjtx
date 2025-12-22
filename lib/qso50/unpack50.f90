subroutine unpack50(dat,n1,n2)

  integer*1 dat(11)

  i=dat(1)
  i4=iand(i,255)
  n1=ishft(i4,20)
  i=dat(2)
  i4=iand(i,255)
  n1=n1 + ishft(i4,12)
  i=dat(3)
  i4=iand(i,255)
  n1=n1 + ishft(i4,4)
  i=dat(4)
  i4=iand(i,255)
  n1=n1 + iand(ishft(i4,-4),15)
  n2=ishft(iand(i4,15),18)
  i=dat(5)
  i4=iand(i,255)
  n2=n2 + ishft(i4,10)
  i=dat(6)
  i4=iand(i,255)
  n2=n2 + ishft(i4,2)
  i=dat(7)
  i4=iand(i,255)
  n2=n2 + iand(ishft(i4,-6),3)

  return
end subroutine unpack50

