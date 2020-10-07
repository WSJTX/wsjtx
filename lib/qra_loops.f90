subroutine qra_loops(c00,npts2,mode,mode64,nsubmode,nFadingModel,minsync,  &
     ndepth,nc1,nc2,ng2,naptype,jpk0,dtx,f0,width,snr2,s3,irc,dat4)

  use timer_module, only: timer
  parameter (LN=1152*63)
  complex c00(0:720000)            !Analytic representation of dd(), 6000 Hz
  complex c0(0:720000)             !Ditto, with freq shift
  real a(3)                        !twkfreq params f,f1,f2
  real s3(LN),s3a(LN)              !Symbol spectra
  integer dat4(12),dat4x(12)       !Decoded message (as 12 integers)
  integer nap(0:11)                !AP return codes
  data nap/0,2,3,2,3,4,2,3,6,4,6,6/
!  save

  irc=-99
  s3lim=20.
  ibwmax=11
  if(mode64.le.4) ibwmax=9
  ibwmin=0
  idtmax=5
  if(minsync.eq.-2) then
     ibwmin=ibwmax
     idtmax=3
  endif
  LL=64*(mode64+2)
  NN=63
  napmin=99
  ncall=0

  do idf0=1,11
     idf=idf0/2
     if(mod(idf0,2).eq.0) idf=-idf
     a=0.
     a(1)=-(f0+0.868*idf)
     call twkfreq(c00,c0,npts2,6000.0,a)
     do idt0=1,idtmax
        idt=idt0/2
        if(mod(idt0,2).eq.0) idt=-idt
           jpk=jpk0 + 750*idt
        if(mode.eq.64) then
           call spec64(c0,jpk,s3a,LL,NN)
        else
           if(jpk.lt.0) jpk=0
           call timer('spec_q65',0)
           call spec_qra65(c0(jpk:),nsps2,s3,LL,NN) !Get synced symbol spectra
           call timer('spec_q65',1)
!           do j=1,63                              !Normalize to symbol baseline
!              call pctile(s3(:,j),LL,40,base)
!              s3(:,j)=s3(:,j)/base
!           enddo
!           LL2=64*(mode65+1)-1
!           s3max=20.0
!           do j=1,63                              !Apply AGC to suppress pings
!              xx=maxval(s3(-64:LL2,j))
!              if(xx.gt.s3max) s3(-64:LL2,j)=s3(-64:LL2,j)*s3max/xx
!           enddo
        endif
        call pctile(s3a,LL*NN,40,base)
        s3a=s3a/base
        where(s3a(1:LL*NN)>s3lim) s3a(1:LL*NN)=s3lim
        do ibw=ibwmax,ibwmin,-2
           b90=1.728**ibw
           if(b90.gt.230.0) cycle
           if(b90.lt.0.15*width) exit
           s3(1:LL*NN)=s3a(1:LL*NN)
           ncall=ncall+1
           call timer('qra64_de',0)
           call qra64_dec(s3,nc1,nc2,ng2,naptype,0,nSubmode,b90,      &
                nFadingModel,dat4,snr2,irc)
           call timer('qra64_de',1)
           if(irc.eq.0) go to 200
           if(irc.gt.0) call badmsg(irc,dat4,nc1,nc2,ng2)
           iirc=max(0,min(irc,11))
           if(irc.gt.0 .and. nap(iirc).lt.napmin) then
              dat4x=dat4
              b90x=b90
              snr2x=snr2
              napmin=nap(iirc)
              irckeep=irc
              dtxkeep=jpk/6000.0 - 1.0
              f0keep=-a(1)
              idfkeep=idf
              idtkeep=idt
              ibwkeep=ibw
           endif
        enddo  ! ibw (b90 loop)
        if(iand(ndepth,3).lt.3 .and. irc.ge.0) go to 100
        if(irc.eq.0) go to 100
     enddo  ! idt (DT loop)
  enddo  ! idf (f0 loop)

100 if(napmin.ne.99) then
     dat4=dat4x
     b90=b90x
     snr2=snr2x
     irc=irckeep
     dtx=dtxkeep
     f0=f0keep
     idt=idtkeep
     idf=idfkeep
     ibw=ibwkeep
  endif

200 return
end subroutine qra_loops
