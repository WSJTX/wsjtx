subroutine q65_loops(c00,npts2,nsps,mode,mode64,nsubmode,nFadingModel,   &
     ndepth,jpk0,xdt,f0,width,iaptype,APmask,APsymbols,snr1,snr2,irc,dat4)

  use packjt77
  use timer_module, only: timer
  parameter (LN=2176*63)           !LN=LL*NN; LL = 64*(mode64+2)
  character*37 decoded
  character*77 c77
  complex c00(0:npts2-1)           !Analytic representation of dd(), 6000 Hz
  complex ,allocatable :: c0(:)    !Ditto, with freq shift
  real a(3)                        !twkfreq params f,f1,f2
  real s3(LN)                      !Symbol spectra
  real s3avg(LN)                   !Averaged symbol spectra
  real s3prob(LN)                  !Symbol-value probabilities
  logical unpk77_success
  integer APmask(13)
  integer APsymbols(13)
  integer dat4(13)                 !Decoded message (as 13 six-bit integers)
  integer nap(0:11)                !AP return codes
  data nap/0,2,3,2,3,4,2,3,6,4,6,6/,nsave/0/
  save nsave,s3avg

  ircbest=9999
  allocate(c0(0:npts2-1))
  irc=-99
  s3lim=20.
!  ibwmax=11
!  if(mode64.le.4) ibwmax=9
!  ibwmin=ibwmax
!  idtmax=3
!  call qra_params(ndepth,maxaptype,idfmax,idtmax,ibwmin,ibwmax,maxdist)
  idfmax=5
  idtmax=5
  ibwmin=1
  ibwmax=2
  maxdist=15
  LL=64*(mode64+2)
  NN=63
  napmin=99
  baud=6000.0/nsps
  
  maxavg=0
  if(iand(ndepth,16).ne.0) maxavg=1
  do iavg=0,maxavg
     if(iavg.eq.1) then
        idfmax=1
        idtmax=1
     endif
     do idf=1,idfmax
        ndf=idf/2
        if(mod(idf,2).eq.0) ndf=-ndf
        a=0.
        a(1)=-(f0+0.5*baud*ndf)
        call twkfreq(c00,c0,npts2,6000.0,a)
        do idt=1,idtmax
           ndt=idt/2
           if(iaptype.eq.0 .and. iavg.eq.0) then
              if(mod(idt,2).eq.0) ndt=-ndt
              jpk=jpk0 + nsps*ndt/16              !tsym/16
              if(jpk.lt.0) jpk=0
              call timer('spec64  ',0)
              call spec64(c0,nsps,mode,mode64,jpk,s3,LL,NN)
              call timer('spec64  ',1)
              call pctile(s3,LL*NN,40,base)
              s3=s3/base
              where(s3(1:LL*NN)>s3lim) s3(1:LL*NN)=s3lim
           endif
           if(iavg.eq.1) then
              s3(1:LL*NN)=s3avg(1:LL*NN)
           endif
           do ibw=ibwmin,ibwmax
              nbw=ibw
              ndist=ndf**2 + ndt**2 + ((nbw-2))**2
              if(ndist.gt.maxdist) cycle
!              b90=1.728**ibw
              b90=3.0**nbw
              if(b90.gt.230.0) cycle
!              if(b90.lt.0.15*width) exit
              call timer('q65_intr',0)
              call q65_intrinsics_ff(s3,nsubmode,b90,nFadingModel,s3prob)
              call timer('q65_intr',1)

              call timer('q65_dec ',0)
              call q65_dec(s3,s3prob,APmask,APsymbols,esnodb,dat4,irc)
              call timer('q65_dec ',1)
              if(irc.ge.0) go to 100
              ! irc > 0 ==> number of iterations required to decode
              !  -1 = invalid params
              !  -2 = decode failed
              !  -3 = CRC mismatch
           enddo  ! ibw (b90 loop)
        enddo  ! idt (DT loop)
     enddo  ! idf (f0 loop)
     if(iaptype.eq.0 .and. iavg.eq.0) then
        a=0.
        a(1)=-f0
        call twkfreq(c00,c0,npts2,6000.0,a)
        jpk=3000                       !###  Are these definitions OK?
        if(nsps.ge.3600) jpk=6000      !###  TR >= 60 s
        call spec64(c0,nsps,mode,mode64,jpk,s3,LL,NN)
        call pctile(s3,LL*NN,40,base)
        s3=s3/base
        where(s3(1:LL*NN)>s3lim) s3(1:LL*NN)=s3lim
        s3avg(1:LL*NN)=s3avg(1:LL*NN)+s3(1:LL*NN)
        nsave=nsave+1
     endif
     if(iavg.eq.0 .and. nsave.lt.2) exit
  enddo  ! iavg

100  if(irc.ge.0) then
     navg=nsave
     snr2=esnodb - db(2500.0/baud)
     if(iavg.eq.0) navg=0
!### For tests only:
     open(53,file='fort.53',status='unknown',position='append')
     write(c77,1100) dat4(1:12),dat4(13)/2
1100 format(12b6.6,b5.5)
     call unpack77(c77,0,decoded,unpk77_success) !Unpack to get msgsent
     write(53,3053) ndf,ndt,nbw,b90,xdt,f0,snr2,ndist,irc,iaptype,navg,  &
          snr1,trim(decoded)
3053 format(3i4,f6.1,f6.2,f7.1,f6.1,4i4,f7.2,1x,a)
     close(53)
!###  
     nsave=0
     s3avg=0.
     irc=irc + 100*navg
  endif

  return
end subroutine q65_loops
