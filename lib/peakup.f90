subroutine peakup(ym,y0,yp,dx)

  real*4 b,c,dx,yp,ym,y0

  b=(yp-ym)
  c=(yp+ym-2.0*y0)
  if(c.ne.0.0) then
     dx=-b/(2.0*c)
  else
     dx=0.0
  endif

  return
end subroutine peakup
