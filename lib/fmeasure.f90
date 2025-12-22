!-------------------------------------------------------------------------------
!
! This file is part of the WSPR application, Weak Signal Propagation Reporter
!
! File Name:    fmeasure.f90
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
program fmeasure

  parameter(NZ=1000)
  implicit real*8 (a-h,o-z)
  character infile*50
  character line*80

  nargs=iargc()
  if(nargs.ne.1) then
     print*,'Usage:   fmeasure <infile>'
     print*,'Example: fmeasure fmtave.out'
     go to 999
  endif
  call getarg(1,infile)

  open(10,file=infile,status='old',err=997)
  open(11,file='fcal.out',status='old',err=998)
  open(12,file='fmeasure.out',status='unknown')

  read(11,*) a,b

  write(*,1000) 
  write(12,1000) 
1000 format('    Freq     DF     A+B*f     Corrected  Offset'/        &
            '   (MHz)    (Hz)    (Hz)        (MHz)      (Hz)'/        &
            '-----------------------------------------------')       
  i=0
  do j=1,9999
     read(10,1010,end=999) line
1010 format(a80)
     i0=index(line,' 0 ')
     if(i0.gt.0) then
        read(line,*,err=5) f,df
        dial_error=a + b*f
        fcor=f + 1.d-6*df - 1.d-6*dial_error
        offset_hz=1.d6*(fcor-f)
        write(*,1020)  f,df,dial_error,fcor,offset_hz
        write(12,1020) f,df,dial_error,fcor,offset_hz
1020    format(3f8.3,f15.9,f8.2)
     endif
5    continue
  enddo

  go to 999

997 print*,'Cannot open input file: ',infile
  go to 999
998 print*,'Cannot open fcal.out'

999 end program fmeasure
