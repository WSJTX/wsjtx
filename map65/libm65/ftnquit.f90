subroutine ftnquit

! Destroy the FFTW plans
  call four2a(a,-1,1,1,1)
  call filbig(id,-1,1,f0,newdat,nfsample,c4a,c4b,n4)

  return
end subroutine ftnquit
