subroutine map65a(dd,ss,savg,newdat,nutc,fcenter,ntol,idphi,nfa,nfb,        &
     mousedf,mousefqso,nagain,ndecdone,nfshift,ndphi,max_drift,             &
     nfcal,nkeep,mcall3b,nsum,nsave,nxant,mycall,mygrid,                    &
     neme,ndepth,nstandalone,hiscall,hisgrid,nhsym,nfsample,                &
     ndiskdat,nxpol,nmode)

!  Processes timf2 data from Linrad to find and decode JT65 signals.

  use wideband_sync
  use timer_module, only: timer

  parameter (MAXMSG=1000)            !Size of decoded message list
  parameter (NSMAX=60*96000)
  real dd(4,NSMAX)
  real*4 ss(4,322,NFFT),savg(4,NFFT)
  real tavg(-50:50)                  !Temp for finding local base level
  real base(4)                       !Local basel level at 4 pol'ns
  real sig(MAXMSG,30)                !Parameters of detected signals
  real a(5)
  real*8 fcenter
  character*22 msg(MAXMSG)
  character*3 shmsg0(4)
  character mycall*12,hiscall*12,mygrid*6,hisgrid*6,cp*1,cm*1
  integer indx(MAXMSG),nsiz(MAXMSG)
  logical done(MAXMSG)
  logical xpol,bq65,q65b_called
  logical candec(MAX_CANDIDATES)
  logical ldecoded
  character decoded*22,blank*22,cmode*2
  real short(3,NFFT)                 !SNR dt ipol for potential shorthands
  real qphi(12)
  type(candidate) :: cand(MAX_CANDIDATES)
  
  common/c3com/ mcall3a
  common/testcom/ifreq
  common/early/nhsym1,nhsym2,ldecoded(32768)
  common/decodes/ndecodes

  data blank/'                      '/,cm/'#'/
  data shmsg0/'ATT','RO ','RRR','73 '/
  data nfile/0/,nutc0/-999/,nid/0/,ip000/1/,ip001/1/,mousefqso0/-999/
  save

  ndecodes=0

! Clean start for Q65 at early decode
  if(nhsym.eq.nhsym1 .or. nagain.ne.0) ldecoded=.false.
  if(ndiskdat.eq.1) ldecoded=.false.

  nkhz_center=nint(1000.0*(fcenter-int(fcenter)))
  mfa=nfa-nkhz_center+48
  mfb=nfb-nkhz_center+48
  mode65=mod(nmode,10)
  if(mode65.eq.3) mode65=4
  mode_q65=nmode/10
  nts_jt65=mode65                     !JT65 tone separation factor
  nts_q65=2**(mode_q65-1)             !Q65 tone separation factor
  xpol=(nxpol.ne.0)
  
! No second decode for JT65?
  if(nhsym.eq.nhsym2 .and. nagain.eq.0 .and.ndiskdat.eq.0) mode65=0

  if(nagain.eq.0) then
     call timer('get_cand',0)
     call get_candidates(ss,savg,xpol,nhsym,mfa,mfb,nts_jt65,nts_q65,cand,ncand)
     call timer('get_cand',1)
     candec=.false.
  endif
!###
!  do k=1,ncand
!     freq=cand(k)%f+nkhz_center-48.0-1.27046
!     ipk=cand(k)%indx
!     write(*,3010) nutc,k,db(cand(k)%snr),freq,cand(k)%xdt,    &
!          cand(k)%ipol,cand(k)%iflip,ipk,ldecoded(ipk)
!3010 format('=a',i5.4,i5,f8.2,f10.3,f8.2,2i3,i6,L4)
!  enddo
!###

  nwrite_q65=0
  bq65=mode_q65.gt.0

  mcall3a=mcall3b
  mousefqso0=mousefqso
  if(.not.xpol) ndphi=0
  nsum=0

!### Should use AppDir! ###
  open(23,file='CALL3.TXT',status='unknown')

  df=96000.0/NFFT                     !df = 96000/NFFT = 2.930 Hz
  if(nfsample.eq.95238) df=95238.1/NFFT
  ftol=0.010                          !Frequency tolerance (kHz)
  dphi=idphi/57.2957795
  foffset=0.001*(1270 + nfcal)              !Offset from sync tone, plus CAL
  fqso=mousefqso + foffset - 0.5*(nfa+nfb) + nfshift !fqso at baseband (khz)
  iloop=0

2  if(ndphi.eq.1) dphi=30*iloop/57.2957795

  if(nutc.ne.nutc0) nfile=nfile+1
  nutc0=nutc

  do nqd=1,0,-1
     if(nqd.eq.1) then                     !Quick decode, at fQSO
        fa=1000.0*(fqso+0.001*mousedf) - ntol
        fb=1000.0*(fqso+0.001*mousedf) + ntol + 4*53.8330078
     else                                  !Wideband decode at all freqs
        fa=-1000*0.5*(nfb-nfa) + 1000*nfshift
        fb= 1000*0.5*(nfb-nfa) + 1000*nfshift
     endif
     ia=nint(fa/df) + 16385
     ib=nint(fb/df) + 16385
     ia=max(51,ia)
     ib=min(32768-51,ib)
     if(ndiskdat.eq.1 .and. mode65.eq.0) ib=ia

     km=0
     nkm=1
     nz=n/8
     freq0=-999.
     sync10=-999.
     fshort0=-999.
     syncshort0=-999.
     ntry=0
     short=0.                                 !Zero the whole short array
     jpz=1
     if(xpol) jpz=4

! First steps for JT65 decoding
     do i=ia,ib                               !Search over freq range
        freq=0.001*(i-16385)*df
!  Find the local base level for each polarization; update every 10 bins.
        if(mod(i-ia,10).eq.0) then
           do jp=1,jpz
              do ii=-50,50
                 iii=i+ii
                 if(iii.ge.1 .and. iii.le.32768) then
                    tavg(ii)=savg(jp,iii)
                 else
                    write(13,*) 'Error in iii:',iii,ia,ib,fa,fb
                    flush(13)
                    go to 900
                 endif
              enddo
              call pctile(tavg,101,50,base(jp))
           enddo
        endif

!  Find max signal at this frequency
        smax=0.
        do jp=1,jpz
           if(savg(jp,i)/base(jp).gt.smax) then
              smax=savg(jp,i)/base(jp)
              jpmax=jp
           endif
        enddo

        if(smax.gt.1.1 .or. ia.eq.ib) then
!  Look for JT65 sync patterns and shorthand square-wave patterns.
           call timer('ccf65   ',0)
           ssmax=1.e30
           call ccf65(ss(1,1,i),nhsym,ssmax,sync1,ipol,jpz,dt,     &
                flipk,syncshort,snr2,ipol2,dt2)
!###           if(dt.lt.-2.6 .or. dt.gt.2.5) sync1=-99.0  !###
           call timer('ccf65   ',1)
           if(mode65.eq.0) syncshort=-99.0     !If "No JT65", don't waste time

! ########################### Search for Shorthand Messages #################
!  Is there a shorthand tone above threshold?
           thresh0=1.0
!  Use lower thresh0 at fQSO
           if(nqd.eq.1 .and. ntol.le.100) thresh0=0.
           if(syncshort.gt.thresh0) then
! ### Do shorthand AFC here (or maybe after finding a pair?) ###
              short(1,i)=syncshort
              short(2,i)=dt2
              short(3,i)=ipol2

!  Check to see if lower tone of shorthand pair was found.
              do j=2,4
                 i0=i-nint(j*mode65*10.0*(11025.0/4096.0)/df)
!  Should this be i0 +/- 1, or just i0?
!  Should we also insist that difference in DT be either 1.5 or -1.5 s?
                 if(short(1,i0).gt.thresh0) then
                    fshort=0.001*(i0-16385)*df
                    noffset=0
                    if(nqd.eq.1) noffset=nint(1000.0*(fshort-fqso)-mousedf)
                    if(abs(noffset).le.ntol) then
!  Keep only the best candidate within ftol.
!### NB: sync2 was not defined here!
!                       sync2=syncshort                   !### try this ???
                       if(fshort-fshort0.le.ftol .and.         &
                            syncshort.gt.syncshort0 .and. nkm.eq.2) km=km-1
                       if(fshort-fshort0.gt.ftol .or.                     &
                            syncshort.gt.syncshort0) then
                          if(km.lt.MAXMSG) km=km+1
                          sig(km,1)=nfile
                          sig(km,2)=nutc
                          sig(km,3)=fshort + 0.5*(nfa+nfb)
                          sig(km,4)=syncshort
                          sig(km,5)=dt2
                          sig(km,6)=45*(ipol2-1)/57.2957795
                          sig(km,7)=0
                          sig(km,8)=snr2
                          sig(km,9)=0
                          sig(km,10)=0
!                           sig(km,11)=rms0
                          sig(km,12)=savg(ipol2,i)
                          sig(km,13)=0
                          sig(km,14)=0
                          sig(km,15)=0
                          sig(km,16)=0
!                           sig(km,17)=0
                          sig(km,18)=0
                          msg(km)=shmsg0(j)
                          fshort0=fshort
                          syncshort0=syncshort
                          nkm=2
                       endif
                    endif
                 endif
              enddo
           endif

! ########################### Search for Normal Messages ###########
!  Is sync1 above threshold?
           thresh1=1.0
!  Use lower thresh1 at fQSO
           if(nqd.eq.1 .and. ntol.le.100) thresh1=0.
           noffset=0
           if(nqd.ge.1) noffset=nint(1000.0*(freq-fqso)-mousedf)
           if(newdat.eq.1 .and. sync1.gt.-99.0) then
              sync1=thresh1+1.0
              noffset=0
           endif
           if(sync1.gt.thresh1 .and. abs(noffset).le.ntol) then
!  Keep only the best candidate within ftol.
!  (Am I deleting any good decodes by doing this?)
              if(freq-freq0.le.ftol .and. sync1.gt.sync10 .and.       &
                   nkm.eq.1) km=km-1
              if(freq-freq0.gt.ftol .or. sync1.gt.sync10) then
                 nflip=nint(flipk)
                 f00=(i-1)*df          !Freq of detected sync tone (0-96000 Hz)
                 ntry=ntry+1
                 if((nqd.eq.1 .and. ntry.ge.40) .or.                  &
                          (nqd.eq.0 .and. ntry.ge.400)) then
! Too many calls to decode1a!
                    write(*,*) '! Signal too strong, or suspect data?  Decoding aborted.'
                    write(13,*) 'Signal too strong, or suspect data?  Decoding aborted.'
                    call flush(13)
                    go to 900
                 endif

                 call timer('decode1a',0)
                 ifreq=i
                 ikhz=nint(freq+0.5*(nfa+nfb)-foffset)-nfshift
                 idf=nint(1000.0*(freq+0.5*(nfa+nfb)-foffset-(ikHz+nfshift)))
                 call decode1a(dd,newdat,f00,nflip,mode65,nfsample,       &
                      xpol,mycall,hiscall,hisgrid,neme,ndepth,nqd,dphi,   &
                      ndphi,nutc,ikHz,idf,ipol,ntol,sync2,                &
                      a,dt,pol,nkv,nhist,nsum,nsave,qual,decoded)
                 call timer('decode1a',1)

! The case sync1=2.0 is just to make sure decode1a is called and bigfft done.
                 if(mode65.ne.0 .and. sync1.ne.2.000000) then
                    if(km.lt.MAXMSG) km=km+1
                    sig(km,1)=nfile
                    sig(km,2)=nutc
                    sig(km,3)=freq + 0.5*(nfa+nfb)
                    sig(km,4)=sync1
                    sig(km,5)=dt
                    sig(km,6)=pol
                    sig(km,7)=flipk
                    sig(km,8)=sync2
                    sig(km,9)=nkv
                    sig(km,10)=qual
!                    sig(km,11)=idphi
                    sig(km,12)=savg(ipol,i)
                    sig(km,13)=a(1)
                    sig(km,14)=a(2)
                    sig(km,15)=a(3)
                    sig(km,16)=a(4)
!                     sig(km,17)=a(5)
                    sig(km,18)=nhist
                    msg(km)=decoded
                    freq0=freq
                    sync10=sync1
                    nkm=1
                 endif
              endif
           endif
        endif
     enddo  !i=ia,ib

     if(nqd.eq.1) then
        nwrite=0
        if(mode65.eq.0) km=0
        do k=1,km
           decoded=msg(k)
           if(decoded.ne.'                      ') then
              nutc=sig(k,2)
              freq=sig(k,3)
              sync1=sig(k,4)
              dt=sig(k,5)
              npol=nint(57.2957795*sig(k,6))
              flip=sig(k,7)
              sync2=sig(k,8)
              nkv=sig(k,9)
              nqual=sig(k,10)
!              idphi=nint(sig(k,11))
              if(flip.lt.0.0) then
                 do i=22,1,-1
                    if(decoded(i:i).ne.' ') go to 8
                 enddo
                 stop 'Error in message format'
8                if(i.le.18) decoded(i+2:i+4)='OOO'
              endif
              nkHz=nint(freq-foffset)-nfshift
              mhz=fcenter                         ! ... +fadd ???
              f0=mhz+0.001*nkHz
              ndf=nint(1000.0*(freq-foffset-(nkHz+nfshift)))
              nsync1=sync1

              s2db=10.0*log10(sync2) - 40             !### empirical ###
              nsync2=nint(s2db)
              if(decoded(1:4).eq.'RO  ' .or. decoded(1:4).eq.'RRR  ' .or.  &
                   decoded(1:4).eq.'73  ') then
                 nsync2=nint(1.33*s2db + 2.0)
              endif

              nwrite=nwrite+1
              if(nxant.ne.0) then
                 npol=npol-45
                 if(npol.lt.0) npol=npol+180
              endif

              call txpol(xpol,decoded,mygrid,npol,nxant,ntxpol,cp)

              if(ndphi.eq.0) then
                 write(*,1010) nkHz,ndf,npol,nutc,dt,nsync2,    &
                      cm,decoded,nkv,nqual,ntxpol,cp
1010             format('!',i3,i5,i4,i6.4,f5.1,i5,1x,a1,1x,a22,i2,i5,i5,1x,a1)
              else
                 if(iloop.ge.1) qphi(iloop)=sig(k,10)
                 write(*,1010) nkHz,ndf,npol,nutc,dt,nsync2,    &
                      cm,decoded,nkv,nqual,30*iloop
                 write(27,1011) 30*iloop,nkHz,ndf,npol,nutc,  &
                      dt,sync2,nkv,nqual,cm,decoded
1011             format(i3,i4,i5,i4,i6.4,1x,f5.1,f7.1,i3,i5,a1,1x,a22)
              endif
           endif
        enddo  ! k=1,km

        if(bq65) then
           q65b_called=.false.
           do icand=1,ncand
              if(cand(icand)%iflip.ne.0) cycle        !Keep only Q65 candidates
              freq=cand(icand)%f+nkhz_center-48.0-1.27046
              nhzdiff=nint(1000.0*(freq-mousefqso)-mousedf) - nfcal
! Now looking for "quick decode" (nqd=1) candidates at cursor freq +/- ntol.
              if(nqd.eq.1 .and. abs(nhzdiff).gt.ntol) cycle
              ikhz=mousefqso
              q65b_called=.true.
              f0=cand(icand)%f
              call timer('q65b    ',0)
              call q65b(nutc,nqd,nxant,fcenter,nfcal,nfsample,ikhz,mousedf,   &
                   ntol,xpol,mycall,mygrid, hiscall,hisgrid,mode_q65,f0,fqso, &
                   newdat,nagain,max_drift,nhsym,idec)
              call timer('q65b    ',1)
              if(idec.ge.0) candec(icand)=.true.
           enddo
           if(.not.q65b_called) then
              freq=mousefqso + 0.001*mousedf
              ikhz=mousefqso
              f0=freq - (nkhz_center-48.0-1.27046)   !### ??? ###
              call timer('q65b    ',0)
              call q65b(nutc,nqd,nxant,fcenter,nfcal,nfsample,ikhz,mousedf,   &
                   ntol,xpol,mycall,mygrid,hiscall,hisgrid,mode_q65,f0,fqso,  &
                   newdat,nagain,max_drift,nhsym,idec)
              call timer('q65b    ',1)
           endif
        endif

        if(nwrite.eq.0 .and. nwrite_q65.eq.0) then
           write(*,1012) mousefqso,nutc
1012       format('!',i3,9x,i6.4,'  ')
        endif
     endif  !nqd.eq.1

     if(ndphi.eq.1 .and.iloop.lt.12) then
        iloop=iloop+1
        go to 2
     endif
     
     if(ndphi.eq.1 .and.iloop.eq.12) call getdphi(qphi)
     if(nqd.eq.1) then
        call sec0(1,tdec)
        write(*,1013) nsum,nsave,nstandalone,nhsym,tdec
1013    format('<QuickDecodeDone>',3i4,i6,f6.2)
        flush(6)
        open(16,file='tquick.dat',status='unknown',access='append')
        write(16,1016) nutc,tdec
1016    format(i4.4,f7.1)
        close(16)
     endif
     call sec0(1,tsec0)
     if(nhsym.eq.nhsym1 .and. tsec0.gt.3.0) go to 700
     if(nqd.eq.1 .and. nagain.eq.1) go to 900

     if(nqd.eq.0 .and. bq65) then
! Do the wideband Q65 decode        
        do icand=1,ncand
           if(cand(icand)%iflip.ne.0) cycle    !Do only Q65 candidates here
           if(candec(icand)) cycle             !Skip if already decoded
           freq=cand(icand)%f+nkhz_center-48.0-1.27046
!###! If here at nqd=1, do only candidates at mousefqso +/- ntol
!###           if(nqd.eq.1 .and. abs(freq-mousefqso).gt.0.001*ntol) cycle
           ikhz=nint(freq)
           f0=cand(icand)%f
           call timer('q65b    ',0)
           call q65b(nutc,nqd,nxant,fcenter,nfcal,nfsample,ikhz,mousedf,ntol, &
                xpol,mycall,mygrid,hiscall,hisgrid,mode_q65,f0,fqso,newdat,   &
                nagain,max_drift,nhsym,idec)
           call timer('q65b    ',1)
           if(idec.ge.0) candec(icand)=.true.
        enddo  ! icand
     endif
     call sec0(1,tsec0)

  enddo  ! nqd

!  Trim the list and produce a sorted index and sizes of groups.
!  (Should trimlist remove all but best SNR for given UTC and message content?)
700 call trimlist(sig,km,ftol,indx,nsiz,nz)
  done(1:km)=.false.
  j=0
  ilatest=-1
  do n=1,nz
     ifile0=0
     do m=1,nsiz(n)
        i=indx(j+m)
        ifile=sig(i,1)
        if(ifile.gt.ifile0 .and.msg(i).ne.blank) then
           ilatest=i
           ifile0=ifile
        endif
     enddo
     i=ilatest

     if(i.ge.1) then
        if(.not.done(i)) then
           done(i)=.true.
           nutc=sig(i,2)
           freq=sig(i,3)
           sync1=sig(i,4)
           dt=sig(i,5)
           npol=nint(57.2957795*sig(i,6))
           flip=sig(i,7)
           sync2=sig(i,8)
           nkv=sig(i,9)
           nqual=min(sig(i,10),10.0)
!                  rms0=sig(i,11)
           do k=1,5
              a(k)=sig(i,12+k)
           enddo
           nhist=sig(i,18)
           decoded=msg(i)
           
           if(flip.lt.0.0) then
              do i=22,1,-1
                 if(decoded(i:i).ne.' ') go to 10
              enddo
              stop 'Error in message format'
10            if(i.le.18) decoded(i+2:i+4)='OOO'
           endif
           mhz=fcenter                             !... +fadd ???
           nkHz=nint(freq-foffset)-nfshift
           f0=mhz+0.001*nkHz
           ndf=nint(1000.0*(freq-foffset-(nkHz+nfshift)))
           ndf0=nint(a(1))
           ndf1=nint(a(2))
           ndf2=nint(a(3))
           nsync1=sync1

           s2db=10.0*log10(sync2) - 40             !### empirical ###
           nsync2=nint(s2db)
           if(decoded(1:4).eq.'RO  ' .or. decoded(1:4).eq.'RRR  ' .or.  &
                decoded(1:4).eq.'73  ') then
              nsync2=nint(1.33*s2db + 2.0)
           endif

           if(nxant.ne.0) then
              npol=npol-45
              if(npol.lt.0) npol=npol+180
           endif

           call txpol(xpol,decoded,mygrid,npol,nxant,ntxpol,cp)

           cmode='#A'
           if(mode65.eq.2) cmode='#B'
           if(mode65.eq.4) cmode='#C'
           write(26,1014) f0,ndf,ndf0,ndf1,ndf2,dt,npol,nsync1,       &
                nsync2,nutc,decoded,cp,cmode
1014       format(f8.3,i5,3i3,f5.1,i4,i3,i4,i5.4,4x,a22,2x,a1,3x,a2)
           ndecodes=ndecodes+1
           write(21,1100) f0,ndf,dt,npol,nsync2,nutc,decoded,cp,          &
                cmode(1:1),cmode(2:2)
1100       format(f8.3,i5,f5.1,2i4,i5.4,2x,a22,2x,a1,3x,a1,1x,a1)
        endif

     endif
     j=j+nsiz(n)
  enddo  !i=1,km

  write(26,1015) nutc
1015 format(37x,i6.4,' ')
  call flush(21)
  call flush(26)
  call display(nkeep,ftol)
  ndecdone=2

900 close(23)
  ndphi=0
  mcall3b=mcall3a

  return
end subroutine map65a
