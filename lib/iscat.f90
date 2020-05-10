subroutine iscat(cdat0,npts0,nh,npct,t2,pick,cfile6,minsync,ntol,   &
     mousebutton,mode4,nafc,nmore,psavg,maxlines,nlines,line)

! Decode an ISCAT signal

  parameter (NMAX=30*3101)
  parameter (NSZ=4*1400)
  character cfile6*6                      !File time
  character c42*42
  character msg*29,msg1*29,msgbig*29
  character*80 line(100)
  character csync*1
  complex cdat0(NMAX)
  complex cdat(NMAX)
  real s0(288,NSZ)
  real fs1(0:41,30)
  real psavg(72)                          !Average spectrum of whole file
  integer nsum(30)
  integer ntol
  integer icos(4)
  logical pick,last
  data icos/0,1,3,2/
  data nsync/4/,nlen/2/,ndat/18/
  data c42/'0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ /.?@-'/
  save cdat,s0

  nlines = 0
  fsample=3100.78125                   !Sample rate after 9/32 downsampling
  nsps=144/mode4

  bigworst=-1.e30                      !Silence compiler warnings ...
  bigxsync=0.
  bigsig=-1.e30
  msglenbig=0
  ndf0big=0
  nfdotbig=0
  bigt2=0.
  bigavg=0.
  bigtana=0.
  if(nmore.eq.-999) bigsig=-1         !... to here

  last=.false.
  do inf=1,6                           !Loop over data-segment sizes
     nframes=2**inf
     if(nframes*24*nsps.gt.npts0) then
        nframes=npts0/(24*nsps)
        last=.true.
     endif
     npts=nframes*24*nsps

     do ia=1,npts0-npts,nsps*24        !Loop over start times stepped by 1 frame
        ib=ia+npts-1
        cdat(1:npts)=cdat0(ia:ib)
        t3=(ia + 0.5*npts)/fsample + 0.9 
        if(pick) t3=t2+t3

! Compute symbol spectra and establish sync:
        call synciscat(cdat,npts,nh,npct,s0,jsym,df,ntol,           &
             mousebutton,mode4,nafc,psavg,xsync,sig,ndf0,msglen,    &
             ipk,jpk,idf,df1)
        nfdot=nint(idf*df1)

        isync=xsync
        if(msglen.eq.0 .or. isync.lt.max(minsync,0)) then
           msglen=0
           worst=1.
           avg=1.
           ndf0=0
           cycle
        endif

        ipk3=0                                  !Silence compiler warning
        nblk=nsync+nlen+ndat
        fs1=0.
        nsum=0
        nfold=jsym/96
        jb=96*nfold
        k=0
        n=0
        do j=jpk,jsym,4                !Fold information symbols into fs1
           k=k+1
           km=mod(k-1,nblk)+1
           if(km.gt.6) then
              n=n+1
              m=mod(n-1,msglen)+1
              ii=nint(idf*float(j-jb/2)/float(jb))
              do i=0,41
                 iii=ii+ipk+2*i
                 if(iii.ge.1 .and. iii.le.288) fs1(i,m)=fs1(i,m) + s0(iii,j)
              enddo
              nsum(m)=nsum(m)+1
           endif
        enddo

        do m=1,msglen
           fs1(0:41,m)=fs1(0:41,m)/nsum(m)
        enddo

! Read out the message contents:
        msg= '                             '
        msg1='                             '
        mpk=0
        worst=9999.
        sum=0.
        do m=1,msglen
           smax=0.
           smax2=0.
           do i=0,41
              if(fs1(i,m).gt.smax) then
                 smax=fs1(i,m)
                 ipk3=i
              endif
           enddo
           do i=0,41
              if(fs1(i,m).gt.smax2 .and. i.ne.ipk3) smax2=fs1(i,m)
           enddo
           rr=0.
           if(smax2.gt.0.0) rr=smax/smax2
           sum=sum + rr
           if(rr.lt.worst) worst=rr
           if(ipk3.eq.40) mpk=m
           msg1(m:m)=c42(ipk3+1:ipk3+1)
        enddo

        avg=sum/msglen
        if(mpk.eq.1) then
           msg=msg1(2:)
        else if(mpk.lt.msglen) then
           msg=msg1(mpk+1:msglen)//msg1(1:mpk-1)
        else
           msg=msg1(1:msglen-1)
        endif

        ttot=npts/3100.78125

        if(worst.gt.bigworst) then
           bigworst=worst
           bigavg=avg
           bigxsync=xsync
           bigsig=sig
           ndf0big=ndf0
           nfdotbig=nfdot
           msgbig=msg
           msglenbig=msglen
           bigt2=t3
           bigtana=nframes*24*nsps/fsample
        endif

        isync = xsync
        if(avg.gt.2.5 .and. xsync.ge.max(float(minsync),1.5) .and. &
             maxlines.ge.2) then
           nsig=nint(sig)
           nworst=10.0*(worst-1.0)
           navg=10.0*(avg-1.0)
           if(nworst.gt.10) nworst=10
           if(navg.gt.10) navg=10
           tana=nframes*24*nsps/fsample
           csync=' '
           if(isync.ge.1) csync='*'
           if(nlines.le.maxlines-1) nlines = nlines + 1
           write(line(nlines),1020) cfile6,isync,nsig,t2,ndf0,nfdot,csync, &
                msg(1:28),msglen,navg,nworst,tana,char(0)
        endif
     enddo
     if(last) exit
  enddo
  
  worst=bigworst
  avg=bigavg
  xsync=bigxsync
  sig=bigsig
  ndf0=ndf0big
  nfdot=nfdotbig
  msg=msgbig
  msglen=msglenbig
  t2=bigt2
  tana=bigtana

  isync=xsync
  nworst=10.0*(worst-1.0)
  navg=10.0*(avg-1.0)
  if(nworst.gt.10) nworst=10
  if(navg.gt.10) navg=10

  if(navg.le.0 .or. isync.lt.max(minsync,0)) then
     msg='                             '
     nworst=0
     navg=0
     ndf0=0
     nfdot=0
     sig=-20
     msglen=0
     tana=0.
     t2=0.
  endif
  csync=' '
  if(isync.ge.1) csync='*'
  nsig=nint(sig)
  
  if(nlines.le.maxlines-1) nlines = nlines + 1
  write(line(nlines),1020) cfile6,isync,nsig,t2,ndf0,nfdot,csync,msg(1:28),  &
       msglen,navg,nworst,tana,char(0)
1020 format(a6,2i4,f5.1,i5,i4,1x,a1,2x,a28,i4,i3,2x,i1,f5.1,a1)

  return
end subroutine iscat
