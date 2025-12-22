subroutine smo121(x,nz)

  real x(nz)

  x0=x(1)
  do i=2,nz-1
     x1=x(i)
     x(i)=0.5*x(i) + 0.25*(x0+x(i+1))
     x0=x1
  enddo

  return
end subroutine smo121
