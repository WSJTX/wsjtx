subroutine decode0(dd,ss,savg,nstandalone)

  parameter (NSMAX=60*96000)
  parameter (NFFT=32768)

  real*4 dd(4,NSMAX),ss(4,322,NFFT),savg(4,NFFT)
  real*8 fcenter
  integer hist(0:32768)
  character mycall*12,hiscall*12,mygrid*6,hisgrid*6,datetime*20
  character mycall0*12,hiscall0*12,hisgrid0*6
  common/npar/fcenter,nutc,idphi,mousedf,mousefqso,nagain,                &
       ndepth,ndiskdat,neme,newdat,nfa,nfb,nfcal,nfshift,                 &
       mcall3,nkeep,ntol,nxant,nrxlog,nfsample,nxpol,nmode,               &
       nfast,nsave,mycall,mygrid,hiscall,hisgrid,datetime
  common/tracer/ limtrace,lu
  data neme0/-99/,mcall3b/1/
  save

  call timer('decode0 ',0)

  if(newdat.ne.0) then
     nz=52*96000
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
  nhsym=279
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
       mousedf,mousefqso,nagain,ndecdone,ndiskdat,nfshift,ndphi,           &
       nfcal,nkeep,mcall3b,nsum,nsave,nxant,rmsdd,mycall,mygrid,          &
       neme,ndepth,hiscall,hisgrid,nhsym,nfsample,nxpol,nmode)

  call timer('map65a  ',1)
  call timer('decode0 ',1)

  write(*,1010) nsum,nsave
1010 format('<DecodeFinished>',2i4)
  flush(6)

  return
end subroutine decode0
