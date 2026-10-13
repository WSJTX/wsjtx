subroutine extract4(sym0,ncount,decoded)

  use packjt
  real sym0(207)
  real sym(207)
  character decoded*22
  character*72 c72
  integer*1 symbol(207)
  integer*1 data1(13)                   !Decoded data (8-bit bytes)
  integer   data4a(9)                   !Decoded data (8-bit bytes)
  integer   data4(12)                   !Decoded data (6-bit bytes)
  integer mettab(-128:127,0:1)          !Metric table
  logical first
  data first/.true./
  save first,mettab,ndelta

  ncount=-1
  decoded=' '
  ave0=sum(sym0)/207.0
  sym=sym0-ave0
  sq=dot_product(sym,sym)
  rms0=sqrt(sq/206.0)
  if(rms0<=tiny(rms0)) return
  sym=sym/rms0

  if(first) then
     call getmet4(mettab,ndelta)
     first=.false.
  endif

  amp=30.0
  limit=10000

  do j=1,207
     n=nint(amp*sym(j))
     if(n.lt.-127) n=-127
     if(n.gt.127) n=127
     symbol(j)=n
  enddo

  nbits=72
  ncycles=0
  call interleave4(symbol(2),-1)          !Remove the interleaving
  call fano232(symbol(2),nbits+31,mettab,ndelta,limit,data1,     &
       ncycles,metric,ncount)
  nlim=ncycles/(nbits+31)

!### Make usage here like that in jt9fano...
  if(ncount.ge.0) then
     do i=1,9
        i4=data1(i)
        if(i4.lt.0) i4=i4+256
        data4a(i)=i4
     enddo
     write(c72,1100) (data4a(i),i=1,9)
1100 format(9b8.8)
     read(c72,1102) data4
1102 format(12b6)

     call unpackmsg(data4,decoded)
     if(decoded(1:6).eq.'000AAA') then
!        decoded='***WRONG MODE?***'
        decoded='                      '
        ncount=-1
     endif
  endif

  return
end subroutine extract4
