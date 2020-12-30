subroutine q65_avg(nutc,ntrperiod,LL,ntol,lclearave,xdt,f0,snr1,s3)

! Accumulate Q65 spectra s3(LL,63) and associated parameters for
! message averaging.

  use q65
  use packjt77
  character*78 c78
  character*37 avemsg
  character*1 cused(MAXAVE)
  character*6 cutc
  real s3(-64:LL-65,63)                  !Symbol spectra
  integer iused(MAXAVE)
  integer dat4(13)
  integer codewords(63,206)
  integer apmask1(78),apsymbols1(78)
  logical first,lclearave,lapcqonly
  data first/.true./
  save

  if(first .or. LL.ne.LL0 .or.lclearave) then
     iutc=-1
     iseq=-1
     f0save=0.0
     dtdiff=0.2
     nsave=0
     LL0=LL
     first=.false.
     if(allocated(s3save)) deallocate(s3save)
     if(allocated(s3avg)) deallocate(s3avg)
     allocate(s3save(-64:LL-65,63,MAXAVE))
     allocate(s3avg(-64:LL-65,63))
     s3save=0.
     s3avg=0.
  endif

  if(ntrperiod.eq.15) then
     dtdiff=0.038
  else if(ntrperiod.eq.30) then
     dtdiff=0.08
  else if(ntrperiod.eq.60) then
     dtdiff=0.16
  else if(ntrperiod.eq.120) then
     dtdiff=0.4
  else if(ntrperiod.eq.300) then
     dtdiff=0.9
  endif

  do i=1,MAXAVE       !Don't save info more than once for same UTC and freq
     if(nutc.eq.iutc(i) .and. abs(nfreq-f0save(i)).le.ntol) go to 10
  enddo

! Save data for message averaging
  nsave=nsave+1
  nsave=mod(nsave-1,MAXAVE)+1
  n=nutc
  if(ntrperiod.ge.60) n=100*n
  write(cutc,'(i6.6)') n
  read(cutc,'(3i2)') ih,im,is
  nsec=3600*ih + 60*im + is
  iseq(nsave)=mod(nsec/ntrperiod,2)         !T/R sequence: 0 (even) or 1 (odd)
  iutc(nsave)=nutc                          !UTC, hhmm or hhmmss
  snr1save(nsave)=snr1                      !SNR from sync
  xdtsave(nsave)=xdt                        !DT
  f0save(nsave)=f0                           !f0
  s3save(:,:,nsave)=s3(:,:)                 !Symbol spectra

10 return

  entry q65_avg2(ntrperiod,ntol,baud,nsubmode,nQSOprogress,lapcqonly, &
       ibwa,ibwb,codewords,ncw,xdt,f0,snr1,snr2,dat4,idec)

  mode_q65=2**nsubmode
  ibwa=1.8*log(baud*mode_q65) + 2
  ibwb=min(10,ibwa+4)
  snr1sum=0.
  xdtsum=0.
  fsum=0.
  navg=0
  s3avg=0.

! Find previously saved spectra that should be averaged with this one
  do i=1,MAXAVE
     cused(i)='.'                                   !Flag for "not used"
     if(iutc(i).lt.0) cycle
     if(iseq(i).ne.iseq(nsave)) cycle               !Sequence must match
     if(abs(xdt-xdtsave(i)).gt.dtdiff) cycle        !DT must be close
     if(abs(f0-f0save(i)).gt.float(ntol)) cycle   !Freq must match
     cused(i)='$'                                   !Flag for "use this one"
     s3avg=s3avg + s3save(:,:,i)                    !Add this spectrum
     snr1sum=snr1sum + snr1save(i)
     xdtsum=xdtsum + xdtsave(i)
     fsum=fsum + f0save(i)
     navg=navg+1
     iused(navg)=i
  enddo
  if(navg.lt.MAXAVE) iused(navg+1)=0
  if(navg.lt.2) go to 900
  
! Find averages of snr1, xdt, and f0 used in this decoding attempt.
  snr1ave=0.
  xdtave=0.
  f0ave=0.
  if(navg.gt.0) then
     snr1ave=snr1sum/navg
     xdtave=xdtsum/navg
     f0ave=fsum/navg
  endif

! Write parameters for display to User in the Message Averaging (F7) window.
  do i=1,nsave
     if(ntrperiod.le.30) write(14,1000) cused(i),iutc(i),snr1save(i),    &
          xdtsave(i),f0save(i)
1000 format(a1,i7.6,f6.1,f6.2,f7.1)
     if(ntrperiod.ge.60) write(14,1001) cused(i),iutc(i),snr1save(i),    &
          xdtsave(i),f0save(i)
1001 format(a1,i5.4,f6.1,f6.2,f7.1)
  enddo

  s3avg=s3avg/navg
  nFadingModel=1
  do ibw=ibwa,ibwb
     b90=1.72**ibw
     b90ts=b90/baud
     call q65_dec1(s3avg,nsubmode,b90ts,codewords,ncw,esnodb,irc,dat4,avemsg)
!     irc=-99  !### TEMPORARY ###
     if(irc.ge.0 .and. plog.ge.PLOG_MIN) then
        snr2=esnodb - 0.5*db(2500.0/baud) + 3.0     !Empirical adjustment
        snr2=snr2 - db(float(navg))             !Is this right?
        idec=100+navg
!        print*,'C dec1 ',ibw,irc,idec,avemsg
        go to 900
     endif
  enddo

! Loop over full range of available AP
!  APmask=0
!  APsymbols=0
  npasses=2
  if(nQSOprogress.eq.5) npasses=3
  if(lapcqonly) npasses=1
  iaptype=0
  do ipass=0,npasses
     apmask=0                         !Try first with no AP information
     apsymbols=0

     if(ipass.ge.1) then
        ! Subsequent passes use AP information appropiate for nQSOprogress
        call q65_ap(nQSOprogress,ipass,ncontest,lapcqonly,iaptype,   &
             apsym0,apmask1,apsymbols1)
        write(c78,1050) apmask1
1050    format(78i1)
        read(c78,1060) apmask
1060    format(13b6.6)
        write(c78,1050) apsymbols1
        read(c78,1060) apsymbols
        if(iaptype.eq.4) then
           do j=1,3
              ng15=32401+j
              write(c78(60:74),'(b15.15)') ng15
              read(c78,1060) dgen
              call q65_enc(dgen,codewords(1,j))
           enddo
        endif
     endif

     do ibw=ibwa,ibwb
        b90=1.72**ibw
        b90ts=b90/baud
        call q65_dec2(s3avg,nsubmode,b90ts,esnodb,irc,dat4,avemsg)
        if(irc.ge.0) then
           snr2=esnodb - db(2500.0/baud) + 3.0     !Empirical adjustment
           snr2=snr2 - 0.5*db(float(navg))             !Is this right?
           idec=100*(iaptype+2) + navg
!        print*,'D dec2 ',ibw,dec,snr2,avemsg
           go to 900
        endif
     enddo  ! ibw
  enddo  ! ipass

900  return
end subroutine q65_avg
