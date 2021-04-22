subroutine fil6521(c1,n1,c2,n2)

! FIR lowpass filter designed using ScopeFIR

!                  Pass #1   Pass #2  
!-----------------------------------------------
! fsample    (Hz)  1378.125   Input sample rate
! Ntaps            21         Number of filter taps
! fc         (Hz)  40         Cutoff frequency
! fstop      (Hz)  172.266    Lower limit of stopband
! Ripple     (dB)  0.1        Ripple in passband
! Stop Atten (dB)  38         Stopband attenuation
! fout       (Hz)  344.531    Output sample rate

  parameter (NTAPS=21)
  parameter (NH=(NTAPS-1)/2)
  parameter (NDOWN=4)                !Downsample ratio = 1/4
  complex c1(n1)
  complex c2(n1/NDOWN)

! Filter coefficients:
  real a(-NH:NH)
  data a/                                                             &
     -0.011958606980,-0.013888627387,-0.015601306443,-0.010602249570, &
      0.003804023436, 0.028320058273, 0.060903935217, 0.096841904411, &
      0.129639871228, 0.152644580853, 0.160917511283, 0.152644580853, &
      0.129639871228, 0.096841904411, 0.060903935217, 0.028320058273, &
      0.003804023436,-0.010602249570,-0.015601306443,-0.013888627387, &
     -0.011958606980/

  n2=(n1-NTAPS+NDOWN)/NDOWN
  k0=NH-NDOWN+1

! Loop over all output samples
  do i=1,n2
     c2(i)=0.
     k=k0 + NDOWN*i
     do j=-NH,NH
        c2(i)=c2(i) + c1(j+k)*a(j)
     enddo
  enddo

  return
end subroutine fil6521
