subroutine move(x,y,n)
  integer x(n),y(n)
  do i=1,n
     y(i)=x(i)
  enddo
  return
end subroutine move
