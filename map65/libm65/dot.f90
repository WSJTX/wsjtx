real*8 function dot(x,y)

  real*8 x(3),y(3)

  dot=0.d0
  do i=1,3
     dot=dot+x(i)*y(i)
  enddo

  return
end function dot
