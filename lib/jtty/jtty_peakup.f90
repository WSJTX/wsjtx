subroutine jtty_peakup(c0,c1,csync,nchunk,nss,xdt0,f0,xdt,f1,snr)
   implicit none

   complex, intent(in) :: c0(0:nchunk-1)                        !Complex data at 6000 S/s
   complex             :: c1(0:nchunk-1)                        !Work array
   complex, intent(in) :: csync(0:13*nss-1)
   complex             :: c(0:13*nss-1)                          !Lengh of Barker sequence
   complex             :: z
   real                :: a(3)
   real                :: fsample, dt, pmax, fpk, xdtpk, xnorm, p
   integer, intent(in) :: nchunk, nss
   integer             :: i, i0, ia, ib, idf, istart, iend, npsync
   real, intent(in)    :: xdt0, f0
   real, intent(out)   :: xdt, f1, snr

   npsync=13*nss        ! size of the sync waveform array
   fsample=6000.0
   dt=1.0/fsample
   ia=max(0,nint((xdt0-0.004)/dt))
   ! The i0 search loop below reads c1(i0:i0+npsync-1), so its upper bound
   ! must keep that in range for c1's actual size (nchunk) -- npsync-1 (the
   ! *sync pattern's own* length, an unrelated quantity also used below for
   ! the small work array c) was wrong here and left ib < ia (an empty
   ! search, silently returning f1=xdt=0) for any xdt0 beyond npsync*dt
   ! (~0.416 s at nsps=384) -- a legitimately reachable value within the
   ! real ~0.472 s candidate search range, confirmed hitting 100 of 570
   ! jtty_peakup calls (17.5%) on 260807_134202.wav, destroying the best
   ! candidate for several real frames and forcing a fallback to noisier,
   ! less precise ones (260807_134202.wav decoded perfectly and
   ! consistently at a single frequency with this call disabled entirely).
   ib=min(nchunk-npsync,nint((xdt0+0.004)/dt))

   pmax=0.
   fpk=0.
   xdtpk=0.
   c1=cmplx(0.,0.)
   xnorm=sum(abs(c0(0:nchunk-1)))/real(nchunk)
   do idf=-5,5
      a=0.
      a(1)=-f0 + 0.5*idf                     !Shift assumed peak to zero frequency
      call twkfreq(c0,c1,nchunk,fsample,a)
      do i0=ia,ib,4                          !Search over xdt for sync pattern
         xdt=i0*dt
         c(0:npsync-1)=conjg(csync)*c1(i0:i0+npsync-1)
! We assume coherence only over the duration of a symbol (32ms) here
         p=0
         do i=0,12
            istart=i*nss
            iend=istart+nss-1
            z=sum( c( istart:iend ) )
            p=p+real(z)**2+aimag(z)**2
         enddo
         if(p.gt.pmax) then
            pmax=p
            fpk=-a(1)
            xdtpk=i0*dt
         endif
      enddo
   enddo

   f1=fpk
   xdt=xdtpk
   snr=pmax
   return
end subroutine jtty_peakup
