subroutine ft8b(dd0,newdat,nQSOProgress,nfqso,nftx,ndepth,nzhsym,lapon,     &
     lapcqonly,napwid,lsubtract,nagain,ncontest,iaptype,mycall12,hiscall12, &
     f1,xdt,xbase,apsym,aph10,nharderrors,dmin,nbadcrc,ipass,               &
     msg37,xsnr,itone)

  use crc
  use timer_module, only: timer
  use packjt77
  include 'ft8_params.f90'
  parameter(NP2=2812)
  character*37 msg37
  character*12 mycall12,hiscall12
  character*77 c77
  real a(5)
  real s8(0:7,NN)
  real s2(0:511)
  real bmeta(174),bmetb(174),bmetc(174),bmetd(174)
  real llra(174),llrb(174),llrc(174),llrd(174),llrz(174)           !Soft symbols
  real dd0(15*12000)
  real ss(9)
  integer*1 message77(77),message91(91),apmask(174),cw(174)
  integer apsym(58),aph10(10)
  integer mcq(29),mcqru(29),mcqfd(29),mcqtest(29),mcqww(29)
  integer mrrr(19),m73(19),mrr73(19)
  integer itone(NN)
  integer icos7(0:6),ip(1)
  integer nappasses(0:5)  !Number of decoding passes to use for each QSO state
  integer naptypes(0:5,4) ! (nQSOProgress, decoding pass)  maximum of 4 passes for now
  integer ncontest,ncontest0
  logical one(0:511,0:8)
  integer graymap(0:7)
  integer iloc(1)
  complex cd0(0:3199)
  complex ctwk(32)
  complex csymb(32)
  complex cs(0:7,NN)
  logical first,newdat,lsubtract,lapon,lapcqonly,nagain,unpk77_success
  data icos7/3,1,4,0,6,5,2/  ! Flipped w.r.t. original FT8 sync array
  data     mcq/0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0/
  data   mcqru/0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,1,1,1,1,0,0,1,1,0,0/
  data   mcqfd/0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,1,0,0,1,0,0,0,1,0/
  data mcqtest/0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,1,0,1,0,1,1,1,1,1,1,0,0,1,0/
  data   mcqww/0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,1,1,0,1,1,1,1,0/
  data    mrrr/0,1,1,1,1,1,1,0,1,0,0,1,0,0,1,0,0,0,1/
  data     m73/0,1,1,1,1,1,1,0,1,0,0,1,0,1,0,0,0,0,1/
  data   mrr73/0,1,1,1,1,1,1,0,0,1,1,1,0,1,0,1,0,0,1/
  data first/.true./
  data graymap/0,1,3,2,5,6,4,7/
  save nappasses,naptypes,ncontest0,one

  if(first.or.(ncontest.ne.ncontest0)) then
     mcq=2*mcq-1
     mcqfd=2*mcqfd-1
     mcqru=2*mcqru-1
     mcqtest=2*mcqtest-1
     mcqww=2*mcqww-1
     mrrr=2*mrrr-1
     m73=2*m73-1
     mrr73=2*mrr73-1
     nappasses(0)=2
     nappasses(1)=2
     nappasses(2)=2
     nappasses(3)=4
     nappasses(4)=4
     nappasses(5)=3

! iaptype
!------------------------
!   1        CQ     ???    ???           (29+3=32 ap bits)
!   2        MyCall ???    ???           (29+3=32 ap bits)
!   3        MyCall DxCall ???           (58+3=61 ap bits)
!   4        MyCall DxCall RRR           (77 ap bits)
!   5        MyCall DxCall 73            (77 ap bits)
!   6        MyCall DxCall RR73          (77 ap bits)

     naptypes(0,1:4)=(/1,2,0,0/) ! Tx6 selected (CQ)
     naptypes(1,1:4)=(/2,3,0,0/) ! Tx1
     naptypes(2,1:4)=(/2,3,0,0/) ! Tx2
     naptypes(3,1:4)=(/3,4,5,6/) ! Tx3
     naptypes(4,1:4)=(/3,4,5,6/) ! Tx4
     naptypes(5,1:4)=(/3,1,2,0/) ! Tx5

     one=.false.
     do i=0,511
       do j=0,8
         if(iand(i,2**j).ne.0) one(i,j)=.true.
       enddo
     enddo
     first=.false.
     ncontest0=ncontest
  endif

  dxcall13=hiscall12  ! initialize for use in packjt77
  mycall13=mycall12

  max_iterations=30
  nharderrors=-1
  fs2=12000.0/NDOWN
  dt2=1.0/fs2
  twopi=8.0*atan(1.0)
  delfbest=0.
  ibest=0

  call timer('ft8_down',0)
  call ft8_downsample(dd0,newdat,f1,cd0)   !Mix f1 to baseband and downsample
  call timer('ft8_down',1)

  i0=nint((xdt+0.5)*fs2)                   !Initial guess for start of signal
  smax=0.0
  do idt=i0-10,i0+10                         !Search over +/- one quarter symbol
     call sync8d(cd0,idt,ctwk,0,sync)
     if(sync.gt.smax) then
        smax=sync
        ibest=idt
     endif
  enddo

! Now peak up in frequency
  smax=0.0
  do ifr=-5,5                              !Search over +/- 2.5 Hz
    delf=ifr*0.5
    dphi=twopi*delf*dt2
    phi=0.0
    do i=1,32
      ctwk(i)=cmplx(cos(phi),sin(phi))
      phi=mod(phi+dphi,twopi)
    enddo
    call sync8d(cd0,ibest,ctwk,1,sync)
    if( sync .gt. smax ) then
      smax=sync
      delfbest=delf
    endif
  enddo
  a=0.0
  a(1)=-delfbest
  call twkfreq1(cd0,NP2,fs2,a,cd0)
  f1=f1+delfbest                           !Improved estimate of DF

  call timer('ft8_down',0)
  call ft8_downsample(dd0,.false.,f1,cd0)   !Mix f1 to baseband and downsample
  call timer('ft8_down',1)

  smax=0.0
  do idt=-4,4                         !Search over +/- one quarter symbol
     call sync8d(cd0,ibest+idt,ctwk,0,sync)
     ss(idt+5)=sync
  enddo
  smax=maxval(ss)
  iloc=maxloc(ss)
  ibest=iloc(1)-5+ibest
  xdt=(ibest-1)*dt2
  sync=smax

  do k=1,NN
    i1=ibest+(k-1)*32
    csymb=cmplx(0.0,0.0)
    if( i1.ge.0 .and. i1+31 .le. NP2-1 ) csymb=cd0(i1:i1+31)
    call four2a(csymb,32,1,-1,1)
    cs(0:7,k)=csymb(1:8)/1e3
    s8(0:7,k)=abs(csymb(1:8))
  enddo  

! sync quality check
  is1=0
  is2=0
  is3=0
  do k=1,7
    ip=maxloc(s8(:,k))
    if(icos7(k-1).eq.(ip(1)-1)) is1=is1+1
    ip=maxloc(s8(:,k+36))
    if(icos7(k-1).eq.(ip(1)-1)) is2=is2+1
    ip=maxloc(s8(:,k+72))
    if(icos7(k-1).eq.(ip(1)-1)) is3=is3+1
  enddo
! hard sync sum - max is 21
  nsync=is1+is2+is3
  if(nsync .le. 6) then ! bail out
    nbadcrc=1
    return
  endif

  do nsym=1,3
    nt=2**(3*nsym)
    do ihalf=1,2
      do k=1,29,nsym
        if(ihalf.eq.1) ks=k+7
        if(ihalf.eq.2) ks=k+43
        amax=-1.0
        do i=0,nt-1
          i1=i/64
          i2=iand(i,63)/8
          i3=iand(i,7)
          if(nsym.eq.1) then
            s2(i)=abs(cs(graymap(i3),ks))
          elseif(nsym.eq.2) then
            s2(i)=abs(cs(graymap(i2),ks)+cs(graymap(i3),ks+1))
          elseif(nsym.eq.3) then
            s2(i)=abs(cs(graymap(i1),ks)+cs(graymap(i2),ks+1)+cs(graymap(i3),ks+2))
          else
            print*,"Error - nsym must be 1, 2, or 3."
          endif
        enddo
        i32=1+(k-1)*3+(ihalf-1)*87
        if(nsym.eq.1) ibmax=2 
        if(nsym.eq.2) ibmax=5 
        if(nsym.eq.3) ibmax=8 
        do ib=0,ibmax
          bm=maxval(s2(0:nt-1),one(0:nt-1,ibmax-ib)) - &
             maxval(s2(0:nt-1),.not.one(0:nt-1,ibmax-ib))
          if(i32+ib .gt.174) cycle
          if(nsym.eq.1) then
            bmeta(i32+ib)=bm
            den=max(maxval(s2(0:nt-1),one(0:nt-1,ibmax-ib)), &
                    maxval(s2(0:nt-1),.not.one(0:nt-1,ibmax-ib)))
            if(den.gt.0.0) then
              cm=bm/den
            else ! erase it
              cm=0.0
            endif
            bmetd(i32+ib)=cm
          elseif(nsym.eq.2) then
            bmetb(i32+ib)=bm
          elseif(nsym.eq.3) then
            bmetc(i32+ib)=bm
          endif
        enddo
      enddo
    enddo
  enddo
  call normalizebmet(bmeta,174)
  call normalizebmet(bmetb,174)
  call normalizebmet(bmetc,174)
  call normalizebmet(bmetd,174)

  scalefac=2.83
  llra=scalefac*bmeta
  llrb=scalefac*bmetb
  llrc=scalefac*bmetc
  llrd=scalefac*bmetd

  apmag=maxval(abs(llra))*1.01

! pass #
!------------------------------
!   1        regular decoding, nsym=1 
!   2        regular decoding, nsym=2 
!   3        regular decoding, nsym=3 
!   4        regular decoding, nsym=1, bit-by-bit normalized 
!   5        ap pass 1, nsym=1
!   6        ap pass 2
!   7        ap pass 3
!   8        ap pass 4

  if(lapon.or.ncontest.eq.7) then !Hounds always use AP
     if(.not.lapcqonly) then
        npasses=4+nappasses(nQSOProgress)
     else
        npasses=5 
     endif
  else
     npasses=4
  endif
  if(nzhsym.lt.50) npasses=4
  
  do ipass=1,npasses 
     llrz=llra
     if(ipass.eq.2) llrz=llrb
     if(ipass.eq.3) llrz=llrc
     if(ipass.eq.4) llrz=llrd
     if(ipass.le.4) then
        apmask=0
        iaptype=0
     endif
     if(ipass .gt. 4) then
        llrz=llra
        if(.not.lapcqonly) then
           iaptype=naptypes(nQSOProgress,ipass-4)
        else
           iaptype=1
        endif

! ncontest=0 : NONE
!          1 : NA_VHF
!          2 : EU_VHF
!          3 : FIELD DAY
!          4 : RTTY
!          5 : WW_DIGI 
!          6 : FOX
!          7 : HOUND
!          8 : ARRL_DIGI
!
! Conditions that cause us to bail out of AP decoding
        if(ncontest.le.5 .and. iaptype.ge.3 .and. (abs(f1-nfqso).gt.napwid .and. abs(f1-nftx).gt.napwid) ) cycle
        if(ncontest.eq.6) cycle                     ! No AP for Foxes
        if(ncontest.eq.7.and.f1.gt.950.0) cycle     ! Hounds use AP only for signals below 950 Hz
        if(iaptype.ge.2 .and. apsym(1).gt.1) cycle  ! No, or nonstandard, mycall 
        if(ncontest.eq.7 .and. iaptype.ge.2 .and. aph10(1).gt.1) cycle
        if(iaptype.ge.3 .and. apsym(30).gt.1) cycle ! No, or nonstandard, dxcall

        if(iaptype.eq.1) then ! CQ or CQ RU or CQ TEST or CQ FD
           apmask=0
           apmask(1:29)=1  
           if(ncontest.eq.0) llrz(1:29)=apmag*mcq(1:29)
           if(ncontest.eq.1) llrz(1:29)=apmag*mcqtest(1:29)
           if(ncontest.eq.2) llrz(1:29)=apmag*mcqtest(1:29)
           if(ncontest.eq.3) llrz(1:29)=apmag*mcqfd(1:29)
           if(ncontest.eq.4) llrz(1:29)=apmag*mcqru(1:29)
           if(ncontest.eq.5) llrz(1:29)=apmag*mcqww(1:29)
           if(ncontest.eq.7) llrz(1:29)=apmag*mcq(1:29)
           if(ncontest.eq.8) llrz(1:29)=apmag*mcqtest(1:29)
           apmask(75:77)=1 
           llrz(75:76)=apmag*(-1)
           llrz(77)=apmag*(+1)
        endif

        if(iaptype.eq.2) then ! MyCall,???,??? 
           apmask=0
           if(ncontest.eq.0.or.ncontest.eq.1.or.ncontest.eq.5.or.ncontest.eq.8) then
              apmask(1:29)=1  
              llrz(1:29)=apmag*apsym(1:29)
              apmask(75:77)=1 
              llrz(75:76)=apmag*(-1)
              llrz(77)=apmag*(+1)
           else if(ncontest.eq.2) then
              apmask(1:28)=1  
              llrz(1:28)=apmag*apsym(1:28)
              apmask(72:74)=1
              llrz(72)=apmag*(-1)
              llrz(73)=apmag*(+1)
              llrz(74)=apmag*(-1)
              apmask(75:77)=1 
              llrz(75:77)=apmag*(-1)
           else if(ncontest.eq.3) then
              apmask(1:28)=1  
              llrz(1:28)=apmag*apsym(1:28)
              apmask(75:77)=1 
              llrz(75:77)=apmag*(-1)
           else if(ncontest.eq.4) then
              apmask(2:29)=1  
              llrz(2:29)=apmag*apsym(1:28)
              apmask(75:77)=1 
              llrz(75)=apmag*(-1)
              llrz(76:77)=apmag*(+1)
           else if(ncontest.eq.7) then ! ??? RR73; MyCall <Fox Call hash10> ???
              apmask(29:56)=1  
              llrz(29:56)=apmag*apsym(1:28)
              apmask(57:66)=1
              llrz(57:66)=apmag*aph10(1:10)
              apmask(72:77)=1 
              llrz(72:73)=apmag*(-1)
              llrz(74)=apmag*(+1)
              llrz(75:77)=apmag*(-1)
           endif
        endif

        if(iaptype.eq.3) then ! MyCall,DxCall,??? 
           apmask=0
           if(ncontest.eq.0.or.ncontest.eq.1.or.ncontest.eq.2.or.ncontest.eq.5.or.ncontest.eq.7.or.ncontest.eq.8) then
              apmask(1:58)=1  
              llrz(1:58)=apmag*apsym
              apmask(75:77)=1 
              llrz(75:76)=apmag*(-1)
              llrz(77)=apmag*(+1)
           else if(ncontest.eq.3) then ! Field Day
              apmask(1:56)=1  
              llrz(1:28)=apmag*apsym(1:28)
              llrz(29:56)=apmag*apsym(30:57)
              apmask(72:74)=1 
              apmask(75:77)=1 
              llrz(75:77)=apmag*(-1)
           else if(ncontest.eq.4) then 
              apmask(2:57)=1  
              llrz(2:29)=apmag*apsym(1:28)
              llrz(30:57)=apmag*apsym(30:57)
              apmask(75:77)=1 
              llrz(75)=apmag*(-1)
              llrz(76:77)=apmag*(+1)
           endif
        endif

        if(iaptype.eq.5.and.ncontest.eq.7) cycle !Hound
        if(iaptype.eq.4 .or. iaptype.eq.5 .or. iaptype.eq.6) then  
           apmask=0
           if(ncontest.le.5 .or. (ncontest.eq.7.and.iaptype.eq.6) .or. ncontest.eq.8) then
              apmask(1:77)=1   ! mycall, hiscall, RRR|73|RR73
              llrz(1:58)=apmag*apsym
              if(iaptype.eq.4) llrz(59:77)=apmag*mrrr 
              if(iaptype.eq.5) llrz(59:77)=apmag*m73 
              if(iaptype.eq.6) llrz(59:77)=apmag*mrr73 
           else if(ncontest.eq.7.and.iaptype.eq.4) then ! Hound listens for MyCall RR73;...
              apmask(1:28)=1
              llrz(1:28)=apmag*apsym(1:28)
              apmask(57:66)=1
              llrz(57:66)=apmag*aph10(1:10)
              apmask(72:77)=1
              llrz(72:73)=apmag*(-1)
              llrz(74)=apmag*(1)
              llrz(75:77)=apmag*(-1)
           endif             
        endif
     endif

     cw=0
     dmin=0.0
     norder=2
     maxosd=2
     if(ndepth.eq.1) maxosd=-1  ! BP only
     if(ndepth.eq.2) maxosd=0   ! uncoupled BP+OSD
     if(ndepth.eq.3 .and.         &
        (abs(nfqso-f1).le.napwid .or. abs(nftx-f1).le.napwid .or. ncontest.eq.7)) then
        maxosd=2
     endif
     call timer('dec174_91 ',0)
     Keff=91
     call decode174_91(llrz,Keff,maxosd,norder,apmask,message91,cw,  &
                       ntype,nharderrors,dmin)
     if(nharderrors.ge.0) message77=message91(1:77)
     call timer('dec174_91 ',1)

     msg37='                                     '
     nbadcrc=1
     if(nharderrors.lt.0 .or. nharderrors.gt.36) cycle
     if(count(cw.eq.0).eq.174) cycle           !Reject the all-zero codeword
     write(c77,'(77i1)') message77
     read(c77(72:74),'(b3)') n3
     read(c77(75:77),'(b3)') i3
     if(i3.gt.5 .or. (i3.eq.0.and.n3.gt.6)) cycle
     if(i3.eq.0 .and. n3.eq.2) cycle
     call unpack77(c77,1,msg37,unpk77_success)
     if(.not.unpk77_success) cycle
     nbadcrc=0  ! If we get this far: valid codeword, valid (i3,n3), nonquirky message.
     call get_ft8_tones_from_77bits(message77,itone)
     if(lsubtract) then
        call timer('sub_ft8a',0)
        call subtractft8(dd0,itone,f1,xdt,.false.)
        call timer('sub_ft8a',1)
     endif
     xsig=0.0
     xnoi=0.0
     do i=1,79
        xsig=xsig+s8(itone(i),i)**2
        ios=mod(itone(i)+4,7)
        xnoi=xnoi+s8(ios,i)**2
     enddo
     xsnr=0.001
     xsnr2=0.001
     arg=xsig/xnoi-1.0 
     if(arg.gt.0.1) xsnr=arg
     arg=xsig/xbase/3.0e6-1.0
     if(arg.gt.0.1) xsnr2=arg
     xsnr=10.0*log10(xsnr)-27.0
     xsnr2=10.0*log10(xsnr2)-27.0
     if(.not.nagain) then
       xsnr=xsnr2
    endif
    if(nsync.le.10 .and. xsnr.lt.-24.0) then    !bail out, likely false decode
       nbadcrc=1
       return
    endif
    if(xsnr .lt. -24.0) xsnr=-25.0
    return
  enddo
  return
end subroutine ft8b

subroutine normalizebmet(bmet,n)
  real bmet(n)

  bmetav=sum(bmet)/real(n)
  bmet2av=sum(bmet*bmet)/real(n)
  var=bmet2av-bmetav*bmetav
  if( var .gt. 0.0 ) then
     bmetsig=sqrt(var)
  else
     bmetsig=sqrt(bmet2av)
  endif
  bmet=bmet/bmetsig
  return
end subroutine normalizebmet


function bessi0(x) 
! From Numerical Recipes
   real bessi0,x
   double precision p1,p2,p3,p4,p5,p6,p7,q1,q2,q3,q4,q5,q6,q7,q8,q9,y
   save p1,p2,p3,p4,p5,p6,p7,q1,q2,q3,q4,q5,q6,q7,q8,q9
   data p1,p2,p3,p4,p5,p6,p7/1.0d0,3.5156229d0,3.0899424d0,1.2067492d0, &
      0.2659732d0,0.360768d-1,0.45813d-2/
   data q1,q2,q3,q4,q5,q6,q7,q8,q9/0.39894228d0,0.1328592d-1,           &
      0.225319d-2,-0.157565d-2,0.916281d-2,-0.2057706d-1,               &
      0.2635537d-1,-0.1647633d-1,0.392377d-2/

   if (abs(x).lt.3.75) then 
      y=(x/3.75)**2
      bessi0=p1+y*(p2+y*(p3+y*(p4+y*(p5+y*(p6+y*p7))))) 
   else
      ax=abs(x)
      y=3.75/ax 
      bessi0=(exp(ax)/sqrt(ax))*(q1+y*(q2+y*(q3+y*(q4         &
           +y*(q5+y*(q6+y*(q7+y*(q8+y*q9))))))))
   endif
   return
end function bessi0
