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
  integer navg,ibwa,ibwb,ncw,nsps,mode_q65,istep,nsmo
  real,allocatable,save :: s1a(:,:)      !Cumulative symbol spectra
  real sync(85)                          !sync vector

contains

subroutine q65_clravg

  s1a=0.
  navg=0
  
  return
end subroutine q65_clravg

subroutine q65_symspec(iwave,nmax,iz,jz,s1)

  integer*2 iwave(0:nmax-1)              !Raw data
  real s1(iz,jz)
  complex, allocatable :: c0(:)          !Complex spectrum of symbol

  allocate(c0(0:nsps-1))
  nfft=nsps
  fac=1/32767.0
  do j=1,jz                              !Compute symbol spectra at step size
     i1=(j-1)*istep
     i2=i1+nsps-1
     k=-1
     do i=i1,i2,2          !Load iwave data into complex array c0, for r2c FFT
        xx=iwave(i)
        yy=iwave(i+1)
        k=k+1
        c0(k)=fac*cmplx(xx,yy)
     enddo
     c0(k+1:)=0.
     call four2a(c0,nfft,1,-1,0)              !r2c FFT
     do i=1,iz
        s1(i,j)=real(c0(i))**2 + aimag(c0(i))**2
     enddo
! For large Doppler spreads, should we smooth the spectra here?
     do i=1,nsmo
        call smo121(s1(1:iz,j),iz)
     enddo
  enddo
  s1a=s1a+s1
  navg=navg+1

  return
end subroutine q65_symspec

subroutine q65_dec_q3(df,s1,iz,jz,ia,lag1,lag2,i0,j0,ccf,ccf1,ccf2,  &
     ia2,s3,LL,nfqso,dtstep,xdt,f0,snr2,dat4,idec,decoded)

  character*37 decoded
  integer itone(85)
  integer ijpk(2)
  integer dat4(13)
  real ccf(-ia2:ia2,-53:214)
  real ccf1(-ia2:ia2)
  real ccf2(-ia2:ia2)
  real s1(iz,jz)
  real s3(-64:LL-65,63)

  ipk=0
  jpk=0
  ccf_best=0.
  imsg_best=-1
  do imsg=1,ncw
     i=1
     k=0
     do j=1,85
        if(j.eq.isync(i)) then
           i=i+1
           itone(j)=-1
        else
           k=k+1
           itone(j)=codewords(k,imsg)
        endif
     enddo
! Compute 2D ccf using all 85 symbols in the list message
     ccf=0.
     iia=200.0/df

     do lag=lag1,lag2
        do k=1,85
           j=j0 + NSTEP*(k-1) + 1 + lag
           if(j.ge.1 .and. j.le.jz) then
              do i=-ia2,ia2
                 ii=i0+mode_q65*itone(k)+i
                 if(ii.ge.iia .and. ii.le.iz) ccf(i,lag)=ccf(i,lag) + s1(ii,j)
              enddo
           endif
        enddo
     enddo

     ccfmax=maxval(ccf(-ia:ia,:))
     if(ccfmax.gt.ccf_best) then
        ccf_best=ccfmax
        ijpk=maxloc(ccf(-ia:ia,:))
        ipk=ijpk(1)-ia-1
        jpk=ijpk(2)-53-1
        f0=nfqso + (ipk-mode_q65)*df
        xdt=jpk*dtstep
        imsg_best=imsg
        ccf1=ccf(:,jpk)
     endif
  enddo  ! imsg

  i1=i0+ipk-64
  i2=i1+LL-1
  j=j0+jpk-7
  n=0
  do k=1,85
     j=j+8
     if(sync(k).gt.0.0) then
        cycle
     endif
     n=n+1
     if(j.ge.1 .and. j.le.jz) then
        do i=0,LL-1
           s3(i-64,n)=s1(i+i1,j)              !Copy from s1 into s3
        enddo
     endif
  enddo

  nsubmode=0
  if(mode_q65.eq.2) nsubmode=1
  if(mode_q65.eq.4) nsubmode=2
  if(mode_q65.eq.8) nsubmode=3
  if(mode_q65.eq.16) nsubmode=4
  baud=12000.0/nsps

  do ibw=ibwa,ibwb
     b90=1.72**ibw
     b90ts=b90/baud
     call q65_dec1(s3,nsubmode,b90ts,esnodb,irc,dat4,decoded)
     if(irc.ge.0) then
        snr2=esnodb - db(2500.0/baud) + 3.0     !Empirical adjustment
        idec=1
        ic=ia2/4;
        base=(sum(ccf1(-ia2:-ia2+ic)) + sum(ccf1(ia2-ic:ia2)))/(2.0+2.0*ic);
        ccf1=ccf1-base
        smax=maxval(ccf1)
        if(smax.gt.10.0) ccf1=10.0*ccf1/smax
        base=(sum(ccf2(-ia2:-ia2+ic)) + sum(ccf2(ia2-ic:ia2)))/(2.0+2.0*ic);
        ccf2=ccf2-base
        smax=maxval(ccf2)
        if(smax.gt.10.0) ccf2=10.0*ccf2/smax
        exit
     endif
  enddo

  return
end subroutine q65_dec_q3

subroutine q65_dec1(s3,nsubmode,b90ts,esnodb,irc,dat4,decoded)

  use packjt77
  real s3(1,1)       !Silence compiler warning that wants to see a 2D array
  real s3prob(0:63,63)                   !Symbol-value probabilities
  integer dat4(13)
  character c77*77,decoded*37
  logical unpk77_success
  
  nFadingModel=1
  decoded='                                     '
  call q65_intrinsics_ff(s3,nsubmode,b90ts,nFadingModel,s3prob)
  call q65_dec_fullaplist(s3,s3prob,codewords,ncw,esnodb,dat4,plog,irc)
  if(sum(dat4).le.0) irc=-2
  if(irc.ge.0 .and. plog.gt.PLOG_MIN) then
     write(c77,1000) dat4(1:12),dat4(13)/2
1000 format(12b6.6,b5.5)
     call unpack77(c77,0,decoded,unpk77_success) !Unpack to get msgsent
  else
     irc=-1
  endif
  
  return
end subroutine q65_dec1

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
