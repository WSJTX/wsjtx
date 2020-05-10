subroutine sh65(cx,n5,mode65,ntol,xdf,nspecial,snrdb)
  parameter(NFFT=2048,NH=NFFT/2)
  complex cx(n5)               !Centered on nfqso, sample rate 1378.125
  complex c(0:NFFT-1)
  real s(-NH+1:NH)
  real ss(-NH+1:NH,16)
  real sigmax(16)
  integer ipk(16)

  ss=0.

  jstep=NFFT/8
  nblks=272
  ia=-jstep+1
  do iblk=1,nblks
     n=mod(iblk-1,16) + 1
     ia=ia+jstep
     ib=ia+NFFT-1
     c=cx(ia:ib)
     call four2a(c,nfft,1,1,1)            !c2c FFT
     do i=0,NFFT-1
        j=i
        if(j.gt.NH) j=j-NFFT
        ss(j,n)=ss(j,n) + real(c(i))**2 + aimag(c(i))**2
     enddo
  enddo

  s=1.e-5*s
  ss=1.e-5*ss
  df=1378.1285/NFFT
  nfac=40*mode65
  dtstep=0.25/df

  do i=1,2*mode65
     call smo121(ss,16*NFFT)
  enddo

!  do i=-NH+1,NH
!     write(72,3072) i*df,(ss(i,j),j=1,16)
!3072 format(17f7.1)
!  enddo

! Define freq range to be searched. Upper tone is at sync freq  + 4*nfac*df Hz
  fa=-ntol
  fb=ntol
  ia2=max(-NH+1,nint(fa/df))
  ib2=min(NH,nint(fb/df + 4.1*nfac))

! Find strongest line in each of the 16 phases
  sbest=0.
  snrbest=0.
  nbest=1
  ipk=0
  do n=1,16
     sigmax(n)=0.
     do i=ia2,ib2
        sig=ss(i,n)
        if(sig.ge.sigmax(n)) then
           ipk(n)=i
           sigmax(n)=sig
           if(sig.ge.sbest) then
              sbest=sig
              nbest=n
           endif
        endif
     enddo
  enddo
  n2best=nbest+8
  if(n2best.gt.16) n2best=nbest-8
  xdf=min(ipk(nbest),ipk(n2best))*df
  nspecial=0
  if(abs(xdf).le.ntol) then
     idiff=abs(ipk(nbest)-ipk(n2best))
     xk=float(idiff)/nfac
     k=nint(xk)
     iderr=nint((xk-k)*nfac)
!     maxerr=nint(0.008*abs(idiff) + 0.51)
     maxerr=nint(0.02*abs(idiff) + 0.51)     !### Better test ??? ###
!     write(71,3001) nbest,n2best,idiff,iderr,maxerr,k,      &
!          ipk(nbest)*df,ipk(n2best)*df,sbest
!3001 format(6i4,2f7.1,f7.2)
     if(abs(iderr).le.maxerr .and. k.ge.2 .and. k.le.4) nspecial=k
     snrdb=-30.0
     if(nspecial.gt.0) then
        call sh65snr(ss(ia2,nbest),ib2-ia2+1,snr1)
        call sh65snr(ss(ia2,n2best),ib2-ia2+1,snr2)
        snr=0.5*(snr1+snr2)
        snrdb=db(snr) - db(2500.0/df) - db(sqrt(nblks/4.0)) + 8.0
     endif
     if(snr1.lt.4.0 .or. snr2.lt.4.0 .or. snr.lt.5.0) nspecial=0
  endif

  return
end subroutine sh65
