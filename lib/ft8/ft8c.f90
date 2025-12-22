subroutine ft8c(dd0,newdat,call_1,call_2,grid4,xdt,f1,nharderrors,dmin,  &
     msg37,xsnr)

  use crc
  use timer_module, only: timer
  use packjt77
  include 'ft8_params.f90'
  parameter(NP2=2812)
  character*37 msg37,msg,msgsent,msgbest
  character*12 call_1,call_2
  character*4 grid4
  real a(5)
  real s8(0:7,NN)
  real s2(0:511)
  real bmeta(174),bmetb(174),bmetc(174),bmetd(174)
  real llra(174),llrb(174),llrc(174),llrd(174),llrbest(174)           !Soft symbols
  real dd0(15*12000)
  real ss(9)
  real rcw(174)
  integer*1 cw(174)
  integer*1 msgbits(77)
  integer*1 nxor(174),hdec(174)
  integer itone(NN)
  integer icos7(0:6),ip(1)
  logical one(0:511,0:8)
  integer graymap(0:7)
  integer iloc(1)
  complex cd0(0:3199)
  complex ctwk(32)
  complex csymb(32)
  complex cs(0:7,NN)
  logical std_1,std_2
  logical first,newdat
  data icos7/3,1,4,0,6,5,2/  ! Flipped w.r.t. original FT8 sync array
  data first/.true./
  data graymap/0,1,3,2,5,6,4,7/
  save one

  if(first) then
     one=.false.
     do i=0,511
       do j=0,8
         if(iand(i,2**j).ne.0) one(i,j)=.true.
       enddo
     enddo
     first=.false.
  endif

  call stdcall(call_1,std_1)
  if(call_1(1:3).eq.'CQ ') std_1=.true.
  call stdcall(call_2,std_2)

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
  xdt=(ibest-1)*dt2 - 0.5
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
!  if(nsync .le. 6) return ! bail out

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

!  apmag=maxval(abs(llra))*1.01

  MAXMSG=206
  pbest=0.
  do imsg=1,MAXMSG
     msg=trim(call_1)//' '//trim(call_2)
     i=imsg
     if(call_1(1:3).eq.'CQ ' .and. i.ne.5) msg='QQ0XYZ '//trim(call_2)
     if(.not.std_1) then
        if(i.eq.1 .or. i.ge.6)  msg='<'//trim(call_1)//'> '//trim(call_2)
        if(i.ge.2 .and. i.le.4) msg=trim(call_1)//' <'//trim(call_2)//'>'
     else if(.not.std_2) then
        if(i.le.4 .or. i.eq.6) msg='<'//trim(call_1)//'> '//trim(call_2)
        if(i.ge.7) msg=trim(call_1)//' <'//trim(call_2)//'>'
     endif
     j0=len(trim(msg))+2
     if(i.eq.2) msg(j0:j0+2)='RRR'
     if(i.eq.3) msg(j0:j0+3)='RR73'
     if(i.eq.4) msg(j0:j0+1)='73'
     if(i.eq.5) then
        if(std_2) then
           msg='CQ '//trim(call_2)
           if(call_1(3:3).eq.'_') msg=trim(call_1)//' '//trim(call_2)
           if(grid4.ne.'RR73') msg=trim(msg)//' '//grid4
        endif
        if(.not.std_2) msg='CQ '//trim(call_2)
     endif
     if(i.eq.6 .and. std_2) msg(j0:j0+3)=grid4
     if(i.ge.7) then
        isnr = -50 + (i-7)/2
        if(iand(i,1).eq.1) then
           write(msg(j0:j0+2),'(i3.2)') isnr
           if(msg(j0:j0).eq.' ') msg(j0:j0)='+'
        else
           write(msg(j0:j0+3),'("R",i3.2)') isnr
           if(msg(j0+1:j0+1).eq.' ') msg(j0+1:j0+1)='+'
        endif
     endif

! Source-encode, then get codeword
     i3=-1
     n3=-1
     call genft8(msg,i3,n3,msgsent,msgbits,itone)
     call encode174_91(msgbits,cw)
     rcw=2*cw-1
     pa=sum(llra*rcw)
     pb=sum(llrb*rcw)
     pc=sum(llrc*rcw)
     pd=sum(llrd*rcw)

     if(pa.gt.pbest) then
        pbest=pa
        msgbest=msgsent
        llrbest=llra
        nharderrors=count((2*cw-1)*llra.lt.0.0)
        hdec=0
        where(llra.ge.0.0) hdec=1
        nxor=ieor(hdec,cw)
        dmin=sum(nxor*abs(llra))
     endif
  enddo  ! imsg

!  write(*,4001) pbest,nharderrors,dmin,trim(msgbest)
!4001 format('$$$',f7.1,i4,f7.1,2x,a)
  msg37=msgbest

  return
end subroutine ft8c
