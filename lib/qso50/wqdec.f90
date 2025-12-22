subroutine wqdec(data0,message,ntype)

  use packjt
  parameter (N15=32758)
  integer*1 data0(11)
  character*22 message
  character*12 callsign
  character*3 cdbm,cf
  character*2 crpt
  character*4 grid,psfx
  character*9 name
  character*36 fmt
  character*6 cwx(4)
  character*7 cwind(5)
  character ccur*4,cxp*2
  logical first
  character*12 dcall(0:N15-1)
  data first/.true./
  data cwx/'CLEAR','CLOUDY','RAIN','SNOW'/
  data cwind/'CALM','BREEZES','WINDY','DRY','HUMID'/
  save first,dcall

  if(first) then
     dcall='            '
     first=.false.
  endif

  message='                      '
  call unpack50(data0,n1,n2)
  call unpackcall(n1,callsign,iv2,psfx)
  i1=index(callsign,' ')
  call unpackgrid(n2/128,grid)
  ntype=iand(n2,127) -64

! Standard WSPR message (types 0 3 7 10 13 17 ... 60)
  nu=mod(ntype,10)
  if(ntype.ge.0 .and. ntype.le.60 .and. (nu.eq.0 .or. nu.eq.3 .or.   &
       nu.eq.7)) then
     write(cdbm,'(i3)'),ntype
     if(cdbm(1:1).eq.' ') cdbm=cdbm(2:)
     if(cdbm(1:1).eq.' ') cdbm=cdbm(2:)
     message=callsign(1:i1)//grid//' '//cdbm
     call hash(callsign,i1-1,ih)
     dcall(ih)=callsign(:i1)

! "Best DX" WSPR response (type 1)
  else if(ntype.eq.1) then
     message=grid//' DE '//callsign

! CQ (msg 3; types 2,4,5)
  else if(ntype.eq.2) then
     message='CQ '//callsign(:i1)//grid
     call hash(callsign,i1-1,ih)
     dcall(ih)=callsign(:i1)
  else if(ntype.eq.4 .or. ntype.eq.5) then
     ng=n2/128 + 32768*(ntype-4)
     call unpackpfx(ng,callsign)
     message='CQ '//callsign
     call hash(callsign,i1-1,ih)
     dcall(ih)=callsign(:i1)

! Reply to CQ (msg #2; type 6)
  else if(ntype.eq.6) then
     ih=(n2-64-ntype)/128
     if(dcall(ih)(1:1).ne.' ') then
        i2=index(dcall(ih),' ')
        message='<'//dcall(ih)(:i2-1)//'> '//callsign(:i1-1)
     else
        message='<...> '//callsign
     endif
     call hash(callsign,i1-1,ih)
     dcall(ih)=callsign(:i1-1)

! Reply to CQ (msg #2; type 8)
  else if(ntype.eq.8) then
     message='DE '//callsign(:i1)//grid
     call hash(callsign,i1-1,ih)
     dcall(ih)=callsign(:i1-1)

! Reply to CQ, DE pfx/call (msg #2; types 9, 11)
  else if(ntype.eq.9 .or. ntype.eq.11) then
     ng=n2/128 + 32768*(ntype-9)/2
     call unpackpfx(ng,callsign)
     message='DE '//callsign
     call hash(callsign,i1-1,ih)
     dcall(ih)=callsign(:i1-1)

! Calls and report (msg #3; types -1 to -9)
  else if(ntype.le.-1 .and. ntype.ge.-9) then
     write(crpt,1010) -ntype
1010 format('S',i1)
     ih=(n2-62-ntype)/128
     if(dcall(ih)(1:1).ne.' ') then
        i2=index(dcall(ih),' ')
        message=callsign(:i1)//'<'//dcall(ih)(:i2-1)//'> '//crpt
     else
        message=callsign(:i1)//'<...> '//crpt
     endif
     call hash(callsign,i1-1,ih)
     dcall(ih)=callsign(:i1-1)

! pfx/call and report (msg #3; types -10 to -27)
  else if(ntype.le.-10 .and. ntype.ge.-27) then
     ng=n2/128
     nrpt=-ntype-9
     if(ntype.le.-19) then
        ng=ng + 32768
        nrpt=-ntype-18
     endif
     write(crpt,1010) nrpt
     call unpackpfx(ng,callsign)
     message=callsign//' '//crpt
     call hash(callsign,i1-1,ih)
     dcall(ih)=callsign(:i1-1)

! Calls and R and report (msg #4; types -28 to -36)
  else if(ntype.le.-28 .and. ntype.ge.-36) then
     write(crpt,1010) -(ntype+27)
     ih=(n2-64+28-ntype)/128
     if(dcall(ih)(1:1).ne.' ') then
        i2=index(dcall(ih),' ')
        message=callsign(:i1)//'<'//dcall(ih)(:i2-1)//'> '//'R '//crpt
     else
        message=callsign(:i1)//'<...> '//'R '//crpt
     endif
     call hash(callsign,i1-1,ih)
     dcall(ih)=callsign(:i1-1)

! pfx/call R and report (msg #4; types -37 to -54)
  else if(ntype.le.-37 .and. ntype.ge.-54) then
     ng=n2/128
     nrpt=-ntype-36
     if(ntype.le.-46) then
        ng=ng + 32768
        nrpt=-ntype-45
     endif
     write(crpt,1010) nrpt
     call unpackpfx(ng,callsign)
     message=callsign//' R '//crpt
     call hash(callsign,i1-1,ih)
     dcall(ih)=callsign(:i1-1)

! Calls and RRR (msg#5; type 12)
  else if(ntype.eq.12) then
     ih=(n2-64+28-ntype)/128
     if(dcall(ih)(1:1).ne.' ') then
        i2=index(dcall(ih),' ')
        message=callsign(:i1)//'<'//dcall(ih)(:i2-1)//'> RRR'
     else
        message=callsign(:i1)//'<...> RRR'
     endif
     call hash(callsign,i1-1,ih)
     dcall(ih)=callsign(:i1-1)

! Calls and RRR (msg#5; type 14)
  else if(ntype.eq.14) then
     ih=(n2-64+28-ntype)/128
     if(dcall(ih)(1:1).ne.' ') then
        i2=index(dcall(ih),' ')
        message='<'//dcall(ih)(:i2-1)//'> '//callsign(:i1)//'RRR'
     else
        message='<...> '//callsign(:i1)//' RRR'
     endif
     call hash(callsign,i1-1,ih)
     dcall(ih)=callsign(:i1-1)

! DE pfx/call and RRR (msg#5; types 15, 16)
  else if(ntype.eq.15 .or. ntype.eq.16) then
     ng=n2/128 + 32768*(ntype-15)
     call unpackpfx(ng,callsign)
     message='DE '//callsign//' RRR'
     call hash(callsign,i1-1,ih)
     dcall(ih)=callsign(:i1-1)

! TNX [name] 73 GL (msg #6; type 18)
  else if(ntype.eq.18) then
     ng=(n2-18-64)/128
     call unpackname(n1,ng,name,len)
     message='TNX '//name(:len)//' 73 GL'

! OP [name] 73 GL (msg #6; type 18)
  else if(ntype.eq.-56) then
     ng=(n2+56-64)/128
     call unpackname(n1,ng,name,len)
     message='OP '//name(:len)//' 73 GL'

! 73 DE [call] [grid] (msg #6; type 19)
  else if(ntype.eq.19) then
     ng=(n2-19-64)/128
     message='73 DE '//callsign(:i1)//grid
     call hash(callsign,i1-1,ih)
     dcall(ih)=callsign(:i1-1)

! 73 DE pfx/call (msg #6; type 21, 22)
  else if(ntype.eq.21 .or. ntype.eq.22) then
     ng=n2/128 + (ntype-21)*32768
     call unpackpfx(ng,callsign)
     i1=index(callsign,' ')
     message='73 DE '//callsign
     call hash(callsign,i1-1,ih)
     dcall(ih)=callsign(:i1-1)

! [power] W [gain] DBD 73 GL (msg#6; type 24, 25)
  else if(ntype.eq.24 .or. ntype.eq.25) then
     ng=(n2-24-64)/128 - 32
     i1=1
     if(n1.gt.0) i1=log10(float(n1)) + 1
     i2=1
     if(ng.ge.10) i2=2
     if(ng.lt.0) i2=i2+1
     if(n1.le.3000) then
        if(ntype.eq.24) fmt="(i4,' W ',i2,' DBD 73 GL')"
        if(ntype.eq.25) fmt="(i4,' W ',i2,' DBD      ')"
        fmt(3:3)=char(48+i1)
        fmt(12:12)=char(48+i2)
        if(ng.le.100) then
           write(message,fmt) n1,ng
        else
           if(ng.eq.30000) fmt=fmt(1:8)//"DIPOLE')"
           if(ng.eq.30001) fmt=fmt(1:8)//"VERTICAL')"
           write(message,fmt) n1
        endif
     else
        mw=n1-3000
        if(ntype.eq.24) fmt="('0.',i3.3,' W ',i2,' DBD 73 GL')"
        if(ntype.eq.25) fmt="('0.',i3.3,' W ',i2,' DBD      ')"
        fmt(19:19)=char(48+i2)
        if(ng.le.100) then
           write(message,fmt) mw,ng
        else
           if(ng.eq.30000) fmt=fmt(1:15)//"DIPOLE')"
           if(ng.eq.30001) fmt=fmt(1:15)//"VERTICAL')"
           write(message,fmt) n1
        endif
        if(index(message,'***').gt.0) go to 700
     endif

! QRZ call (msg #3; type 26)
  else if(ntype.eq.26) then
     ng=(n2-24-64)/128 - 32
     message='QRZ '//callsign

! PSE QSY [nnn] KHZ (msg #6; type 28)
  else if(ntype.eq.28) then
     if(n1.gt.0) i1=log10(float(n1)) + 1
     fmt="('PSE QSY ',i2,' KHZ')"
     fmt(14:14)=char(48+i1)
     write(message,fmt) n1        

! WX wx temp C/F wind (msg #6; type 29)
  else if(ntype.eq.29) then
     nwx=n1/10000
     ntemp=mod(n1,10000) - 100
     cf=' F '
     if(ntemp.gt.800) then
        ntemp=ntemp-1000
        cf=' C '
     endif
     n2a=n2/128
     if(nwx.ge.1 .and. nwx.le.4 .and. n2a.ge.1 .and. n2a.le.5) then
        write(message,1020) cwx(nwx),ntemp,cf,cwind(n2/128)
1020    format('WX ',a6,i3,a3,a7)
     else
        message='WX'//' (BadMsg)'
     endif

! Hexadecimal data (type 62)
  else if(ntype.eq.62) then
     ng=n2/128
     write(message,'(z4.4,z7.7)') ng,n1
     
! Solar/geomagnetic/ionospheric data (type 63)
  else if(ntype.eq.63) then
     ih=(n2-64-ntype)/128
     if(dcall(ih)(1:1).ne.' ') then
        i2=index(dcall(ih),' ')
        message='<'//dcall(ih)(:i2-1)//'> '
     else
        message='<...> '
     endif
     call unpackprop(n1,k,muf,ccur,cxp)
     i2=index(message,'>')
     write(message(i2+1:),'(i3,i3)') k,muf
     message=message(:i2+7)//ccur//' '//cxp
     
! [plain text] (msg #6; type -57)
  else if(ntype.eq.-57) then
     ng=n2/128
     call unpacktext2(n1,ng,message)
  else
     go to 700
  endif
  go to 750

!     message='<Unknown message type>'
700 i1=index(callsign,' ')
  if(i1.lt.1) i1=12
  message=callsign(:i1)//' (BadMsg)'

750 do i=1,22
     if(ichar(message(i:i)).eq.0) message(i:i)=' '
  enddo

  do i=22,1,-1
     if(message(i:i).ne.' ') go to 800
  enddo
800 i2=i
  do n=1,20
     i1=index(message(:i2),'  ')
     if(i1.le.0) go to 900
     message=message(1:i1)//message(i1+2:)
     i2=i2-1
  enddo

900  return
end subroutine wqdec
