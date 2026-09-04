subroutine jtty_peakup(c0,c1,csync,nchunk,nss,xdt0,f0,xdt,f1,snr)
   implicit none

   complex, intent(in) :: c0(0:nchunk-1)                        !Complex data at 6000 S/s
   complex             :: c1(0:nchunk-1)                        !Work array
   complex, intent(in) :: csync(0:13*nss-1)
   complex             :: c(0:13*nss-1)                          !Lengh of Barker sequence
   complex             :: z
   complex             :: zcur(0:12), zbest(0:12), ztot
   real                :: a(3)
   real                :: fsample, dt, pmax, fpk, xdtpk, xnorm, p
   real                :: phase(0:12), uw(0:12)
   real                :: dphi, xm, ym, sxy, sxx, slope, intercept
   real                :: resid, resid_rms, dfhz, tsym
   integer, intent(in) :: nchunk, nss
   integer             :: i, i0, ia, ib, idf, istart, iend, npsync
   real, intent(in)    :: xdt0, f0
   real, intent(out)   :: xdt, f1, snr
   real, parameter     :: TWOPI = 6.283185307179586
   real, parameter     :: PI = 3.141592653589793

   npsync=13*nss        ! size of the sync waveform array
   fsample=6000.0
   dt=1.0/fsample
   ia=max(0,nint((xdt0-0.004)/dt))
   ! ib must bound the i0 loop against c1's actual size (nchunk), not
   ! npsync (an unrelated length) -- else ib<ia silently returns f1=xdt=0.
   ib=min(nchunk-npsync,nint((xdt0+0.004)/dt))

   pmax=0.
   fpk=0.
   xdtpk=0.
   c1=cmplx(0.,0.)
   xnorm=sum(abs(c0(0:nchunk-1)))/real(nchunk)
   do idf=-5,5
      a=0.
      a(1)=-f0 + 0.5*idf                     !Shift assumed peak to zero frequency
      call twkfreq(c0,c1,ib+npsync,fsample,a)
      do i0=ia,ib,4                          !Search over xdt for sync pattern
         xdt=i0*dt
         c(0:npsync-1)=conjg(csync)*c1(i0:i0+npsync-1)
! Coherent only within a symbol (32ms) here; this locates the sync
! instant. Stage 2 below refines f1 with a coherent combination across
! all 13 symbols once that instant is known.
         p=0
         do i=0,12
            istart=i*nss
            iend=istart+nss-1
            z=sum( c( istart:iend ) )
            zcur(i)=z
            p=p+real(z)**2+aimag(z)**2
         enddo
         if(p.gt.pmax) then
            pmax=p
            fpk=-a(1)
            xdtpk=i0*dt
            zbest=zcur
         endif
      enddo
   enddo

   ! Stage 2: the winning candidate's 13 per-symbol phasors carry a near-
   ! linear phase ramp vs. symbol index, from the sub-0.5-Hz residual
   ! frequency error the coarse idf grid above can't resolve. Fit and
   ! remove that ramp, then combine all 13 symbols coherently for a
   ! sharper f1.
   if(pmax.gt.0.) then
      tsym=nss/fsample
      do i=0,12
         phase(i)=atan2(aimag(zbest(i)),real(zbest(i)))
      enddo
      uw(0)=phase(0)
      do i=1,12
         dphi=phase(i)-phase(i-1)
         do while(dphi.gt.PI)
            dphi=dphi-TWOPI
         enddo
         do while(dphi.lt.-PI)
            dphi=dphi+TWOPI
         enddo
         uw(i)=uw(i-1)+dphi
      enddo
      xm=6.0                                 !mean symbol index (0..12)
      ym=sum(uw)/13.0
      sxy=0.
      sxx=0.
      do i=0,12
         sxy=sxy+(real(i)-xm)*(uw(i)-ym)
         sxx=sxx+(real(i)-xm)**2
      enddo
      slope=sxy/sxx
      intercept=ym-slope*xm
      resid_rms=0.
      do i=0,12
         resid=uw(i)-(slope*real(i)+intercept)
         resid_rms=resid_rms+resid**2
      enddo
      resid_rms=sqrt(resid_rms/13.0)
      dfhz=slope/(TWOPI*tsym)

      ! Guardrails: only trust the refinement when the fit is clean (this
      ! sharpens the coarse estimate, it doesn't replace it) and the
      ! correction stays within the coarse grid's own 0.5 Hz step.
      if(resid_rms.lt.1.0 .and. abs(dfhz).le.0.5) then
         ztot=cmplx(0.,0.)
         do i=0,12
            ztot=ztot+zbest(i)*cmplx(cos(-slope*i),sin(-slope*i))
         enddo
         fpk=fpk+dfhz
         pmax=real(ztot)**2+aimag(ztot)**2
      endif
   endif

   f1=fpk
   xdt=xdtpk
   snr=pmax
   return
end subroutine jtty_peakup
