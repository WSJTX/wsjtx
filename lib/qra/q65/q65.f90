module q65

  parameter (NSTEP=8)                !Time bins per symbol, in s1() and s1a()
  parameter (PLOG_MIN=-240.0)        !List decoding threshold
  integer nsave,nlist,LL0
  integer listutc(10)
  integer apsym0(58),aph10(10)
  integer apmask(13),apsymbols(13)
  integer,dimension(22) ::  isync = (/1,9,12,13,15,22,23,26,27,33,35,   &
                                     38,46,50,55,60,62,66,69,74,76,85/)
  integer codewords(63,206)
  integer navg,ibwa,ibwb,ncw
  real,allocatable,save :: s1a(:,:)      !Cumulative symbol spectra
  real sync(85)                          !sync vector

contains


subroutine q65_clravg

  s1a=0.
  navg=0
  
  return
end subroutine q65_clravg

subroutine q65_dec2(s3,nsubmode,b90ts,esnodb,irc,dat4,decoded)

  use packjt77
  real s3(1,1)       !Silence compiler warning that wants to see a 2D array
  real s3prob(0:63,63)                   !Symbol-value probabilities
  integer dat4(13)
  character c77*77,decoded*37
  logical unpk77_success

  nFadingModel=1
  decoded='                                     '
  call q65_intrinsics_ff(s3,nsubmode,b90ts,nFadingModel,s3prob)
  call q65_dec(s3,s3prob,APmask,APsymbols,esnodb,dat4,irc)
  if(sum(dat4).le.0) irc=-2
  if(irc.ge.0) then
     write(c77,1000) dat4(1:12),dat4(13)/2
1000 format(12b6.6,b5.5)
     call unpack77(c77,0,decoded,unpk77_success) !Unpack to get msgsent
  endif

  return
end subroutine q65_dec2

subroutine q65_s1_to_s3(s1,iz,jz,i0,j0,ipk,jpk,LL,mode_q65,sync,s3)

! Copy from s1 or s1a into s3

  real s1(iz,jz)
  real s3(-64:LL-65,63)
  real sync(85)                          !sync vector
  
  i1=i0+ipk-64 + mode_q65
  i2=i1+LL-1
  if(i1.ge.1 .and. i2.le.iz) then
     j=j0+jpk-7
     n=0
     do k=1,85
        j=j+8
        if(sync(k).gt.0.0) then
           cycle
        endif
        n=n+1
        if(j.ge.1 .and. j.le.jz) s3(-64:LL-65,n)=s1(i1:i2,j)
     enddo
  endif

  return
end subroutine q65_s1_to_s3

end module q65
