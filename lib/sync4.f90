subroutine sync4(dat,jz,ntol,nfqso,mode,mode4,minwidth,dtx,dfx,snrx,    &
     snrsync,flip,width)

! Synchronizes JT4 data, finding the best-fit DT and DF.  

  parameter (NFFTMAX=2520)         !Max length of FFTs
  parameter (NHMAX=NFFTMAX/2)      !Max length of power spectra
  parameter (NSMAX=525)            !Max number of half-symbol steps
  integer ntol                     !Range of DF search
  real dat(jz)
  real s2(NHMAX,NSMAX)             !2d spectrum, stepped by half-symbols
  real ccfblue(-5:540)             !CCF with pseudorandom sequence
  real ccfred(NHMAX)               !Peak of ccfblue, as function of freq
  real red(NHMAX)                  !Peak of ccfblue, as function of freq
  integer ipk1(1)
  integer nch(7)
  logical savered
  equivalence (ipk1,ipk1a)
  data nch/1,2,4,9,18,36,72/
  save

! Do FFTs of twice symbol length, stepped by half symbols.  Note that 
! we have already downsampled the data by factor of 2.
  nsym=207
  nfft=2520
  nh=nfft/2
  nq=nfft/4
  nsteps=jz/nq - 1
  df=0.5*11025.0/nfft
  ftop=nfqso + 7*mode4*df
  if(ftop.gt.11025.0/4.0) then
     print*,'*** Rx Freq is set too high for this submode ***'
     go to 900
  endif

  if(mode.eq.-999) width=0.                        !Silence compiler warning

  do j=1,nsteps                     !Compute spectrum for each step, get average
     k=(j-1)*nq + 1
     call ps4(dat(k),nfft,s2(1,j))
  enddo

! Set freq and lag ranges
  ia=(nfqso-ntol)/df              !Index of lowest tone, bottom of search range
  ib=(nfqso+ntol)/df              !Index of lowest tone, top of search range
  iamin=nint(100.0/df)
  if(ia.lt.iamin) ia=iamin
  ibmax=nint(2700.0/df) - 6*mode4
  if(ib.gt.ibmax) ib=ibmax

  lag1=-5
  lag2=59
  syncbest=-1.e30
  snrx=-26.0
  ccfred=0.
  red=0.
  i0=nint(nfqso/df)

  do ich=minwidth,7                       !Find best width
     kz=nch(ich)/2
     savered=.false.
     iaa=ia+kz
     ibb=ib-kz
     do i=iaa,ibb                       !Find best frequency channel for CCF
        call xcor4(s2,i,nsteps,nsym,lag1,lag2,ich,mode4,ccfblue,ccf0,   &
             lagpk0,flip)
        ccfred(i)=ccf0
        
! Find rms of the CCF, without main peak
        call slope(ccfblue(lag1),lag2-lag1+1,lagpk0-lag1+1.0)
        sync=abs(ccfblue(lagpk0))
!        write(*,3000) ich,i,i*df,ccf0,sync,syncbest
!3000    format(2i5,4f12.3)

! Find best sync value
        if(sync.gt.syncbest*1.03) then
           ipk=i
           lagpk=lagpk0
           ichpk=ich
           syncbest=sync
           savered=.true.
        endif
     enddo
     if(savered) red=ccfred
  enddo
  if(syncbest.lt.-1.e29) go to 900
  ccfred=red
  call pctile(ccfred(ia:ib),ib-ia+1,45,base)
  ccfred=ccfred-base
  
  dfx=ipk*df

! Peak up in time, at best whole-channel frequency
  call xcor4(s2,ipk,nsteps,nsym,lag1,lag2,ichpk,mode4,ccfblue,ccfmax,   &
       lagpk,flip)
  xlag=lagpk
  if(lagpk.gt.lag1 .and. lagpk.lt.lag2) then
     call peakup(ccfblue(lagpk-1),ccfmax,ccfblue(lagpk+1),dx2)
     xlag=lagpk+dx2
  endif

! Find rms of the CCF, without the main peak
  call slope(ccfblue(lag1),lag2-lag1+1,xlag-lag1+1.0)
  sq=0.
  nsq=0
  do lag=lag1,lag2
     if(abs(lag-xlag).gt.2.0) then
        sq=sq+ccfblue(lag)**2
        nsq=nsq+1
     endif
  enddo
  rms=sqrt(sq/nsq)
  snrsync=max(0.0,db(abs(ccfblue(lagpk)/rms - 1.0)) - 4.5)
  dt=2.0/11025.0
  istart=xlag*nq
  dtx=istart*dt

  ipk1=maxloc(ccfred)
  ccf10=0.5*maxval(ccfred)
  do i=ipk1a,ia,-1
     if(ccfred(i).le.ccf10) exit
  enddo
  i1=i
  do i=ipk1a,ib
     if(ccfred(i).le.ccf10) exit
  enddo
  nw=i-i1
  width=nw*df

  sq=0.
  ns=0
  iaa=max(ipk1a-10*nw,ia)
  ibb=min(ipk1a+10*nw,ib)
  jmax=2*mode4/3
  do i=iaa,ibb
     j=abs(i-ipk1a)
     if(j.gt.nw .and. j.lt.jmax) then
        sq=sq + ccfred(j)*ccfred(j)
        ns=ns+1
     endif
  enddo
  rms=sqrt(sq/ns)
  snrx=10.0*log10(ccfred(ipk1a)/rms) - 41.2

900  return
end subroutine sync4
