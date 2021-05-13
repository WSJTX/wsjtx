subroutine q65_loops(c00,npts2,nsps2,nsubmode,ndepth,jpk0,    &
     xdt0,f0,iaptype,xdt1,f1,snr2,dat4,idec)

  use packjt77
  use timer_module, only: timer
  use q65
  
  parameter (NN=63)
  parameter (LN=2176*63)           !LN=LL*NN; LL=64*(mode_q65+2), NN=63
  complex c00(0:npts2-1)           !Analytic representation of dd(), 6000 Hz
  complex ,allocatable :: c0(:)    !Ditto, with freq shift
  character decoded*37
  real a(3)                        !twkfreq params f,f1,f2
  real,allocatable :: s3(:)        !Symbol spectra
  integer dat4(13)                 !Decoded message (as 13 six-bit integers)
  integer nap(0:11)                !AP return codes
  data nap/0,2,3,2,3,4,2,3,6,4,6,6/

  LL=64*(mode_q65+2)
  allocate(s3(LL*NN))
  allocate(c0(0:npts2-1))
  idec=-1
  ircbest=9999
  irc=-99
  s3lim=20.
  baud=6000.0/nsps2

  idfmax=1
  idtmax=1
  maxdist=4
  ibw0=(ibwa+ibwb)/2
  if(iand(ndepth,3).eq.2) then
     idfmax=3
     idtmax=3
     maxdist=5
  endif
  if(iand(ndepth,3).eq.3) then
     idfmax=5
     idtmax=5
     maxdist=15
  endif

  napmin=99
  xdt1=xdt0
  f1=f0
  idfbest=0
  idtbest=0
  ndistbest=0

  do idf=1,idfmax
     ndf=idf/2
     if(mod(idf,2).eq.0) ndf=-ndf
     a=0.
     a(1)=-(f0+0.5*baud*ndf)
     call twkfreq(c00,c0,npts2,6000.0,a)
     do idt=1,idtmax
        ndt=idt/2
        if(mod(idt,2).eq.0) ndt=-ndt
        jpk=jpk0 + nsps2*ndt/16              !tsym/16
        if(jpk.lt.0) jpk=0
        call spec64(c0,nsps2,mode_q65,jpk,s3,LL,NN)
        call pctile(s3,LL*NN,40,base)
        s3=s3/base
        where(s3(1:LL*NN)>s3lim) s3(1:LL*NN)=s3lim
        call q65_bzap(s3,LL)                   !Zap birdies
        do ibw=ibwa,ibwb
           ndist=ndf**2 + ndt**2 + (ibw-ibw0)**2
           if(ndist.gt.maxdist) cycle
           b90=1.72**ibw
           if(b90.gt.345.0) cycle
           b90ts = b90/baud
           call timer('dec2    ',0)
           call q65_dec2(s3,nsubmode,b90ts,esnodb,irc,dat4,decoded)
           call timer('dec2    ',1)
              ! irc > 0 ==> number of iterations required to decode
              !  -1 = invalid params
              !  -2 = decode failed
              !  -3 = CRC mismatch
           if(irc.ge.0) then
              idfbest=idf
              idtbest=idt
              ndistbest=ndist
              nrc=irc
              go to 100
           endif
        enddo  ! ibw (b90 loop)
     enddo  ! idt (DT loop)
  enddo  ! idf (f0 loop)

100 if(irc.ge.0) then
     idec=iaptype
     snr2=esnodb - db(2500.0/baud)
     xdt1=xdt0 +  nsps2*ndt/(16.0*6000.0)
     f1=f0 + 0.5*baud*ndf
  endif

  return
end subroutine q65_loops
