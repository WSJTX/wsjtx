!-------------------------------------------------------------------------------
!
! This file is part of the WSPR application, Weak Signal Propagation Reporter
!
! File Name:    wqdecode.f90
! Description:  
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
!  print*,data0,n1,n2
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

!-------------------------------------------------------------------------------
!
! This file is part of the WSPR application, Weak Signal Propagation Reporter
!
! File Name:    unpack50.f90
! Description:  
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

!-------------------------------------------------------------------------------
!
! This file is part of the WSPR application, Weak Signal Propagation Reporter
!
! File Name:    unpackcall.f90
! Description:  
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

!-------------------------------------------------------------------------------
!
! This file is part of the WSPR application, Weak Signal Propagation Reporter
!
! File Name:    unpackgrid.f90
! Description:  
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

!-------------------------------------------------------------------------------
!
! This file is part of the WSPR application, Weak Signal Propagation Reporter
!
! File Name:    unpackpfx.f90
! Description:  
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
