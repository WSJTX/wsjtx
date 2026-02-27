subroutine decode65b(s2,flip,mycall,hiscall,hisgrid,mode65,neme,ndepth,  &
     nqd,nkv,nhist,qual,decoded,s3,sy)

  real s2(66,126)
  real s3(64,63),sy(63)
  logical first,ltext
  character decoded*22,deepmsg*22
  character mycall*12,hiscall*12,hisgrid*6
  common/prcom/pr(126),mdat(126),mref(126,2),mdat2(126),mref2(126,2)
  data first/.true./
  save

  if(first) call setup65
  first=.false.

  do j=1,63
     k=mdat(j)                       !Points to data symbol
     if(flip.lt.0.0) k=mdat2(j)
     do i=1,64
        s3(i,j)=s2(i+2,k)
     enddo
     k=mdat2(j)                       !Points to data symbol
     if(flip.lt.0.0) k=mdat(j)
     sy(j)=s2(1,k)
  enddo

  nadd=mode65
  call extract(s3,nadd,ncount,nhist,decoded,ltext)     !Extract the message
! Suppress "birdie messages" and other garbage decodes:
  if(decoded(1:7).eq.'000AAA ') ncount=-1
  if(decoded(1:7).eq.'0L6MWK ') ncount=-1
  if(flip.lt.0.0 .and. ltext) ncount=-1
  nkv=1
  if(ncount.lt.0) then 
     nkv=0
     decoded='                      '
  endif

  qual=0.
  if(ndepth.ge.1 .and. (nqd.eq.1 .or. flip.eq.1.0)) then
     call deep65(s3,mode65,neme,flip,mycall,hiscall,hisgrid,deepmsg,qual)
     if(nqd.ne.1 .and. qual.lt.10.0) qual=0.0
     if(ndepth.lt.2 .and. qual.lt.6.0) qual=0.0
  endif
  if(nkv.eq.0 .and. qual.ge.1.0) decoded=deepmsg

  return
end subroutine decode65b
