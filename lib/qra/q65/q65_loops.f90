subroutine q65_loops(c00,npts2,nsps,mode_q65,nsubmode,ndepth,jpk0,    &
     xdt0,f0,iaptype,xdt1,f1,snr2,dat4,idec)

  use packjt77
  use timer_module, only: timer
  use q65
  
  parameter (NN=63)
  parameter (LN=1152*63)           !LN=LL*NN; LL=64*(mode_q65+2), NN=63
  complex c00(0:npts2-1)           !Analytic representation of dd(), 6000 Hz
  complex ,allocatable :: c0(:)    !Ditto, with freq shift
  character decoded*37
  real a(3)                        !twkfreq params f,f1,f2
  real s3(LN)                      !Symbol spectra
  integer dat4(13)                 !Decoded message (as 13 six-bit integers)
  integer nap(0:11)                !AP return codes
  data nap/0,2,3,2,3,4,2,3,6,4,6,6/

  idec=-1
  ircbest=9999
  allocate(c0(0:npts2-1))
  irc=-99
  s3lim=20.
  baud=6000.0/nsps

  idfmax=3
  idtmax=3
  ibw0=(ibwa+ibwb)/2
  maxdist=5
  if(iand(ndepth,3).ge.2) then
     idfmax=5
     idtmax=5
     maxdist=10
  endif
  if(iand(ndepth,3).eq.3) maxdist=15

  LL=64*(mode_q65+2)
  napmin=99
  xdt1=xdt0
  f1=f0

  do idf=1,idfmax
     ndf=idf/2
     if(mod(idf,2).eq.0) ndf=-ndf
     a=0.
     a(1)=-(f0+0.5*baud*ndf)
     call twkfreq(c00,c0,npts2,6000.0,a)
     do idt=1,idtmax
        ndt=idt/2
        if(mod(idt,2).eq.0) ndt=-ndt
        jpk=jpk0 + nsps*ndt/16              !tsym/16
        if(jpk.lt.0) jpk=0
        call timer('spec64  ',0)
        call spec64(c0,nsps,65,mode_q65,jpk,s3,LL,NN)
        call timer('spec64  ',1)
        call pctile(s3,LL*NN,40,base)
        s3=s3/base
        where(s3(1:LL*NN)>s3lim) s3(1:LL*NN)=s3lim
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
           if(irc.ge.0) go to 100
        enddo  ! ibw (b90 loop)
     enddo  ! idt (DT loop)
  enddo  ! idf (f0 loop)

100 if(irc.ge.0) then
!     write(55,3055) ndepth,iaptype,idf,idt,ibw,ndist,irc,sum(s3(1:LL*NN)), &
!          trim(decoded)
!3055 format(7i4,f10.1,1x,a)
     idec=iaptype+2
     snr2=esnodb - db(2500.0/baud)
     xdt1=xdt0 +  nsps*ndt/(16.0*6000.0)
     f1=f0 + 0.5*baud*ndf
  endif

  return
end subroutine q65_loops
