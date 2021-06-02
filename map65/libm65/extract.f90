subroutine extract(s3,nadd,ncount,nhist,decoded,ltext)

  use packjt
  use timer_module, only: timer
  real s3(64,63)
  character decoded*22
  integer dat4(12)
  integer mrsym(63),mr2sym(63),mrprob(63),mr2prob(63)
  logical first,ltext
  integer correct(63),itmp(63)
  integer param(0:8)
  integer h0(0:11),d0(0:11)
  real r0(0:11)
  common/test001/s3a(64,63),mrs(63),mrs2(63)        !### TEST ONLY ###

!          0  1  2  3  4  5  6  7  8  9 10 11
  data h0/41,42,43,43,44,45,46,47,48,48,49,49/
  data d0/71,72,73,74,76,77,78,80,81,82,83,83/
!            0    1    2    3    4    5    6    7    8    9   10   11
  data r0/0.70,0.72,0.74,0.76,0.78,0.80,0.82,0.84,0.86,0.88,0.90,0.90/

  data first/.true./,nsec1/0/
  save

  nfail=0
  call pctile(s3,4032,50,base)     ! ### or, use ave from demod64a
  s3=s3/base
  s3a=s3
1 call demod64a(s3,nadd,mrsym,mrprob,mr2sym,mr2prob,ntest,nlow)
!  if(ntest.lt.50 .or. nlow.gt.20) then
!     ncount=-999                         !Flag bad data
!     go to 900
!  endif
  call chkhist(mrsym,nhist,ipk)

  if(nhist.ge.20) then
     nfail=nfail+1
     call pctile(s3,4032,50,base)     ! ### or, use ave from demod64a
     s3(ipk,1:63)=base
     if(nfail.gt.30) then
        decoded='                      '
        ncount=-1
        go to 900
     endif
     go to 1
  endif

  mrs=mrsym
  mrs2=mr2sym

  call graycode(mrsym,63,-1)
  call interleave63(mrsym,-1)
  call interleave63(mrprob,-1)

  call graycode(mr2sym,63,-1)
  call interleave63(mr2sym,-1)
  call interleave63(mr2prob,-1)

  ntrials=10000
  naggressive=10

  ntry=0
  param=0

  call timer('ftrsd   ',0)
  call ftrsd2(mrsym,mrprob,mr2sym,mr2prob,ntrials,correct,param,ntry)
  call timer('ftrsd   ',1)
  ncandidates=param(0)
  nhard=param(1)
  nsoft=param(2)
  nerased=param(3)
  rtt=0.001*param(4)
  ntotal=param(5)
  qual=0.001*param(7)
  nd0=81
  r00=0.87
  if(naggressive.eq.10) then
     nd0=83
     r00=0.90
  endif
  if(ntotal.le.nd0 .and. rtt.le.r00) nft=1
  n=naggressive
  if(nhard.gt.50) nft=0
  if(nhard.gt.h0(n)) nft=0
  if(ntotal.gt.d0(n)) nft=0
  if(rtt.gt.r0(n)) nft=0

  ncount=-1
  decoded='                      '
  ltext=.false.
  if(nft.gt.0) then
! Turn the corrected symbol array into channel symbols for subtraction;
! pass it back to jt65a via common block "chansyms65".
     do i=1,12
        dat4(i)=correct(13-i)
     enddo
     do i=1,63
       itmp(i)=correct(64-i)
     enddo
     correct(1:63)=itmp(1:63)
     call interleave63(correct,1)
     call graycode65(correct,63,1)
     call unpackmsg(dat4,decoded)     !Unpack the user message
     ncount=0
     if(iand(dat4(10),8).ne.0) ltext=.true.
  endif
900 continue
  if(nft.eq.1 .and. nhard.lt.0) decoded='                      '
!  write(81,3001) naggressive,ncandidates,nhard,ntotal,rtt,qual,decoded
!3001 format(i2,i6,i3,i4,2f8.2,2x,a22)

  return
end subroutine extract

subroutine getpp(workdat,p)

  integer workdat(63)
  integer a(63)
  common/test001/s3a(64,63),mrs(63),mrs2(63)

  a(1:63)=workdat(63:1:-1)
  call interleave63(a,1)
  call graycode(a,63,1)

  psum=0.
  do j=1,63
     i=a(j)+1
     x=s3a(i,j)
     s3a(i,j)=0.
     psum=psum + x
     s3a(i,j)=x
  enddo
  p=psum/63.0

  return
end subroutine getpp
