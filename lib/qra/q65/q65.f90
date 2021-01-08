module q65

  parameter (MAXAVE=64)
  parameter (PLOG_MIN=-240.0)            !List decoding threshold
  integer nsave,nlist,LL0
  integer iutc(MAXAVE)
  integer iseq(MAXAVE)
  integer listutc(10)
  integer apsym0(58),aph10(10)
  integer apmask(13),apsymbols(13)
  integer navg,ibwa,ibwb
  real    f0save(MAXAVE)
  real    xdtsave(MAXAVE)
  real    snr1save(MAXAVE)
  real,allocatable :: s3save(:,:,:)
  real,allocatable :: s3avg(:,:)

end module q65
