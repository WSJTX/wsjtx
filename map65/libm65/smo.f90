subroutine smo(x,npts,y,nadd)

  real x(npts)
  real y(npts)

  nh=nadd/2
  do i=1+nh,npts-nh
     sum=0.
     do j=-nh,nh
        sum=sum + x(i+j)
     enddo
     y(i)=sum
  enddo
  x=y
  x(:nh)=0.
  x(npts-nh+1:)=0.

  return
end subroutine smo
