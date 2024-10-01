subroutine sfox_remove_tone(c0,fsync)

   parameter (NMAX=15*12000)
   parameter (NFILT=8000)
   complex c0(NMAX)
   complex cwindow(15*12000)
   complex cref(NMAX)
   complex cfilt(NMAX)
   real window(-NFILT/2:NFILT/2)
!  real endcorrection(NFILT/2+1)
   real s(NMAX/4)
   integer ipk(1)
   logical first
   data first/.true./
   save cwindow,first,pi

   if(fsync.gt.1400.0) return     !Avoid a potential bounds error

   if(first) then
      pi=4.0*atan(1.0)
      fac=1.0/float(NMAX)
      sumw=0.0
      do j=-NFILT/2,NFILT/2
         window(j)=cos(pi*j/NFILT)**2
         sumw=sumw+window(j)
      enddo
      cwindow=0.
      cwindow(1:NFILT+1)=window/sumw
      cwindow=cshift(cwindow,NFILT/2+1)
      call four2a(cwindow,NMAX,1,-1,1)
      cwindow=cwindow*fac        ! frequency domain smoothing filter
      first=.false.
   endif

   fsample=12000.0
   baud=fsample/1024.0
   df=fsample/NMAX
   fac=1.0/NMAX

   do it=1,1                           ! Remove 1 tone, if present
      cfilt=fac*c0
      call four2a(cfilt,NMAX,1,-1,1)   ! fourier transform of input data
      iz=NMAX/4
      do i=1,iz
         s(i)=real(cfilt(i))**2 + aimag(cfilt(i))**2
      enddo

      ia=nint((fsync-50.0)/df)
      ib=nint((fsync+1500.0+50.0)/df)
      ipk=maxloc(s(ia:ib))
      i0=ipk(1) + ia - 1

      nbaud=nint(baud/df)
      ia=i0-nbaud
      ib=i0+nbaud
      s0=0.0
      s1=0.0
      s2=0.0
      do i=ia,ib
         s0=s0+s(i)
         s1=s1+(i-i0)*s(i)
      enddo
      delta=s1/s0
      i0=nint(i0+delta)
      f2=i0*df

      ia=i0-nbaud
      ib=i0+nbaud
      do i=ia,ib
         s2=s2 + s(i)*(i-i0)**2
      enddo
      sigma=sqrt(s2/s0)*df

!      write(*,*) 'frequency, spectral width ',f2,sigma
      if(sigma .gt. 2.5) exit
!      write(*,*) 'remove_tone - frequency: ',f2

      dt=1.0/fsample
      do i=1, NMAX
         arg=2*pi*f2*i*dt
         cref(i)=cmplx(cos(arg),sin(arg))
      enddo
      cfilt=c0*conjg(cref)   ! baseband to be filtered
      call four2a(cfilt,NMAX,1,-1,1)
      cfilt=cfilt*cwindow
      call four2a(cfilt,NMAX,1,1,1)

      nframe=50*3456
      do i=1,nframe
         cref(i)=cfilt(i)*cref(i)
         c0(i)=c0(i)-cref(i)
      enddo
   enddo

   return
end subroutine sfox_remove_tone
