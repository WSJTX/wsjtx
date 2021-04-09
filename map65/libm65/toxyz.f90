subroutine toxyz(alpha,delta,r,vec)

  implicit real*8 (a-h,o-z)
  real*8 vec(3)

  vec(1)=r*cos(delta)*cos(alpha)
  vec(2)=r*cos(delta)*sin(alpha)
  vec(3)=r*sin(delta)

  return
end subroutine toxyz

subroutine fromxyz(vec,alpha,delta,r)
  
  implicit real*8 (a-h,o-z)
  real*8 vec(3)
  data twopi/6.283185307d0/

  r=sqrt(vec(1)**2 + vec(2)**2 + vec(3)**2)
  alpha=atan2(vec(2),vec(1))
  if(alpha.lt.0.d0) alpha=alpha+twopi
  delta=asin(vec(3)/r)

  return
end subroutine fromxyz
