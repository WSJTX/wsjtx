subroutine cw_cwave(csync)

! Generate complex sync waveform.
  real dummy(0:13*192-1)
  complex csync(0:13*192-1)
  integer ib13(13)                          !Barker 13 sequence
  data ib13/0,0,0,0,0,1,1,0,0,1,0,1,0/      !Barker 13 sequence

  fsample=6000.0
  nsps=192                          !Samples per symbol @6000 Hz
  nsym=13                           !Sync sequence has 13 symbols
  bt=2.0                            !Default bt=2 (smaller ==> more smoothing)
  f0=0.0                            !Frequency of lowest tone
  nwave=nsps*nsym                   !Length of csync
  icmplx=1
write(*,*) 'call gen_jttywave ',nsym,nsps,bt,fsample,f0,icmplx,nwave
  call gen_jttywave(ib13,nsym,nsps,bt,fsample,f0,csync,dummy,icmplx,nwave)

  return
end subroutine cw_cwave
