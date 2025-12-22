logical function baddata(id2,nz)

  integer*2 id2(nz)

  nadd=1200
  j=0
  smin=1.e30
  smax=-smin
  iz=49*12000/nadd
  do i=1,iz
     sq=0.
     do n=1,nadd
        j=j+1
        x=id2(j)
        sq=sq + x*x
     enddo
     rms=sqrt(sq/nadd)
     if(i.gt.6) then
        smin=min(smin,rms)
        smax=max(smax,rms)
     endif
  enddo

  sratio=smax/(smin+1.e-30)
  baddata=.false.
  if(sratio.gt.1.e30) baddata=.true.

  return
end function baddata
