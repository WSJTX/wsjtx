subroutine fil4(id1,n1,id2,n2)

  parameter (NTAPS=49)
  integer*2 id1(n1)
  integer*2 id2(*)
  real t(NTAPS)
  data t/NTAPS*0.0/
  save t

  call fil4_state(id1,n1,id2,n2,t)

  return
end subroutine fil4

subroutine fil4_state(id1,n1,id2,n2,t)

! FIR lowpass filter designed using ScopeFIR

! fsample     = 48000 Hz
! Ntaps       = 49
! fc          = 4500  Hz
! fstop       = 6000  Hz
! Ripple      = 1     dB
! Stop Atten  = 40    dB
! fout        = 12000 Hz

  parameter (NTAPS=49)
  parameter (NDOWN=4)             !Downsample ratio
  integer*2 id1(n1)
  integer*2 id2(*)
  real t(NTAPS)

! Filter coefficients:
  real w(NTAPS)
  data w/                                                                 &
        0.000861074040, 0.010051920210, 0.010161983649, 0.011363155076,   &
        0.008706594219, 0.002613872664,-0.005202883094,-0.011720748164,   &
       -0.013752163325,-0.009431602741, 0.000539063909, 0.012636767098,   &
        0.021494659597, 0.021951235065, 0.011564169382,-0.007656470131,   &
       -0.028965787341,-0.042637874109,-0.039203309748,-0.013153301537,   &
        0.034320769178, 0.094717832646, 0.154224604789, 0.197758325022,   &
        0.213715139513, 0.197758325022, 0.154224604789, 0.094717832646,   &
        0.034320769178,-0.013153301537,-0.039203309748,-0.042637874109,   &
       -0.028965787341,-0.007656470131, 0.011564169382, 0.021951235065,   &
        0.021494659597, 0.012636767098, 0.000539063909,-0.009431602741,   &
       -0.013752163325,-0.011720748164,-0.005202883094, 0.002613872664,   &
        0.008706594219, 0.011363155076, 0.010161983649, 0.010051920210,   &
        0.000861074040/
  save w

  n2=n1/NDOWN
  if(n2*NDOWN.ne.n1) stop 'Error in fil4'
  k=1-NDOWN
  do i=1,n2
     k=k+NDOWN
     t(1:NTAPS-NDOWN)=t(1+NDOWN:NTAPS)          !Shift old data down in array t
     t(1+NTAPS-NDOWN:NTAPS)=id1(k:k+NDOWN-1)    !Insert new data at end of t
     id2(i)=nint(dot_product(w,t))
  enddo

  return
end subroutine fil4_state
