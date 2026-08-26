module s3avg_mod
  implicit none
contains

subroutine s3avg(nsave, mode65, nutc, nhz, xdt, npol, ntol, s3, nsum, nkv, decoded)
  use extract_mod
  implicit none

! Save the current synchronized spectra, s3(64,63), for possible
! decoding of average.

  integer,      intent(inout) :: nsave
  integer,      intent(in)    :: mode65, nutc, nhz, npol, ntol
  real,         intent(in)    :: xdt
  real,         intent(in)    :: s3(64,63)   !Synchronized spectra for 63 symbols
  integer,      intent(out) :: nsum, nkv
  character(len=22), intent(out) :: decoded

  real s3a(64,63,64)                    !Saved spectra
  real s3b(64,63)                       !Average spectra
  integer iutc(64),ihz(64),ipol(64)
  integer :: i,ihzdiff,nadd,ncount,nhist
  integer :: mrs(63), mrs2(63)          !Dummy arguments for extract
  real, parameter :: dtdiff=0.2
  real dt(64)
  logical ltext,first
  data first/.true./
  save first,iutc,ihz,ipol,s3a,dt

  nsum=0
  nkv=0
  decoded='                      '
  ihzdiff=min(100,ntol)

  if(first) then
     iutc=-1
     ihz=0
     ipol=0
     first=.false.
  endif

  do i=1,64
     if(nutc.eq.iutc(i) .and. abs(nhz-ihz(i)).le.ihzdiff) then
        nsave=mod(nsave-2+64,64)+1
        go to 10
     endif
  enddo
  
  iutc(nsave)=nutc                          !Save UTC
  ihz(nsave)=nhz                            !Save freq in Hz
  ipol(nsave)=npol                          !Save pol
  dt(nsave)=xdt                             !Save DT
  s3a(1:64,1:63,nsave)=s3                   !Save the spectra

10 s3b=0.
  do i=1,64                                 !Accumulate avg spectra
     if(iutc(i).lt.0) cycle
     if(mod(iutc(i),2).ne.mod(nutc,2)) cycle !Use only same sequence
     if(abs(nhz-ihz(i)).gt.ihzdiff) cycle   !Freq must match
     if(abs(xdt-dt(i)).gt.dtdiff) cycle     !DT must match
     s3b=s3b + s3a(1:64,1:63,i)
     nsum=nsum+1
  enddo
 
  if(nsum.ge.2) then                        !Try decoding the sverage
     nadd=mode65*nsum
     call extract(s3b,nadd,ncount,nhist,decoded,ltext,mrs,mrs2)     !Extract the message
     nkv=nsum
     if(ncount.lt.0) then 
        nkv=0
        decoded='                      '
     endif
  endif

  return
end subroutine s3avg

end module s3avg_mod
