subroutine q65_loops(c00,nutc,npts2,nsps,mode,mode_q65,nsubmode,nFadingModel, &
     ndepth,jpk0,xdt0,f0,iaptype,APmask,APsymbols,codewords,snr1,       &
     xdt1,f1,snr2,dat4,id2,id3)

  use packjt77
  use timer_module, only: timer
  parameter (NN=63)
  parameter (LN=1152*63)           !LN=LL*NN; LL=64*(mode_q65+2), NN=63
  character*37 decoded
  character*77 c77
  complex c00(0:npts2-1)           !Analytic representation of dd(), 6000 Hz
  complex ,allocatable :: c0(:)    !Ditto, with freq shift
  real a(3)                        !twkfreq params f,f1,f2
  real s3(LN)                      !Symbol spectra
  real s3avg(LN)                   !Averaged symbol spectra
  real s3prob(64*NN)               !Symbol-value probabilities
  logical unpk77_success
  integer APmask(13)
  integer APsymbols(13)
  integer codewords(63,64)
  integer cw4(63)
  integer dat4(13)                 !Decoded message (as 13 six-bit integers)
  integer nap(0:11)                !AP return codes
  data nap/0,2,3,2,3,4,2,3,6,4,6,6/,nsave/0/
  data cw4/0, 0, 0, 0, 8, 4,60,35,17,48,33,25,34,43,43,43,35,15,46,30, &
          54,24,26,26,57,57,42, 3,23,11,49,49,16, 2, 6, 6,55,21,39,51, &
          51,51,42,42,50,25,31,35,57,30, 1,54,54,10,10,22,44,58,57,40, &
          21,21,19/

  save nsave,s3avg

  id2=0
  id3=0
  ircbest=9999
  allocate(c0(0:npts2-1))
  irc=-99
  s3lim=20.

  idfmax=3
  idtmax=3
  ibwmin=1
  ibwmax=2
  maxdist=5
  if(iand(ndepth,3).ge.2) then
     idfmax=5
     idtmax=5
     maxdist=15
  endif
  if(iand(ndepth,3).eq.3) then
     ibwmax=5
  endif
  LL=64*(mode_q65+2)
  napmin=99
  baud=6000.0/nsps
  xdt1=xdt0
  f1=f0

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
              call spec64(c0,nsps,mode,mode_q65,jpk,s3,LL,NN)
              call timer('spec64  ',1)
              call pctile(s3,LL*NN,40,base)
              s3=s3/base
              where(s3(1:LL*NN)>s3lim) s3(1:LL*NN)=s3lim
           endif
           kavg=0
           if(iavg.eq.1 .and. nsave.ge.2) then
              s3(1:LL*NN)=s3avg(1:LL*NN)
              kavg=nsave
           endif
           do ibw=ibwmin,ibwmax
              nbw=ibw
              ndist=ndf**2 + ndt**2 + ((nbw-2))**2
              if(ndist.gt.maxdist) cycle
!              b90=1.728**ibw
              b90=3.0**nbw
              if(b90.gt.230.0) cycle
              call timer('q65_intr',0)
              b90ts = b90/baud
              call q65_intrinsics_ff(s3,nsubmode,b90ts,nFadingModel,s3prob)
              call timer('q65_intr',1)
              if(iaptype.eq.4) then
                 codewords(1:63,4)=cw4
                 call timer('q65_apli',0)
                 call q65_dec_fullaplist(s3,s3prob,codewords,4,esnodb,   &
                      dat4,plog,irc)
                 call timer('q65_apli',1)
                 if(irc.ge.0) id2=4
              else
                 call timer('q65_dec ',0)
                 call q65_dec(s3,s3prob,APmask,APsymbols,esnodb,dat4,irc)
                 call timer('q65_dec ',1)
                 if(irc.ge.0) id2=iaptype
              endif
!              write(71,3071) 100*nutc,0.0,ndf,ndt,nbw,ndist,irc,iaptype,  &
!                   kavg,nsave
!3071          format(i6.6,f8.4,8i5)
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
        call spec64(c0,nsps,mode,mode_q65,jpk,s3,LL,NN)
        call pctile(s3,LL*NN,40,base)
        s3=s3/base
        where(s3(1:LL*NN)>s3lim) s3(1:LL*NN)=s3lim
        s3avg(1:LL*NN)=s3avg(1:LL*NN) + s3(1:LL*NN)
        nsave=nsave+1
     endif
     if(iavg.eq.0 .and. nsave.lt.2) exit
  enddo  ! iavg

100 if(irc.ge.0) then
     navg=nsave
     snr2=esnodb - db(2500.0/baud)
     if(kavg.eq.0) navg=0
     xdt1=xdt0 +  nsps*ndt/(16.0*6000.0)
     f1=f0 + 0.5*baud*ndf
!### For tests only:
!     open(53,file='fort.53',status='unknown',position='append')
!    write(c77,1100) dat4(1:12),dat4(13)/2
!1100 format(12b6.6,b5.5)
!     call unpack77(c77,0,decoded,unpk77_success) !Unpack to get msgsent
!     m=nutc
!     if(nsps.ge.3600) m=100*m
!     ihr=m/10000
!     imin=mod(m/100,100)
!     isec=mod(m,100)
!     hours=ihr + imin/60.0 + isec/3600.0
!     write(53,3053) m,hours,ndf,ndt,nbw,ndist,irc,iaptype,kavg,snr1,   &
!          xdt1,f1,snr2,trim(decoded)
!3053 format(i6.6,f8.4,4i3,i4,2i3,f6.1,f6.2,f7.1,f6.1,1x,a)
!     close(53)
!###  
     nsave=0
     s3avg=0.
     irc=irc + 100*navg
  endif

  return
end subroutine q65_loops
