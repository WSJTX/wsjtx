subroutine decode0(dd,ss,savg,nstandalone)

  use timer_module, only: timer
  parameter (NSMAX=60*96000)

  real*4 dd(4,NSMAX),ss(4,322,NFFT),savg(4,NFFT)
  real*8 fcenter
  integer hist(0:32768)
  logical ldecoded
  character mycall*12,hiscall*12,mygrid*6,hisgrid*6,datetime*20
  character mycall0*12,hiscall0*12,hisgrid0*6
  common/npar/fcenter,nutc,idphi,mousedf,mousefqso,nagain,                &
       ndepth,ndiskdat,neme,newdat,nfa,nfb,nfcal,nfshift,                 &
       mcall3,nkeep,ntol,nxant,nrxlog,nfsample,nxpol,nmode,               &
       nfast,nsave,max_drift,nhsym,mycall,mygrid,hiscall,hisgrid,datetime
  common/early/nhsym1,nhsym2,ldecoded(32768)
  common/decodes/ndecodes
  data neme0/-99/,mcall3b/1/
  save

  call sec0(0,tquick)
  call timer('decode0 ',0)
  if(newdat.ne.0) then
     nz=96000*nhsym/5.3833
     hist=0
     do i=1,nz
        j1=min(abs(dd(1,i)),32768.0)
        hist(j1)=hist(j1)+1
        j2=min(abs(dd(2,i)),32768.0)
        hist(j2)=hist(j2)+1
        j3=min(abs(dd(3,i)),32768.0)
        hist(j3)=hist(j3)+1
        j4=min(abs(dd(4,i)),32768.0)
        hist(j4)=hist(j4)+1
     enddo
     m=0
     do i=0,32768
        m=m+hist(i)
        if(m.ge.2*nz) go to 10
     enddo
10   rmsdd=1.5*i
  endif
  ndphi=0
  if(iand(nrxlog,8).ne.0) ndphi=1

  if(mycall.ne.mycall0 .or. hiscall.ne.hiscall0 .or.         &
       hisgrid.ne.hisgrid0 .or. mcall3.ne.0 .or. neme.ne.neme0) mcall3b=1
      
  mycall0=mycall
  hiscall0=hiscall
  hisgrid0=hisgrid
  neme0=neme

  call timer('map65a  ',0)
  call map65a(dd,ss,savg,newdat,nutc,fcenter,ntol,idphi,nfa,nfb,           &
       mousedf,mousefqso,nagain,ndecdone,nfshift,ndphi,max_drift,          &
       nfcal,nkeep,mcall3b,nsum,nsave,nxant,mycall,mygrid,                 &
       neme,ndepth,nstandalone,hiscall,hisgrid,nhsym,nfsample,             &
       ndiskdat,nxpol,nmode)
  call timer('map65a  ',1)
  call timer('decode0 ',1)

  call sec0(1,tdec)
  if(nhsym.eq.nhsym1) write(*,1010) nsum,nsave,nstandalone,nhsym,tdec
1010 format('<EarlyFinished>',3i4,i6,f6.2)
  if(nhsym.eq.nhsym2) write(*,1012) nsum,nsave,nstandalone,nhsym,tdec,ndecodes
1012 format('<DecodeFinished>',3i4,i6,f6.2,i5)
  flush(6)

  return
end subroutine decode0
