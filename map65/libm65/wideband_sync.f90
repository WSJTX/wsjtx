module wideband_sync

  parameter (NFFT=32768)
  integer isync(22)
  real sync_dat(NFFT,4)     !fkhz, ccfmax, xdt, ipol

  contains
  
subroutine wb_sync(ss,savg)

! Compute "orange sync curve" using the Q65 sync pattern

  parameter (LAGMAX=30)
  real ss(4,322,NFFT)
  real savg(4,NFFT)
  logical first
  integer isync0(22)
! Q65 sync symbols
  data isync0/1,9,12,13,15,22,23,26,27,33,35,38,46,50,55,60,62,66,69,74,76,85/
  data first/.true./
  save first

  tstep=2048.0/11025.0
  if(first) then
     fac=0.6/tstep
     do i=1,22                                !Expand the sync stride
        isync(i)=nint((isync0(i)-1)*fac) + 1
     enddo
     first=.false.
  endif

  df=96000.0/NFFT
  ia=nint(7000.0/df)          !Flat frequency range for WSE converters
  ib=nint(89000.0/df)
  lagbest=-1
  ipolbest=-1

  do i=ia,ib
     ccfmax=0.
     do ipol=1,4
        do lag=0,LAGMAX
           ccf=0.
           do j=1,22
              k=isync(j) + lag
              ccf=ccf + ss(ipol,k,i) 
           enddo
           ccf=ccf - savg(ipol,i)*22.0/322.0
           if(ccf.gt.ccfmax) then
              ipolbest=ipol
              lagbest=lag
              ccfmax=ccf
           endif
        enddo  ! lag
     enddo  !ipol

     fkhz=0.001*i*df + 125.0 - 48.0
     xdt=lagbest*tstep-1.0
     sync_dat(i,1)=fkhz
     sync_dat(i,2)=ccfmax
     sync_dat(i,3)=xdt
     sync_dat(i,4)=ipolbest
!     write(14,3010) i,sync_dat(i,1:3),nint(sync_dat(i,4))
!3010 format(i6,3f10.3,i5)

  enddo

  return
end subroutine wb_sync

end module wideband_sync
