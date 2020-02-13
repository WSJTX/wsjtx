subroutine msk144spd(cbig,n,ntol,nsuccess,msgreceived,fc,fret,tret,navg,ct,   &
  softbits)

! MSK144 short-ping-decoder

  use packjt77
  use timer_module, only: timer

  parameter (NSPM=864, MAXSTEPS=100, NFFT=NSPM, MAXCAND=5, NPATTERNS=6)
  character*37 msgreceived
  complex cbig(n)
  complex cdat(3*NSPM)                    !Analytic signal
  complex c(NSPM)
  complex ct(NSPM)
  complex ctmp(NFFT)                  
  integer, dimension(1) :: iloc
  integer indices(MAXSTEPS)
  integer npkloc(10)
  integer navpatterns(3,NPATTERNS)
  integer navmask(3)
  integer nstart(MAXCAND)
  logical ismask(NFFT)
  real detmet(-2:MAXSTEPS+3)
  real detmet2(-2:MAXSTEPS+3)
  real detfer(MAXSTEPS)
  real rcw(12)
  real ferrs(MAXCAND)
  real snrs(MAXCAND)
  real softbits(144)
  real tonespec(NFFT)
  real tpat(NPATTERNS)
  real*8 dt, df, fs, pi, twopi
  logical first
  logical doosd
  data first/.true./
  data navpatterns/ &
       0,1,0, &
       1,0,0, &
       0,0,1, &
       1,1,0, &
       0,1,1, &
       1,1,1/
  data tpat/1.5,0.5,2.5,1.0,2.0,1.5/

  save df,first,fs,pi,twopi,dt,tframe,rcw

  if( ntol .le. 50 ) doosd = .true.
  if(first) then
     nmatchedfilter=1
! define half-sine pulse and raised-cosine edge window
     pi=4d0*datan(1d0)
     twopi=8d0*datan(1d0)
     fs=12000.0
     dt=1.0/fs
     df=fs/NFFT
     tframe=NSPM/fs

     do i=1,12
       angle=(i-1)*pi/12.0
       rcw(i)=(1-cos(angle))/2
     enddo

     first=.false.
  endif

  ! fill the detmet, detferr arrays
  nstep=(n-NSPM)/216  ! 72ms/4=18ms steps
  detmet=0
  detmet2=0
  detfer=-999.99
  nfhi=2*(fc+500)
  nflo=2*(fc-500)
  ihlo=nint((nfhi-2*ntol)/df) + 1
  ihhi=nint((nfhi+2*ntol)/df) + 1
  illo=nint((nflo-2*ntol)/df) + 1
  ilhi=nint((nflo+2*ntol)/df) + 1
  i2000=nint(nflo/df) + 1
  i4000=nint(nfhi/df) + 1
  do istp=1,nstep
    ns=1+216*(istp-1)
    ne=ns+NSPM-1
    if( ne .gt. n ) exit
    ctmp=cmplx(0.0,0.0)
    ctmp(1:NSPM)=cbig(ns:ne)

! Coarse carrier frequency sync - seek tones at 2000 Hz and 4000 Hz in 
! squared signal spectrum.

    ctmp=ctmp**2
    ctmp(1:12)=ctmp(1:12)*rcw
    ctmp(NSPM-11:NSPM)=ctmp(NSPM-11:NSPM)*rcw(12:1:-1)
    call four2a(ctmp,NFFT,1,-1,1)
    tonespec=abs(ctmp)**2

    ismask=.false.
    ismask(ihlo:ihhi)=.true.  ! high tone search window
    iloc=maxloc(tonespec,ismask)
    ihpk=iloc(1)
    deltah=-real( (ctmp(ihpk-1)-ctmp(ihpk+1)) / (2*ctmp(ihpk)-ctmp(ihpk-1)-ctmp(ihpk+1)) )
    ah=tonespec(ihpk)
    ahavp=(sum(tonespec,ismask)-ah)/count(ismask)
    trath=ah/(ahavp+0.01)
    ismask=.false.
    ismask(illo:ilhi)=.true.   ! window for low tone
    iloc=maxloc(tonespec,ismask)
    ilpk=iloc(1)
    deltal=-real( (ctmp(ilpk-1)-ctmp(ilpk+1)) / (2*ctmp(ilpk)-ctmp(ilpk-1)-ctmp(ilpk+1)) )
    al=tonespec(ilpk)
    alavp=(sum(tonespec,ismask)-al)/count(ismask)
    tratl=al/(alavp+0.01)
    fdiff=(ihpk+deltah-ilpk-deltal)*df
    ferrh=(ihpk+deltah-i4000)*df/2.0
    ferrl=(ilpk+deltal-i2000)*df/2.0
    if( ah .ge. al ) then
      ferr=ferrh
    else
      ferr=ferrl
    endif
    detmet(istp)=max(ah,al)
    detmet2(istp)=max(trath,tratl)
    detfer(istp)=ferr
  enddo  ! end of detection-metric and frequency error estimation loop

  call indexx(detmet(1:nstep),nstep,indices) !find median of detection metric vector
  xmed=detmet(indices(nstep/4))
  detmet=detmet/xmed ! noise floor of detection metric is 1.0
  ndet=0

  do ip=1,MAXCAND ! Find candidates
    iloc=maxloc(detmet(1:nstep))
    il=iloc(1)
    if( (detmet(il) .lt. 3.0) ) exit 
    if( abs(detfer(il)) .le. ntol ) then 
      ndet=ndet+1
      nstart(ndet)=1+(il-1)*216+1
      ferrs(ndet)=detfer(il)
      snrs(ndet)=12.0*log10(detmet(il))/2-9.0
    endif
    detmet(il)=0.0
  enddo

  if( ndet .lt. 3 ) then  
    do ip=1,MAXCAND-ndet ! Find candidates
      iloc=maxloc(detmet2(1:nstep))
      il=iloc(1)
      if( (detmet2(il) .lt. 12.0) ) exit 
      if( abs(detfer(il)) .le. ntol ) then 
        ndet=ndet+1
        nstart(ndet)=1+(il-1)*216+1
        ferrs(ndet)=detfer(il)
        snrs(ndet)=12.0*log10(detmet2(il))/2-9.0
      endif
      detmet2(il)=0.0
    enddo
  endif

  nsuccess=0
  msgreceived=' '
  npeaks=2
  ntol0=8
  deltaf=2.0
  do icand=1,ndet  ! Try to sync/demod/decode each candidate.
    ib=max(1,nstart(icand)-NSPM)
    ie=ib-1+3*NSPM
    if( ie .gt. n ) then
      ie=n
      ib=ie-3*NSPM+1
    endif
    cdat=cbig(ib:ie) 
    fo=fc+ferrs(icand)
    do iav=1,NPATTERNS
      navmask=navpatterns(1:3,iav) 
      call msk144sync(cdat,3,ntol0,deltaf,navmask,npeaks,fo,fest,npkloc,   &
           nsyncsuccess,xmax,c)

      if( nsyncsuccess .eq. 0 ) cycle

      do ipk=1,npeaks
        do is=1,3
          ic0=npkloc(ipk)
          if( is.eq.2) ic0=max(1,ic0-1)
          if( is.eq.3) ic0=min(NSPM,ic0+1)
          ct=cshift(c,ic0-1)
          call msk144decodeframe(ct,softbits,msgreceived,ndecodesuccess,doosd)
          if( ndecodesuccess .gt. 0 ) then
            tret=(nstart(icand)+NSPM/2)/fs
            fret=fest
            navg=sum(navmask)
            nsuccess=1
            return
          endif 
        enddo
      enddo
    enddo
  enddo       ! candidate loop

  return
end subroutine msk144spd
