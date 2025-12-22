subroutine ft8q3(cd,xdt,f0,call_1,call_2,grid4,msgbest,snr)

! Get q3-style decodes for FT8.

  use packjt77
  parameter(NN=79,NSPS=32)
  parameter(NWAVE=NN*NSPS)               !2528
  parameter(NZ=3200,NLAGS=NZ-NWAVE)
  character*12 call_1,call_2
  character*4 grid4
  character*37 msg,msgbest,msgsent
  character c77*77
  complex cwave(0:NWAVE-1)
  complex cd(0:NZ-1)
  complex z
  real xjunk(NWAVE)
  real ccf(0:NLAGS-1)
  real ccfmsg(206)
  integer itone(NN)
  integer*1 msgbits(77)
  logical std_1,std_2

  if(xdt.eq.-99.0) return                !Silence compiler warning
  call stdcall(call_1,std_1)
  call stdcall(call_2,std_2)

  fs=200.0                               !Sample rate (Hz)
  dt=1.0/fs                              !Sample interval (s)
  bt=2.0
  ccfbest=0.
  lagbest=-1

  do imsg=1,206
     msg=trim(call_1)//' '//trim(call_2)
     i=imsg
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
        if(std_2) msg='CQ '//trim(call_2)//' '//grid4
        if(.not.std_2) msg='CQ '//trim(call_2)
     endif
     if(i.eq.6 .and. std_2) msg(j0:j0+3)=grid4
     if(i.ge.7 .and. i.le.206) then
        isnr = -50 + (i-7)/2
        if(iand(i,1).eq.1) then
           write(msg(j0:j0+2),'(i3.2)') isnr
           if(msg(j0:j0).eq.' ') msg(j0:j0)='+'
        else
           write(msg(j0:j0+3),'("R",i3.2)') isnr
           if(msg(j0+1:j0+1).eq.' ') msg(j0+1:j0+1)='+'
        endif
     endif

! Source-encode, then get itone()
     i3=-1
     n3=-1
     call pack77(msg,i3,n3,c77)
     call genft8(msg,i3,n3,msgsent,msgbits,itone)
! Generate complex cwave
     call gen_ft8wave(itone,NN,NSPS,bt,fs,f0,cwave,xjunk,1,NWAVE)

     lagmax=-1
     ccfmax=0.
     nsum=32*2
     do lag=0,nlags-1
        z=0.
        s=0.
        do i=0,NWAVE-1
           z=z + cd(i+lag)*conjg(cwave(i))
           if(mod(i,nsum).eq.nsum-1 .or. i.eq.NWAVE-1) then
              s=s + abs(z)
              z=0.
           endif
        enddo
        ccf(lag)=s
        if(ccf(lag).gt.ccfmax) then
           ccfmax=ccf(lag)
           lagmax=lag
        endif
     enddo ! lag
     ccfmsg(imsg)=ccfmax
     if(ccfmax.gt.ccfbest) then
        ccfbest=ccfmax
        lagbest=lagmax
        msgbest=msg
     endif
  enddo  ! imsg

  call pctile(ccfmsg,207,50,base)
  call pctile(ccfmsg,207,67,sigma)
  sigma=sigma-base
  ccfmsg=(ccfmsg-base)/sigma
!  do imsg=1,207
!     write(44,3044) imsg,ccfmsg(imsg)
!3044 format(i5,f10.3)
!  enddo
  snr=maxval(ccfmsg)

  return
end subroutine ft8q3
