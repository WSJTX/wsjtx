module q65

  parameter (MAXAVE=64)
  parameter (PLOG_MIN=-240.0)            !List decoding threshold
  integer nsave,nlist,LL0
  integer iutc(MAXAVE)
  integer iseq(MAXAVE)
  integer listutc(10)
  integer apsym0(58),aph10(10)
  integer apmask(13),apsymbols(13)
  integer navg,ibwa,ibwb
  real    f0save(MAXAVE)
  real    xdtsave(MAXAVE)
  real    snr1save(MAXAVE)
  real,allocatable :: s3save(:,:,:)
  real,allocatable :: s3avg(:,:)

contains

subroutine q65_avg(nutc,ntrperiod,LL,nfqso,ntol,lclearave,xdt,f0,snr1,s3)

! Accumulate Q65 spectra s3(LL,63) and associated parameters for
! message averaging.

  character*6 cutc
  real s3(-64:LL-65,63)                  !Symbol spectra
  logical first,lclearave
  data first/.true./
  save

  if(LL.ne.LL0) then
     LL0=LL
     if(allocated(s3save)) deallocate(s3save)
     if(allocated(s3avg)) deallocate(s3avg)
     allocate(s3save(-64:LL-65,63,MAXAVE))
     allocate(s3avg(-64:LL-65,63))
     s3save=0.
     s3avg=0.
  endif
  if(first .or. lclearave) then
     iutc=-1
     iseq=-1
     f0save=0.0
     nsave=0
     first=.false.
     s3save=0.
     s3avg=0.
  endif

  do i=1,MAXAVE       !Don't save info more than once for same UTC and freq
     if(nutc.eq.iutc(i) .and. abs(nfqso-f0save(i)).le.ntol) go to 900
  enddo

! Save data for message averaging
  nsave=nsave+1
  if(nsave.gt.MAXAVE) nsave=nsave-MAXAVE
  n=nutc
  if(ntrperiod.ge.60) n=100*n
  write(cutc,'(i6.6)') n
  read(cutc,'(3i2)') ih,im,is
  nsec=3600*ih + 60*im + is
  iseq(nsave)=mod(nsec/ntrperiod,2)         !T/R sequence: 0 (even) or 1 (odd)
  iutc(nsave)=nutc                          !UTC, hhmm or hhmmss
  snr1save(nsave)=snr1                      !SNR from sync
  xdtsave(nsave)=xdt                        !DT
  f0save(nsave)=f0                          !f0
  s3save(:,:,nsave)=s3(:,:)                 !Symbol spectra

900 return
end subroutine q65_avg

subroutine q65_avg2(ntrperiod,baud,nsubmode,nQSOprogress,lapcqonly, &
       codewords,ncw,xdt,f0,snr2,dat4,idec)

  use packjt77
  use timer_module, only: timer
  character*78 c78
  character*1 cused(MAXAVE)
  character*37 avemsg
  integer dat4(13)
  integer codewords(63,206)
  integer apmask1(78),apsymbols1(78)
  logical lapcqonly
  
  mode_q65=2**nsubmode
  dtdiff=0.038
  if(ntrperiod.eq.30) then
     dtdiff=0.08
  else if(ntrperiod.eq.60) then
     dtdiff=0.16
  else if(ntrperiod.eq.120) then
     dtdiff=0.4
  else if(ntrperiod.eq.300) then
     dtdiff=0.9
  endif
  dtdiff=3.5*dtdiff
  f0diff=2.5*baud*mode_q65
  navg=0
  s3avg=0.

! Find previously saved spectra that should be averaged with this one
  do i=1,MAXAVE
     cused(i)='.'                                   !Flag for "not used"
     if(iutc(i).lt.0) cycle
     if(iseq(i).ne.iseq(nsave)) cycle               !Sequence must match
     if(abs(xdt-xdtsave(i)).gt.dtdiff) cycle        !DT must be close
     if(abs(f0-f0save(i)).gt.f0diff) cycle          !Freq must match
     cused(i)='$'                                   !Flag for "use this one"
     s3avg=s3avg + s3save(:,:,i)                    !Add this spectrum
     navg=navg+1
  enddo

! Write parameters for display to User in the Message Averaging (F7) window.
  do i=1,MAXAVE
     if(iutc(i).lt.0) cycle
     if(ntrperiod.le.30) write(14,1000) cused(i),iutc(i),snr1save(i),    &
          xdtsave(i),f0save(i)
1000 format(a1,i7.6,f6.1,f6.2,f7.1)
     if(ntrperiod.ge.60) write(14,1001) cused(i),iutc(i),snr1save(i),    &
          xdtsave(i),f0save(i)
1001 format(a1,i5.4,f6.1,f6.2,f7.1)
  enddo
  if(navg.lt.2) go to 100

  s3avg=s3avg/navg
  do ibw=ibwa,ibwb
     b90=1.72**ibw
     b90ts=b90/baud
     call timer('dec1avg ',0)
     call q65_dec1(s3avg,nsubmode,b90ts,codewords,ncw,esnodb,irc,dat4,avemsg)
     call timer('dec1avg ',1)
     if(irc.ge.0) then
        snr2=esnodb - 0.5*db(2500.0/baud) + 3.0     !Empirical adjustment
        snr2=snr2 - db(float(navg))                 !Is this right?
        idec=100+navg
        go to 100
     endif
  enddo

! Loop over full range of available AP
!  APmask=0
!  APsymbols=0
  npasses=2
  if(nQSOprogress.eq.5) npasses=3
  if(lapcqonly) npasses=1
  iaptype=0
  ncontest=0   !### ??? ###
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
     endif

     do ibw=ibwa,ibwb
        b90=1.72**ibw
        b90ts=b90/baud
        call timer('dec2avg ',0)
        call q65_dec2(s3avg,nsubmode,b90ts,esnodb,irc,dat4,avemsg)
        call timer('dec2avg ',1)
        if(irc.ge.0) then
           snr2=esnodb - db(2500.0/baud) + 3.0     !Empirical adjustment
           snr2=snr2 - 0.5*db(float(navg))             !Is this right?
           idec=100*(iaptype+2) + navg
           go to 100
        endif
     enddo  ! ibw
  enddo  ! ipass

100 return
end subroutine q65_avg2

subroutine q65_clravg

  iutc=-1
  iseq=-1
  snr1save=0.
  xdtsave=0.
  f0save=0.0
  nsave=0
  if(allocated(s3save)) s3save=0.
  if(allocated(s3avg)) s3avg=0.
  
  return
end subroutine q65_clravg

end module q65
