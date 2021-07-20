subroutine recvpkt(nsam,nblock2,userx_no,k,buf4,buf8,buf16)

! Reformat timf2 data from Linrad and stuff data into r*4 array dd().

  include 'njunk.f90'
  parameter (NSMAX=60*96000)          !Total sample intervals per minute
  parameter (NFFT=32768)
  integer*1 userx_no
  real*4 d4,buf4(*)                   !(348)
  real*8 d8,buf8(*)                   !(174)
  complex*16 c16,buf16(*)             !(87)
  integer*2 jd(4),kd(2),nblock2
  real*4 xd(4),yd(2)
  real*8 fcenter
  common/datcom/dd(4,5760000),ss(4,322,NFFT),savg(4,NFFT),fcenter,nutc,  &
       junk(NJUNK)
  equivalence (kd,d4)
  equivalence (jd,d8,yd)
  equivalence (xd,c16)

  if(nblock2.eq.-9999) nblock2=-9998    !Silence a compiler warning
  if(nsam.eq.-1) then
! Move data from the UDP packet buffer into array dd().
     if(userx_no.eq.-1) then
        do i=1,174                    !One RF channel, r*4 data
           k=k+1
           d8=buf8(i)
           dd(1,k)=yd(1)
           dd(2,k)=yd(2)
        enddo
     else if(userx_no.eq.1) then
        do i=1,348                    !One RF channel, i*2 data
           k=k+1
           d4=buf4(i)
           dd(1,k)=kd(1)
           dd(2,k)=kd(2)
        enddo
     else if(userx_no.eq.-2) then
        do i=1,87                    !Two RF channels, r*4 data
           k=k+1
           c16=buf16(i)
           dd(1,k)=xd(1)
           dd(2,k)=xd(2)
           dd(3,k)=xd(3)
           dd(4,k)=xd(4)
        enddo
     else if(userx_no.eq.2) then
        do i=1,174                    !Two RF channels, i*2 data
           k=k+1
           d8=buf8(i)
           dd(1,k)=jd(1)
           dd(2,k)=jd(2)
           dd(3,k)=jd(3)
           dd(4,k)=jd(4)
        enddo
     endif
  else
     if(userx_no.eq.1) then
        do i=1,nsam                    !One RF channel, r*4 data
           k=k+1
           d4=buf4(i)
           dd(1,k)=kd(1)
           dd(2,k)=kd(2)

           k=k+1
           dd(1,k)=kd(1)
           dd(2,k)=kd(2)
        enddo
     endif
  endif

  return
end subroutine recvpkt
