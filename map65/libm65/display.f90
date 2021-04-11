subroutine display(nkeep,ftol)

  parameter (MAXLINES=400,MX=400,MAXCALLS=500)
  integer indx(MAXLINES),indx2(MX)
  character*83 line(MAXLINES),line2(MX),line3(MAXLINES)
  character out*52,cfreq0*3,livecq*58
  character*6 callsign,callsign0
  character*12 freqcall(MAXCALLS)
  real freqkHz(MAXLINES)
  integer utc(MAXLINES),utc2(MX),utcz
  real*8 f0

  rewind 26

  do i=1,MAXLINES
     read(26,1010,end=10) line(i)
1010 format(a77)
     read(line(i),1020) f0,ndf,nh,nm
1020 format(f8.3,i5,25x,i3,i2)
     utc(i)=60*nh + nm
     freqkHz(i)=1000.d0*(f0-144.d0) + 0.001d0*ndf
  enddo

10 backspace(26)
  nz=i-1
  utcz=utc(nz)
  nz=nz-1
  if(nz.lt.1) go to 999
  nquad=max(nkeep/4,3)
  do i=1,nz
     nage=utcz-utc(i)
     if(nage.lt.0) nage=nage+1440
     iage=nage/nquad
     write(line(i)(73:74),1021) iage
1021 format(i2)
  enddo

  nage=utcz-utc(1)
  if(nage.lt.0) nage=nage+1440
  if(nage.gt.nkeep) then
     do i=1,nz
        nage=utcz-utc(i)
        if(nage.lt.0) nage=nage+1440
        if(nage.le.nkeep) go to 20
     enddo
20   i0=i
     nz=nz-i0+1
     rewind 26
     if(nz.lt.1) go to 999
     do i=1,nz
        j=i+i0-1
        line(i)=line(j)
        utc(i)=utc(j)
        freqkHz(i)=freqkHz(j)
        write(26,1022) line(i)
1022    format(a77)
     enddo
  endif

  call flush(26)
  call indexx(freqkHz,nz,indx)

  nstart=1
  k3=0
  k=1
  m=indx(1)
  if(m.lt.1 .or. m.gt.MAXLINES) then
     print*,'Error in display.f90: ',nz,m
     m=1
  endif
  line2(1)=line(m)
  utc2(1)=utc(m)
  do i=2,nz
     j0=indx(i-1)
     j=indx(i)
     if(freqkHz(j)-freqkHz(j0).gt.2.0*ftol) then
        if(nstart.eq.0) then
           k=k+1
           line2(k)=""
           utc2(k)=-1
        endif
        kz=k
        if(nstart.eq.1) then
           call indexx(float(utc2(1:kz)),kz,indx2)
           k3=0
           do k=1,kz
              k3=min(k3+1,400)
              line3(k3)=line2(indx2(k))
           enddo
           nstart=0
        else
           call indexx(float(utc2(1:kz)),kz,indx2)
           do k=1,kz
              k3=min(k3+1,400)
              line3(k3)=line2(indx2(k))
           enddo
        endif
        k=0
     endif
     if(i.eq.nz) then
        k=k+1
        line2(k)=""
        utc2(k)=-1
     endif
     k=k+1
     line2(k)=line(j)
     utc2(k)=utc(j)
     j0=j
  enddo
  kz=k
  call indexx(float(utc2(1:kz)),kz,indx2)
  do k=1,kz
     k3=min(k3+1,400)
     line3(k3)=line2(indx2(k))
  enddo

  rewind 19
  rewind 20
  cfreq0='   '
  nc=0
  callsign0='      '
  do k=1,k3
     out=line3(k)(6:13)//line3(k)(28:31)//line3(k)(39:45)//       &
          line3(k)(35:38)//line3(k)(46:74)
     if(out(1:3).ne.'   ') then
        cfreq0=out(1:3)
        if(iw.lt.MAXLINES-1) iw=iw+1
        livecq=line3(k)(6:13)//line3(k)(28:31)//line3(k)(39:45)//       &
             line3(k)(23:27)//line3(k)(35:38)//line3(k)(46:70)//        &
             line3(k)(73:77)
        if(index(livecq,' CQ ').gt.0 .or. index(livecq,' QRZ ').gt.0 .or.   &
           index(livecq,' QRT ').gt.0 .or. index(livecq,' CQV ').gt.0 .or.  &
           index(livecq,' CQH ').gt.0) write(19,1029) livecq
1029    format(a58)
        write(*,1030) out                           !Messages
1030    format('@',a52)
        i1=index(out(26:),' ')
        callsign=out(i1+26:)
        i2=index(callsign,' ')
        if(i2.gt.1) callsign(i2:)='      '
        if(callsign.ne.'      ' .and. callsign.ne.callsign0) then
           len=i2-1
           if(len.lt.0) len=6
           if(len.ge.4) then                        !Omit short "callsigns"
              if(nc.lt.MAXCALLS) nc=nc+1
              freqcall(nc)=cfreq0//' '//callsign//line3(k)(73:74)
              callsign0=callsign
           endif
        endif
        if(callsign.ne.'      ' .and. callsign.eq.callsign0) then
           freqcall(nc)=cfreq0//' '//callsign//line3(k)(73:74)
        endif
     endif
  enddo
  flush(19)
  if(nc.lt.MAXCALLS) nc=nc+1
  freqcall(nc)='            '
  if(nc.lt.MAXCALLS) nc=nc+1
  freqcall(nc)='            '
  freqcall(nc+1)='            '
  freqcall(nc+2)='            '

  do i=1,nc
  write(*,1042) freqcall(i)                         !Band Map
1042 format('&',a12)
  enddo

999  continue
  return
end subroutine display
