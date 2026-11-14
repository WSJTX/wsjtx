! What refspectrum's last call did with fname: wrote it while measuring,
! loaded it when apply began, or could not.
module refspectrum_file
  implicit none
  integer, parameter :: REFSPEC_NONE=0, REFSPEC_WRITTEN=1, REFSPEC_LOADED=2, &
       REFSPEC_WRITE_FAILED=3, REFSPEC_READ_FAILED=4
  integer :: refspec_status=REFSPEC_NONE
end module refspectrum_file

subroutine refspectrum(id2,ninput,bclear,brefspec,buseref,fname)

! Input:
!  id2       i*2        Raw 16-bit integer data, 12000 Hz sample rate
!  ninput              Valid input count, 1..3456; zero resets stream history
!  brefspec  logical    True when accumulating a reference spectrum

  use refspectrum_file
  parameter (NFFT=6912,NH=NFFT/2,NPOLYLOW=400,NPOLYHIGH=2600)
  integer*2 id2(NFFT)
  integer*2 measurement(NH)
  integer ninput,nmeasurement,offset,ncopy
  logical*1 bclear,brefspec,buseref,blastuse
  
  real xs(0:NH-1)                         !Saved upper half of input chunk convolved with h(t) 
  real x(0:NFFT-1)                        !Work array
  real*4 w(0:NFFT-1)                      !Window function
  real*4 s(0:NH)                          !Average spectrum
  real*4 fil(0:NH)
  real*8 xfit(1500),yfit(1500),sigmay(1500),a(5),chisqr !Polyfit arrays
  logical first
  complex cx(0:NH)                        !Complex frequency-domain work array
  complex cfil(0:NH)
  character*(*) fname
  common/spectra/syellow(6827),ref(0:NH),filter(0:NH)
  equivalence(x,cx)
  data first/.true./,blastuse/.false./
  save

  refspec_status=REFSPEC_NONE
  if(first) then
     pi=4.0*atan(1.0)
     do i=0,NFFT-1
        ww=sin(i*pi/NFFT)
        w(i)=ww*ww/NFFT
     enddo
     nsave=0
     nmeasurement=0
     s=0.0
     filter=1.0
     xs=0.
     first=.false.
  endif
  if(bclear) then
     s=0.
     fil=1.0
     ref=0.0
     filter=0.0
     cfil=0.0
     xs=0.0
     nsave=0
     nmeasurement=0
     blastuse=.false.
  endif

  if(ninput.lt.0.or.ninput.gt.NH) return
  if(ninput.eq.0) then
     xs=0.0
     nmeasurement=0
     if(.not.buseref) blastuse=.false.
     return
  endif
  if(brefspec) then
     offset=0
     do while(offset.lt.ninput)
     ncopy=min(NH-nmeasurement,ninput-offset)
     measurement(nmeasurement+1:nmeasurement+ncopy)=id2(offset+1:offset+ncopy)
     offset=offset+ncopy
     nmeasurement=nmeasurement+ncopy
     if(nmeasurement.lt.NH) cycle
     nmeasurement=0
     x(0:NH-1)=0.001*measurement
     x(NH:NFFT-1)=0.0
     call four2a(cx,NFFT,1,-1,0)                 !r2c FFT

     do i=1,NH
        s(i)=s(i) + real(cx(i))**2 + aimag(cx(i))**2
     enddo
     nsave=nsave+1

     fac0=0.9
     if(mod(nsave,4).eq.0) then
        df=12000.0/NFFT
        ia=nint(1000.0/df)
        ib=nint(2000.0/df)
        avemid=sum(s(ia:ib))/(ib-ia+1)
        do i=0,NH
           fil(i)=0.
           if(s(i).gt.0.0) then
              fil(i)=sqrt(avemid/s(i))
           endif
        enddo

! Default range is 240 - 4000 Hz.  For narrower filters, use frequencies
! at which gain is -20 dB relative to 1500 Hz.
        ia=nint(240.0/df)
        ib=nint(4000.0/df)
        i0=nint(1500.0/df)
        do i=i0,ia,-1
           if(s(i)/s(i0).lt.0.01) exit
        enddo
        ia=i
        do i=i0,ib,1
           if(s(i)/s(i0).lt.0.01) exit
        enddo
        ib=i

        fac=fac0
        do i=ia,1,-1
           fac=fac*fac0
           fil(i)=fac*fil(i)
        enddo

        fac=fac0
        do i=ib,NH
           fac=fac*fac0
           fil(i)=fac*fil(i)
        enddo

        do iter=1,100                        !### ??? ###
           call smo121(fil,NH)
        enddo

        do i=0,NH 
           filter(i)=-60.0
           if(s(i).gt.0.0) filter(i)=20.0*log10(fil(i))
        enddo

        il=nint(NPOLYLOW/df)
        ih=nint(NPOLYHIGH/df)
        nfit=ih-il+1
        mode=0
        nterms=5
        do i=1,nfit
          xfit(i)=((i+il-1)*df-1500.0)/1000.0
          yfit(i)=fil(i+il-1)
          sigmay(i)=1.0
        enddo
        call polyfit(xfit,yfit,sigmay,nfit,nterms,mode,a,chisqr)

        open(16,file=fname,status='unknown',iostat=ios)
        if(ios.eq.0) write(16,1003,iostat=ios) NPOLYLOW,NPOLYHIGH,nterms,a
1003    format(3i5,5e25.16)
        do i=1,NH
           freq=i*df
           ref(i)=db(s(i)/avemid)
           if(ios.eq.0) write(16,1005,iostat=ios) freq,s(i),ref(i),fil(i),filter(i)
1005       format(f10.3,e12.3,f12.6,e12.3,f12.6)
        enddo
        close(16,iostat=iosclose)
        if(ios.eq.0) ios=iosclose
        refspec_status=REFSPEC_WRITTEN
        if(ios.ne.0) refspec_status=REFSPEC_WRITE_FAILED
     endif
     enddo
     return
  endif

  if(buseref) then
     if(blastuse.neqv.buseref) then !just enabled so read filter
        fil=1.0
        open(16,file=fname,status='old',iostat=ios)
        if(ios.ne.0) go to 998
        read(16,1003,iostat=ios) ndummy,ndummy,nterms,a
        if(ios.lt.0) go to 998
        if(ios.gt.0) rewind(16,iostat=ios)  !allow for old style refspec.dat with no header
        if(ios.ne.0) go to 998
        do i=1,NH
           read(16,1005,iostat=ios) freq,s(i),ref(i),fil(i),filter(i)
           if(ios.ne.0) go to 998
        enddo
! Make the filter causal for overlap and add.
        cx(0)=0.0
        cx(1:NH)=fil(1:NH)/NFFT
        call four2a(cx,NFFT,1,1,-1)
        x=cshift(x,-400)
! Retain only the causal 800-tap response. Leaving the negative-time tail
! near NFFT produces circular wraparound that depends on input chunk size.
        x(800:NFFT-1)=0.0
        call four2a(cx,NFFT,1,-1,0)
        cfil=cx
        close(16,iostat=ios)
        refspec_status=REFSPEC_LOADED
     endif
! Use overlap and add method to apply causal reference filter.
     x(0:ninput-1)=id2(1:ninput)
     x(ninput:NFFT-1)=0.0
     x=x/NFFT
     call four2a(cx,NFFT,1,-1,0)
     cx=cfil*cx
     call four2a(cx,NFFT,1,1,-1)
     x(0:NH-1)=x(0:NH-1)+xs    
     xs=x(ninput:ninput+NH-1)
     id2(1:ninput)=nint(x(0:ninput-1))
  endif
  blastuse=buseref
  return

998 close(16,iostat=ios)
  refspec_status=REFSPEC_READ_FAILED
end subroutine refspectrum
