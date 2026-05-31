module decode65b_mod
  implicit none
contains

subroutine decode65b(s2,flip,mycall,hiscall,hisgrid,mode65,neme,ndepth,  &
     nqd,nkv,nhist,qual,decoded,s3,sy)
     
  use deep65_mod
  use extract_mod
  use pr_mod
  use setup65_mod

  real,          intent(in)    :: s2(66,126)
  real,          intent(out)   :: s3(64,63), sy(63)
  real,          intent(in)    :: flip
  real,          intent(inout) :: qual
  integer,       intent(in)    :: mode65, neme, ndepth, nqd
  integer,       intent(inout) :: nhist
  integer,       intent(out)   :: nkv
  character(len=12),  intent(in)    :: mycall, hiscall
  character(len=6),   intent(in)    :: hisgrid
  character(len=22),  intent(out) :: decoded

  integer :: nadd,ncount,i,j,k
  logical first,ltext
  character deepmsg*22
  integer :: mrs(63), mrs2(63)
  data deepmsg/'                      '/
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
!  write(*,*) 'DECODE65B: flip=', flip, ' mode65=', mode65, ' ndepth=', ndepth, ' neme=', neme
!  write(*,*) 'DECODE65B: nhist in=', nhist
!  write(*,*) 'DECODE65B: first few s3(:,1) = ', s3(1:10,1)
!  write(*,*) 'DECODE65B: mdat(1:10) = ', mdat(1:10)
!  write(*,*) 'DECODE65B: mdat2(1:10) = ', mdat2(1:10)

  call extract(s3,nadd,ncount,nhist,decoded,ltext,mrs,mrs2)     !Extract the message
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
     call deep65(s3,mode65,neme,flip,mycall,hiscall,hisgrid,deepmsg,qual,mrs,mrs2)
     if(nqd.ne.1 .and. qual.lt.10.0) qual=0.0
     if(ndepth.lt.2 .and. qual.lt.6.0) qual=0.0
  endif
  if(nkv.eq.0 .and. qual.ge.1.0) decoded=deepmsg

  return
end subroutine decode65b

end module decode65b_mod 

