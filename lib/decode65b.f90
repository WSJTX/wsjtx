subroutine decode65b(s2,nflip,nadd,mode65,ntrials,naggressive,ndepth,      &
     mycall,hiscall,hisgrid,nQSOProgress,ljt65apon,nqd,nft,qual,           &
     nhist,decoded)

  use jt65_mod
  real s2(66,126)
  real s3(64,63)
  logical ltext,ljt65apon
  character decoded*22
  character mycall*12,hiscall*12,hisgrid*6
  save

  if(nqd.eq.-99) stop                !Silence compiler warning
  do j=1,63
     k=mdat(j)                       !Points to data symbol
     if(nflip.lt.0) k=mdat2(j)
     do i=1,64
        s3(i,j)=s2(i+2,k)
     enddo
  enddo

  call extract(s3,nadd,mode65,ntrials,naggressive,ndepth,nflip,mycall,   &
      hiscall,hisgrid,nQSOProgress,ljt65apon,ncount,                     &
      nhist,decoded,ltext,nft,qual)

! Suppress "birdie messages" and other garbage decodes:
  if(decoded(1:7).eq.'000AAA ') ncount=-1
  if(decoded(1:7).eq.'0L6MWK ') ncount=-1
  if(nflip.lt.0 .and. ltext) ncount=-1
  if(ncount.lt.0) then 
     nft=0
     decoded='                      '
  endif

  return
end subroutine decode65b
