subroutine sync9(ss,nzhsym,lag1,lag2,ia,ib,ccfred,red2,ipkbest)

  include 'constants.f90'
  real ss(184,NSMAX)
  real ss1(184)
  real ccfred(NSMAX)
  real savg(NSMAX)
  real savg2(NSMAX)
  real smo(-5:25)
  real sq(NSMAX)
  real red2(NSMAX)
  character*27 cr
  data cr/'(C) 2016, Joe Taylor - K1JT'/
  include 'jt9sync.f90'

  ipk=0
  ipkbest=0
  sbest=0.
  ccfred=0.

  do i=ia,ib                         !Loop over freq range
     ss1=ss(1:184,i)
     call pctile(ss1,nzhsym,40,xmed)

     ss1=ss1/xmed - 1.0
     do j=1,nzhsym
        if(ss1(j).gt.3.0) ss1(j)=3.0
     enddo

     call pctile(ss1,nzhsym,45,sbase)
     ss1=ss1-sbase
     sq0=dot_product(ss1(1:nzhsym),ss1(1:nzhsym))
     rms=sqrt(sq0/(nzhsym-1))

     smax=0.
     do lag=lag1,lag2                !DT = 2.5 to 5.0 s
        sum1=0.
        sq2=sq0
        nsum=nzhsym
        do j=1,16                    !Sum over 16 sync symbols
           k=ii2(j) + lag
           if(k.ge.1 .and. k.le.nzhsym) then
              sum1=sum1 + ss1(k)
              sq2=sq2 - ss1(k)*ss1(k)
              nsum=nsum-1
           endif
        enddo
        if(sum1.gt.smax) then
           smax=sum1
           ipk=i 
        endif
        rms=sqrt(sq2/(nsum-1))
     enddo
     ccfred(i)=smax                        !Best at this freq, over all lags
     if(smax.gt.sbest) then
        sbest=smax
        ipkbest=ipk
     endif
  enddo

  call pctile(ccfred(ia),ib-ia+1,50,xmed)
  if(xmed.le.0.0) xmed=1.0
  ccfred=2.0*ccfred/xmed 

  savg=0.
  do j=1,nzhsym
     savg(ia:ib)=savg(ia:ib) + ss(j,ia:ib)
  enddo
  savg(ia:ib)=savg(ia:ib)/nzhsym
  smo(0:20)=1.0/21.0
  smo(-5:-1)=-(1.0/21.0)*(21.0/10.0)
  smo(21:25)=smo(-5)

  do i=ia,ib
     sm=0.
     do j=-5,25
        if(i+j.ge.1 .and. i+j.lt.NSMAX) sm=sm + smo(j)*savg(i+j)
     enddo
     savg2(i)=sm
     sq(i)=sm*sm
  enddo

  call pctile(sq(ia:ib),ib-ia+1,20,sq0)
  rms=sqrt(sq0)
  savg2(ia:ib)=savg2(ia:ib)/(5.0*rms)

  red2=0.
  do i=ia+11,ib-10
     ref=max(savg2(i-10),savg2(i+10))
     red2(i)=savg2(i)-ref
     if(red2(i).lt.-99.0) red2(i)=-99.0
     if(red2(i).gt.99.0) red2(i)=99.0
  enddo

  return
end subroutine sync9
