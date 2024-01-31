subroutine ftnquit

! Destroy the FFTW plans
  call four2a(a,-1,1,1,1)
  call fftbig(id,-1)

  return
end subroutine ftnquit
