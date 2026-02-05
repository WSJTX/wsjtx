subroutine jtty_peakup(c0,c1,csync,xdt0,f0,xdt,f1,snr)

  parameter (NZ=30*6000)                    !Max length of data at 6000 S/s
  parameter (NSPS=384)                      !Samples per symbol @12000 Hz
  parameter (NB=13*NSPS/2)                 !Length of Barker sequence
  complex c0(0:NZ-1)                        !Complex data at 6000 S/s
  complex c1(0:NZ-1)                        !Work array
  complex c(0:NB-1)                         !Lengh of Barker sequence
  complex csync(0:NB-1)
  complex z
  real a(3)

  fsample=6000.0
  dt=1.0/fsample
  ia=max(0,nint((xdt0-0.05)/dt))     
  ib=(xdt0+0.05)/dt
  npts=2*6000                               ! 2 seconds 

  pmax=0.
  fpk=0.
  xdtpk=0.
  c1=cmplx(0.,0.)
  xnorm=sum(abs(c0(1:npts)))/real(npts)
  do idf=-3,3     
     a=0.
     a(1)=-f0 + 0.5*idf                     !Shift assumed peak to zero frequency
     call twkfreq(c0,c1,npts,fsample,a)
     do i0=ia,ib,2                          !Search over xdt for sync pattern
        xdt=i0*dt
        c(0:NB-1)=conjg(csync)*c1(i0:i0+NB-1)
        z=sum(c(0:NB-1))
        p=real(z)**2 + aimag(z)**2
!        write(71,3071) i0*dt,-a(1),p
!3071    format(3f10.3)
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
