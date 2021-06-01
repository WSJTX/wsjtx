subroutine set(a,y,n)
  real y(n)
  do i=1,n
     y(i)=a
  enddo
  return
end subroutine set

subroutine move(x,y,n)
  real x(n),y(n)
  do i=1,n
     y(i)=x(i)
  enddo
  return
end subroutine move

subroutine zero(x,n)
  real x(n)
  do i=1,n
     x(i)=0.0
  enddo
  return
end subroutine zero

subroutine add(a,b,c,n)
  real a(n),b(n),c(n)
  do i=1,n
     c(i)=a(i)+b(i)
  enddo
  return
end subroutine add
