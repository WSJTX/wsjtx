subroutine recvpkt(nsam,nblock2,userx_no,k,buf4,buf8,ndb)

! Reformat timf2 data from Linrad and stuff data into r*4 array dd().

  use qmap_params, only: MAX_NSMAX, MAX_NFFT, npairs_active
  include 'njunk.f90'
  integer*1 userx_no
  real*4 d4,buf4(*)                   !(348)
  real*8 d8,buf8(*)                   !(174)
  integer*2 jd(4),kd(2),nblock2
  real*4 yd(2)
  real*8 fcenter
  common/datcom/dd(2,MAX_NSMAX),ss(400,MAX_NFFT),savg(MAX_NFFT),fcenter,nutc,  &
       junk(NJUNK)
  equivalence (kd,d4)
  equivalence (jd,d8,yd)

  gain=10.0**(0.05*ndb)
  if(nblock2.eq.-9999) nblock2=-9998    !Silence a compiler warning
  if(nsam.eq.-1) then
! Move data from the UDP packet buffer into array dd().
     if(userx_no.eq.-1) then
        do i=1,npairs_active       !One RF channel, r*4 data (174 pairs in a 1416-byte packet)
           k=k+1
           d8=buf8(i)
           dd(1,k)=yd(1)*gain
           dd(2,k)=yd(2)*gain
        enddo
     else if(userx_no.eq.1) then
        do i=1,npairs_active       !One RF channel, i*2 data (348 pairs at 96 kHz)
           k=k+1
           d4=buf4(i)
           dd(1,k)=kd(1)*gain
           dd(2,k)=kd(2)*gain
        enddo
     endif
  else
     if(userx_no.eq.1) then
        do i=1,nsam                    !One RF channel, r*4 data
           k=k+1
           d4=buf4(i)
           dd(1,k)=kd(1)*gain
           dd(2,k)=kd(2)*gain

           k=k+1
           dd(1,k)=kd(1)*gain
           dd(2,k)=kd(2)*gain
        enddo
     endif
  endif

  return
end subroutine recvpkt
