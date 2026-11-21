!-------------------------------------------------------------------------------
!
! This file is part of the WSPR application, Weak Signal Propagation Reporter
!
! File Name:    wspr_old_subs.f90
! Description:  Utility subroutines from WSPR 2.0
!
! Copyright (C) 2001-2014 Joseph Taylor, K1JT
! License: GPL-3
!
! This program is free software; you can redistribute it and/or modify it under
! the terms of the GNU General Public License as published by the Free Software
! Foundation; either version 3 of the License, or (at your option) any later
! version.
!
! This program is distributed in the hope that it will be useful, but WITHOUT
! ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
! FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
! details.
!
! You should have received a copy of the GNU General Public License along with
! this program; if not, write to the Free Software Foundation, Inc., 51 Franklin
! Street, Fifth Floor, Boston, MA 02110-1301, USA.
!
!-------------------------------------------------------------------------------

subroutine encode232(dat,nbytes,symbol,maxsym)

! Convolutional encoder for a K=32, r=1/2 code.

  integer*1 dat(nbytes)             !User data, packed 8 bits per byte
  integer*1 symbol(maxsym)          !Channel symbols, one bit per byte
  integer*1 i1

! Layland-Lushbaugh polynomials for a K=32, r=1/2 convolutional code,
! and 8-bit parity lookup table.

  data npoly1/-221228207/,npoly2/-463389625/
  integer*1 partab(0:255)
  data partab/                 &
       0, 1, 1, 0, 1, 0, 0, 1, &
       1, 0, 0, 1, 0, 1, 1, 0, &
       1, 0, 0, 1, 0, 1, 1, 0, &
       0, 1, 1, 0, 1, 0, 0, 1, &
       1, 0, 0, 1, 0, 1, 1, 0, &
       0, 1, 1, 0, 1, 0, 0, 1, &
       0, 1, 1, 0, 1, 0, 0, 1, &
       1, 0, 0, 1, 0, 1, 1, 0, &
       1, 0, 0, 1, 0, 1, 1, 0, &
       0, 1, 1, 0, 1, 0, 0, 1, &
       0, 1, 1, 0, 1, 0, 0, 1, &
       1, 0, 0, 1, 0, 1, 1, 0, &
       0, 1, 1, 0, 1, 0, 0, 1, &
       1, 0, 0, 1, 0, 1, 1, 0, &
       1, 0, 0, 1, 0, 1, 1, 0, &
       0, 1, 1, 0, 1, 0, 0, 1, &
       1, 0, 0, 1, 0, 1, 1, 0, &
       0, 1, 1, 0, 1, 0, 0, 1, &
       0, 1, 1, 0, 1, 0, 0, 1, &
       1, 0, 0, 1, 0, 1, 1, 0, &
       0, 1, 1, 0, 1, 0, 0, 1, &
       1, 0, 0, 1, 0, 1, 1, 0, &
       1, 0, 0, 1, 0, 1, 1, 0, &
       0, 1, 1, 0, 1, 0, 0, 1, &
       0, 1, 1, 0, 1, 0, 0, 1, &
       1, 0, 0, 1, 0, 1, 1, 0, &
       1, 0, 0, 1, 0, 1, 1, 0, &
       0, 1, 1, 0, 1, 0, 0, 1, &
       1, 0, 0, 1, 0, 1, 1, 0, &
       0, 1, 1, 0, 1, 0, 0, 1, &
       0, 1, 1, 0, 1, 0, 0, 1, &
       1, 0, 0, 1, 0, 1, 1, 0/

  nstate=0
  k=0
  do j=1,nbytes
     do i=7,0,-1
        i1=dat(j)
        i4=i1
        if (i4.lt.0) i4=i4+256
        nstate=ior(ishft(nstate,1),iand(ishft(i4,-i),1))
        n=iand(nstate,npoly1)
        n=ieor(n,ishft(n,-16))
        k=k+1
        symbol(k)=partab(iand(ieor(n,ishft(n,-8)),255))
        n=iand(nstate,npoly2)
        n=ieor(n,ishft(n,-16))
        k=k+1
        symbol(k)=partab(iand(ieor(n,ishft(n,-8)),255))
     enddo
  enddo

  return
end subroutine encode232

subroutine fano232(symbol,nbits,mettab,ndelta,maxcycles,dat,ncycles,metric,ierr)

! Sequential decoder for K=32, r=1/2 convolutional code using 
! the Fano algorithm.  Translated from C routine for same purpose
! written by Phil Karn, KA9Q.

  parameter (MAXBITS=103)
  parameter (MAXDAT=13)               !(MAXBITS+7)/8
  integer*1 symbol(0:2*MAXBITS-1)
  integer*1 dat(MAXDAT)               !Decoded user data, 8 bits per byte
  integer mettab(0:255,0:1)           !Metric table

! These were the "node" structure in Karn's C code:
  integer nstate(0:MAXBITS-1)      !Encoder state of next node
  integer gamma(0:MAXBITS-1)       !Cumulative metric to this node
  integer metrics(0:3,0:MAXBITS-1) !Metrics indexed by all possible Tx syms
  integer tm(0:1,0:MAXBITS-1)      !Sorted metrics for current hypotheses
  integer ii(0:MAXBITS-1)          !Current branch being tested

  logical noback

! Layland-Lushbaugh polynomials for a K=32, r=1/2 convolutional code,
! and 8-bit parity lookup table.

  data npoly1/-221228207/,npoly2/-463389625/
  integer*1 partab(0:255)
  data partab/                 &
       0, 1, 1, 0, 1, 0, 0, 1, &
       1, 0, 0, 1, 0, 1, 1, 0, &
       1, 0, 0, 1, 0, 1, 1, 0, &
       0, 1, 1, 0, 1, 0, 0, 1, &
       1, 0, 0, 1, 0, 1, 1, 0, &
       0, 1, 1, 0, 1, 0, 0, 1, &
       0, 1, 1, 0, 1, 0, 0, 1, &
       1, 0, 0, 1, 0, 1, 1, 0, &
       1, 0, 0, 1, 0, 1, 1, 0, &
       0, 1, 1, 0, 1, 0, 0, 1, &
       0, 1, 1, 0, 1, 0, 0, 1, &
       1, 0, 0, 1, 0, 1, 1, 0, &
       0, 1, 1, 0, 1, 0, 0, 1, &
       1, 0, 0, 1, 0, 1, 1, 0, &
       1, 0, 0, 1, 0, 1, 1, 0, &
       0, 1, 1, 0, 1, 0, 0, 1, &
       1, 0, 0, 1, 0, 1, 1, 0, &
       0, 1, 1, 0, 1, 0, 0, 1, &
       0, 1, 1, 0, 1, 0, 0, 1, &
       1, 0, 0, 1, 0, 1, 1, 0, &
       0, 1, 1, 0, 1, 0, 0, 1, &
       1, 0, 0, 1, 0, 1, 1, 0, &
       1, 0, 0, 1, 0, 1, 1, 0, &
       0, 1, 1, 0, 1, 0, 0, 1, &
       0, 1, 1, 0, 1, 0, 0, 1, &
       1, 0, 0, 1, 0, 1, 1, 0, &
       1, 0, 0, 1, 0, 1, 1, 0, &
       0, 1, 1, 0, 1, 0, 0, 1, &
       1, 0, 0, 1, 0, 1, 1, 0, &
       0, 1, 1, 0, 1, 0, 0, 1, &
       0, 1, 1, 0, 1, 0, 0, 1, &
       1, 0, 0, 1, 0, 1, 1, 0/

  ntail=nbits-31

! Compute all possible branch metrics for each symbol pair.
! This is the only place we actually look at the raw input symbols
  i4a=0
  i4b=0
  do np=0,nbits-1
     j=2*np
     i4a=symbol(j)
     i4b=symbol(j+1)
     if (i4a.lt.0) i4a=i4a+256
     if (i4b.lt.0) i4b=i4b+256
     metrics(0,np) = mettab(i4a,0) + mettab(i4b,0)
     metrics(1,np) = mettab(i4a,0) + mettab(i4b,1)
     metrics(2,np) = mettab(i4a,1) + mettab(i4b,0)
     metrics(3,np) = mettab(i4a,1) + mettab(i4b,1)
  enddo

  np=0
  nstate(np)=0

! Compute and sort branch metrics from the root node
  n=iand(nstate(np),npoly1)
  n=ieor(n,ishft(n,-16))
  lsym=partab(iand(ieor(n,ishft(n,-8)),255))
  n=iand(nstate(np),npoly2)
  n=ieor(n,ishft(n,-16))
  lsym=lsym+lsym+partab(iand(ieor(n,ishft(n,-8)),255))
  m0=metrics(lsym,np)
  m1=metrics(ieor(3,lsym),np)
  if(m0.gt.m1) then
     tm(0,np)=m0                      !0-branch has better metric
     tm(1,np)=m1
  else
     tm(0,np)=m1                      !1-branch is better
     tm(1,np)=m0
     nstate(np)=nstate(np) + 1        !Set low bit
  endif

! Start with best branch
  ii(np)=0
  gamma(np)=0
  nt=0

! Start the Fano decoder
  do i=1,nbits*maxcycles
! Look forward
     ngamma=gamma(np) + tm(ii(np),np)
     if(ngamma.ge.nt) then

! Node is acceptable.  If first time visiting this node, tighten threshold:
        if(gamma(np).lt.(nt+ndelta)) nt=nt +                     &
             ndelta * ((ngamma-nt)/ndelta)

! Move forward
        gamma(np+1)=ngamma
        nstate(np+1)=ishft(nstate(np),1)
        np=np+1
        if(np.eq.nbits-1) go to 100     !We're done!

        n=iand(nstate(np),npoly1)
        n=ieor(n,ishft(n,-16))
        lsym=partab(iand(ieor(n,ishft(n,-8)),255))
        n=iand(nstate(np),npoly2)
        n=ieor(n,ishft(n,-16))
        lsym=lsym+lsym+partab(iand(ieor(n,ishft(n,-8)),255))
            
        if(np.ge.ntail) then
           tm(0,np)=metrics(lsym,np)      !We're in the tail, all zeros
        else
           m0=metrics(lsym,np)
           m1=metrics(ieor(3,lsym),np)
           if(m0.gt.m1) then
              tm(0,np)=m0                 !0-branch has better metric
              tm(1,np)=m1
           else
              tm(0,np)=m1                 !1-branch is better
              tm(1,np)=m0
              nstate(np)=nstate(np) + 1   !Set low bit
           endif
        endif

        ii(np)=0                          !Start with best branch
        go to 99
     endif

! Threshold violated, can't go forward
10   noback=.false.
     if(np.eq.0) noback=.true.
     if(np.gt.0) then
        if(gamma(np-1).lt.nt) noback=.true.
     endif

     if(noback) then
! Can't back up, either.  Relax threshold and look forward again 
! to a better branch.
        nt=nt-ndelta
        if(ii(np).ne.0) then
           ii(np)=0
           nstate(np)=ieor(nstate(np),1)
        endif
        go to 99
     endif

! Back up
     np=np-1
     if(np.lt.ntail .and. ii(np).ne.1) then
! Search the next best branch
        ii(np)=ii(np)+1
        nstate(np)=ieor(nstate(np),1)
        go to 99
     endif
     go to 10
99   continue
  enddo
  i=nbits*maxcycles

100 metric=gamma(np)                       !Final path metric

! Copy decoded data to user's buffer
  nbytes=(nbits+7)/8
  np=7
  do j=1,nbytes-1
     i4a=nstate(np)
     dat(j)=i4a
     np=np+8
  enddo
  dat(nbytes)=0

  ncycles=i+1
  ierr=0
  if(i.ge.maxcycles*nbits) ierr=-1

  return
end subroutine fano232

subroutine inter_mept(id,ndir)

! Interleave (ndir=1) or de-interleave (ndir=-1) the array id.

  integer*1 id(0:161),itmp(0:161)
  integer j0(0:161)
  logical first
  data first/.true./
  save

  if(first) then
! Compute the interleave table using bit reversal.
     k=-1
     do i=0,255
        n=0
        ii=i
        do j=0,7
           n=n+n
           if(iand(ii,1).ne.0) n=n+1
           ii=ii/2
        enddo
        if(n.le.161) then
           k=k+1
           j0(k)=n
        endif
     enddo
     first=.false.
  endif

  if(ndir.eq.1) then
     do i=0,161
        itmp(j0(i))=id(i)
     enddo
  else
     do i=0,161
        itmp(i)=id(j0(i))
     enddo
  endif

  do i=0,161
     id(i)=itmp(i)
  enddo

  return
end subroutine inter_mept

subroutine unpack50(dat,n1,n2)

  integer*1 dat(11)

  i=dat(1)
  i4=iand(i,255)
  n1=ishft(i4,20)
  i=dat(2)
  i4=iand(i,255)
  n1=n1 + ishft(i4,12)
  i=dat(3)
  i4=iand(i,255)
  n1=n1 + ishft(i4,4)
  i=dat(4)
  i4=iand(i,255)
  n1=n1 + iand(ishft(i4,-4),15)
  n2=ishft(iand(i4,15),18)
  i=dat(5)
  i4=iand(i,255)
  n2=n2 + ishft(i4,10)
  i=dat(6)
  i4=iand(i,255)
  n2=n2 + ishft(i4,2)
  i=dat(7)
  i4=iand(i,255)
  n2=n2 + iand(ishft(i4,-6),3)

  return
end subroutine unpack50

subroutine unpackcall(ncall,word)

  character word*12,c*37

  data c/'0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ '/

  n=ncall
  word='......'
  if(n.ge.262177560) go to 999            !Plain text message ...
  i=mod(n,27)+11
  word(6:6)=c(i:i)
  n=n/27
  i=mod(n,27)+11
  word(5:5)=c(i:i)
  n=n/27
  i=mod(n,27)+11
  word(4:4)=c(i:i)
  n=n/27
  i=mod(n,10)+1
  word(3:3)=c(i:i)
  n=n/10
  i=mod(n,36)+1
  word(2:2)=c(i:i)
  n=n/36
  i=n+1
  word(1:1)=c(i:i)
  do i=1,4
     if(word(i:i).ne.' ') go to 10
  enddo
  go to 999
10 word=word(i:)

999 if(word(1:3).eq.'3D0') word='3DA0'//word(4:)
  return
end subroutine unpackcall

subroutine unpackgrid(ng,grid)

  parameter (NGBASE=180*180)
  character grid*4,grid6*6,digit*10
  data digit/'0123456789'/

  grid='    '
  if(ng.ge.32400) go to 10
  dlat=mod(ng,180)-90
  dlong=(ng/180)*2 - 180 + 2
  call deg2grid(dlong,dlat,grid6)
  grid=grid6(1:4) !XXX explicitly truncate this -db
  go to 100

10 n=ng-NGBASE-1
  if(n.ge.1 .and.n.le.30) then
     grid(1:1)='-'
     grid(2:2)=char(48+n/10)
     grid(3:3)=char(48+mod(n,10))
  else if(n.ge.31 .and.n.le.60) then
     n=n-30
     grid(1:2)='R-'
     grid(3:3)=char(48+n/10)
     grid(4:4)=char(48+mod(n,10))
  else if(n.eq.61) then
     grid='RO'
  else if(n.eq.62) then
     grid='RRR'
  else if(n.eq.63) then
     grid='73'
  endif

100 return
end subroutine unpackgrid

subroutine unpackpfx(ng,call1)

  character*12 call1
  character*3 pfx

  if(ng.lt.60000) then
! Add-on prefix of 1 to 3 characters
     n=ng
     do i=3,1,-1
        nc=mod(n,37)
        if(nc.ge.0 .and. nc.le.9) then
           pfx(i:i)=char(nc+48)
        else if(nc.ge.10 .and. nc.le.35) then
           pfx(i:i)=char(nc+55)
        else
           pfx(i:i)=' '
        endif
        n=n/37
     enddo
     call1=pfx//'/'//call1(1:8)
     if(call1(1:1).eq.' ') call1=call1(2:)
     if(call1(1:1).eq.' ') call1=call1(2:)
  else
! Add-on suffix, one or teo characters
     i1=index(call1,' ')
     nc=ng-60000
     if(nc.ge.0 .and. nc.le.9) then
        call1=call1(:i1-1)//'/'//char(nc+48)
     else if(nc.ge.10 .and. nc.le.35) then
        call1=call1(:i1-1)//'/'//char(nc+55)
     else if(nc.ge.36 .and. nc.le.125) then
        nc1=(nc-26)/10
        nc2=mod(nc-26,10)
        call1=call1(:i1-1)//'/'//char(nc1+48)//char(nc2+48)
     endif
  endif

  return
end subroutine unpackpfx

subroutine wqdecode(data0,message,ntype)

  parameter (N15=32768)
  integer*1 data0(11)
  character*22 message
  character*12 callsign
  character*3 cdbm
  character grid4*4,grid6*6
  logical first
  character*12 dcall(0:N15-1)
  data first/.true./
  save first,dcall

! May want to have a timeout (say, one hour?) on calls fetched 
! from the hash table.

  if(first) then
     dcall='            '
     first=.false.
  endif

  message='                      '
  call unpack50(data0,n1,n2)
  call unpackcall(n1,callsign)
  i1=index(callsign,' ')
  call unpackgrid(n2/128,grid4)
  ntype=iand(n2,127) -64

! Standard WSPR message (types 0 3 7 10 13 17 ... 60)
  if(ntype.ge.0 .and. ntype.le.62) then
     nu=mod(ntype,10)
     if(nu.eq.0 .or. nu.eq.3 .or. nu.eq.7) then
        write(cdbm,'(i3)') ntype
        if(cdbm(1:1).eq.' ') cdbm=cdbm(2:)
        if(cdbm(1:1).eq.' ') cdbm=cdbm(2:)
        message=callsign(1:i1)//grid4//' '//cdbm
        call hash(callsign,i1-1,ih)
        dcall(ih)=callsign(:i1)
     else
        nadd=nu
        if(nu.gt.3) nadd=nu-3
        if(nu.gt.7) nadd=nu-7
        ng=n2/128 + 32768*(nadd-1)
        call unpackpfx(ng,callsign)
        ndbm=ntype-nadd
        write(cdbm,'(i3)') ndbm
        if(cdbm(1:1).eq.' ') cdbm=cdbm(2:)
        if(cdbm(1:1).eq.' ') cdbm=cdbm(2:)
        i2=index(callsign,' ')
        message=callsign(:i2)//cdbm
        call hash(callsign,i2-1,ih)
        dcall(ih)=callsign(:i2)
     endif
  else if(ntype.lt.0) then
     ndbm=-(ntype+1)
     grid6=callsign(6:6)//callsign(1:5)
     ih=(n2-ntype-64)/128
     callsign=dcall(ih)
     write(cdbm,'(i3)') ndbm
     if(cdbm(1:1).eq.' ') cdbm=cdbm(2:)
     if(cdbm(1:1).eq.' ') cdbm=cdbm(2:)
     i2=index(callsign,' ')
     if(dcall(ih)(1:1).ne.' ') then
        message='<'//callsign(:i2-1)//'> '//grid6//' '//cdbm
     else
        message='<...> '//grid6//' '//cdbm
     endif
  endif

  return
end subroutine wqdecode
