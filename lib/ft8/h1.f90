real function h1(x)

!  sigma=1.0/sqrt(2.0)
  sigma=1.0
  xlim=sigma/sqrt(6.0)
  ax=abs(x)
  sgnx=1.0
  if(x.lt.0) sgnx=-1.0
  if(ax.le.xlim) then
     h1=x
  else
     z=exp(1.0/6.0 - (ax/sigma)**2)
     h1=sgnx*sqrt(6.0)*sigma*(2.0/3.0 - 0.5*z)
  endif

  return
end function h1
