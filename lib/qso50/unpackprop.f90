subroutine unpackprop(n1,k,muf,ccur,cxp)

  character ccur*4,cxp*2

  muf=mod(n1,62)
  n1=n1/62

  k=mod(n1,11)
  n1=n1/11

  j=mod(n1,53)
  n1=n1/53
  if(j.eq.0) cxp='*'
  if(j.ge.1 .and. j.le.26) cxp=char(64+j)
  if(j.gt.26) cxp=char(64+j-26)//char(64+j-26)

  j=mod(n1,53)
  n1=n1/53
  if(j.eq.0) ccur(2:2)='*'
  if(j.ge.1 .and. j.le.26) ccur(2:2)=char(64+j)
  if(j.gt.26) ccur(2:3)=char(64+j-26)//char(64+j-26)
  j=n1
  if(j.eq.0) ccur(1:1)='*'
  if(j.ge.1 .and. j.le.26) ccur(1:1)=char(64+j)
  if(j.gt.26) ccur=char(64+j-26)//char(64+j-26)//ccur(2:3)

  return
end subroutine unpackprop
