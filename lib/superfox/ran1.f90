FUNCTION ran1(idum)
  INTEGER idum
  INTEGER, PARAMETER :: IA=16807,IM=2147483647,IQ=127773,IR=2836,NTAB=32
  INTEGER, PARAMETER :: NDIV=1+INT(DBLE(IM-1)/DBLE(NTAB))
  REAL ran1
  REAL, PARAMETER :: AM=1.0/REAL(IM),EPS=1.2e-7,RNMX=1.-EPS
  INTEGER j,k,iv(NTAB),iy
  SAVE iv,iy
  DATA iv /NTAB*0/, iy /0/
  if (idum.le.0.or.iy.eq.0) then
     idum=max(-idum,1)
     do j=NTAB+8,NTAB+1,-1
        k=idum/IQ
        idum=IA*(idum-k*IQ)-IR*k
        if (idum.lt.0) idum=idum+IM
     enddo
     do j=NTAB,1,-1
        k=idum/IQ
        idum=IA*(idum-k*IQ)-IR*k
        if (idum.lt.0) idum=idum+IM
        iv(j)=idum
     enddo
     iy=iv(1)
  endif
  k=idum/IQ
  idum=IA*(idum-k*IQ)-IR*k
  if (idum.lt.0) idum=idum+IM
  j=1+iy/NDIV
  iy=iv(j)
  iv(j)=idum
  ran1=min(AM*iy,RNMX)

  return
END FUNCTION ran1
