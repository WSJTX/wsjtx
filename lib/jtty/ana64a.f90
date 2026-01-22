subroutine ana64a(iwave,npts,c0,nfft1)
  implicit none
  integer*2, intent(in)   :: iwave(npts)   !Raw data at 12000 Hz
  integer, intent(in)     :: npts, nfft1   
  integer                 :: nfft2
  real                    :: fac 
  complex, intent(out)    :: c0(0:nfft1-1)

  nfft2=nfft1/2
  fac=2.0/(32767.0*NFFT1)
  c0(0:npts-1)=fac*iwave(1:npts)
  c0(npts:)=0.
  call four2a(c0,NFFT1,1,-1,1)             !Forward c2c FFT
  c0(nfft2/2+1:nfft2-1)=0.                 !Set negative freqs to 0.
  c0(0)=0.5*c0(0)
  call four2a(c0,nfft2,1,1,1)              !Inverse c2c FFT; c0 is the analytic sig
  c0(npts:)=0.
  return
end subroutine ana64a
