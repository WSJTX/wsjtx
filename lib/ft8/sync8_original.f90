subroutine sync8(dd,nfa,nfb,syncmin,nfqso,maxcand,nzhsym,candidate,   &
     ncand,sbase)

  include 'ft8_params.f90'
  parameter (MAXPRECAND=1000)
! Maximum sync correlation lag +/- 2.5s relative to 0.5s TX start time. 
! 2.5s / 0.16s/symbol * 4 samples/symbol = 62.5 lag steps in 2.5s
  parameter (JZ=62)                        
  complex cx(0:NH1)
  real s(NH1,NHSYM)
  real savg(NH1)
  real sbase(NH1)
  real x(NFFT1)
  real sync2d(NH1,-JZ:JZ)
  real red(NH1)
  real red2(NH1)
  real candidate0(3,MAXPRECAND)
  real candidate(3,maxcand)
  real dd(NMAX)
  integer jpeak(NH1)
  integer jpeak2(NH1)
  integer indx(NH1)
  integer indx2(NH1)
  integer ii(1)
  integer icos7(0:6)
  data icos7/3,1,4,0,6,5,2/                   !Costas 7x7 tone pattern
  equivalence (x,cx)

! Compute symbol spectra, stepping by NSTEP steps.  
  savg=0.
  tstep=NSTEP/12000.0                         
  df=12000.0/NFFT1                            !3.125 Hz
  fac=1.0/300.0
  do j=1,NHSYM
     ia=(j-1)*NSTEP + 1
     ib=ia+NSPS-1
     x(1:NSPS)=fac*dd(ia:ib)
     x(NSPS+1:)=0.
     call four2a(x,NFFT1,1,-1,0)              !r2c FFT
     do i=1,NH1
        s(i,j)=real(cx(i))**2 + aimag(cx(i))**2
     enddo
     savg=savg + s(1:NH1,j)                   !Average spectrum
  enddo
  call get_spectrum_baseline(dd,nfa,nfb,sbase)

  ia=max(1,nint(nfa/df))
  ib=nint(nfb/df)
  nssy=NSPS/NSTEP   ! # steps per symbol
  nfos=NFFT1/NSPS   ! # frequency bin oversampling factor
  jstrt=0.5/tstep
  candidate0=0.
  k=0

  do i=ia,ib
     do j=-JZ,+JZ
        ta=0.
        tb=0.
        tc=0.
        t0a=0.
        t0b=0.
        t0c=0.
        do n=0,6
           m=j+jstrt+nssy*n
           if(m.ge.1.and.m.le.NHSYM) then
              ta=ta + s(i+nfos*icos7(n),m)
              t0a=t0a + sum(s(i:i+nfos*6:nfos,m))
           endif
           tb=tb + s(i+nfos*icos7(n),m+nssy*36)
           t0b=t0b + sum(s(i:i+nfos*6:nfos,m+nssy*36))
           if(m+nssy*72.le.NHSYM) then
              tc=tc + s(i+nfos*icos7(n),m+nssy*72)
              t0c=t0c + sum(s(i:i+nfos*6:nfos,m+nssy*72))
           endif
        enddo
        t=ta+tb+tc
        t0=t0a+t0b+t0c
        t0=(t0-t)/6.0
        sync_abc=t/t0
        t=tb+tc
        t0=t0b+t0c
        t0=(t0-t)/6.0
        sync_bc=t/t0
        sync2d(i,j)=max(sync_abc,sync_bc)
     enddo
  enddo

  red=0.
  red2=0.
  mlag=10
  mlag2=JZ
  do i=ia,ib
     ii=maxloc(sync2d(i,-mlag:mlag)) - 1 - mlag 
     jpeak(i)=ii(1)
     red(i)=sync2d(i,jpeak(i))
     ii=maxloc(sync2d(i,-mlag2:mlag2)) - 1 - mlag2
     jpeak2(i)=ii(1)
     red2(i)=sync2d(i,jpeak2(i))
  enddo
  iz=ib-ia+1
  call indexx(red(ia:ib),iz,indx)
  npctile=nint(0.40*iz)
  if(npctile.lt.1) then ! something is wrong; bail out
    ncand=0
    return;
  endif
  ibase=indx(npctile) - 1 + ia
  if(ibase.lt.1) ibase=1
  if(ibase.gt.nh1) ibase=nh1
  base=red(ibase)
  red=red/base
  call indexx(red2(ia:ib),iz,indx2)
  ibase2=indx2(npctile) - 1 + ia
  if(ibase2.lt.1) ibase2=1
  if(ibase2.gt.nh1) ibase2=nh1
  base2=red2(ibase2)
  red2=red2/base2
  do i=1,min(MAXPRECAND,iz)
     n=ia + indx(iz+1-i) - 1
     if(k.ge.MAXPRECAND) exit
     if( (red(n).ge.syncmin) .and. (.not.isnan(red(n))) ) then 
        k=k+1
        candidate0(1,k)=n*df
        candidate0(2,k)=(jpeak(n)-0.5)*tstep
        candidate0(3,k)=red(n)
     endif
     if(abs(jpeak2(n)-jpeak(n)).eq.0) cycle 
     if(k.ge.MAXPRECAND) exit
     if( (red2(n).ge.syncmin) .and. (.not.isnan(red2(n))) ) then
        k=k+1
        candidate0(1,k)=n*df
        candidate0(2,k)=(jpeak2(n)-0.5)*tstep
        candidate0(3,k)=red2(n)
     endif
  enddo
  ncand=k

! Save only the best of near-dupe freqs.  
  do i=1,ncand
     if(i.ge.2) then
        do j=1,i-1
           fdiff=abs(candidate0(1,i))-abs(candidate0(1,j))
           tdiff=abs(candidate0(2,i)-candidate0(2,j))
           if(abs(fdiff).lt.4.0.and.tdiff.lt.0.04) then
              if(candidate0(3,i).ge.candidate0(3,j)) candidate0(3,j)=0.
              if(candidate0(3,i).lt.candidate0(3,j)) candidate0(3,i)=0.
           endif
        enddo
     endif
  enddo
  fac=20.0/maxval(s)
  s=fac*s

! Sort by sync
  call indexx(candidate0(3,1:ncand),ncand,indx)
! Place candidates within 10 Hz of nfqso at the top of the list
  k=1
  do i=1,ncand
    if( abs( candidate0(1,i)-nfqso ).le.10.0 .and. candidate0(3,i).ge.syncmin ) then
      candidate(1:3,k)=candidate0(1:3,i)
      candidate0(3,i)=0.0
      k=k+1
    endif
  enddo
 
  do i=ncand,1,-1
     j=indx(i)
     if( candidate0(3,j) .ge. syncmin ) then
       candidate(2:3,k)=candidate0(2:3,j)
       candidate(1,k)=abs(candidate0(1,j))
       k=k+1
       if(k.gt.maxcand) exit
     endif
  enddo
  ncand=k-1
  return
end subroutine sync8
