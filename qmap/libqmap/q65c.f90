subroutine q65c

  use timer_module, only: timer
  use timer_impl, only: fini_timer !, limtrace
  use, intrinsic :: iso_c_binding, only: C_NULL_CHAR
  use FFTW3
  use q65
  use q65_decode

  parameter (NFFT=32768)
  include 'njunk.f90'
  real*8 fcenter
  real*4 pdb(4)
  integer nparams0(NJUNK+3),nparams(NJUNK+3)
!  integer values(8)
  logical first
  logical*1 bAlso30
  character*120 fname
  character*22 revision
  character*12 mycall,hiscall
  character*6 mygrid,hisgrid
  character*20 datetime
  character*64 result
  common/decodes/ndecodes,ncand2,nQDecoderDone,nWDecoderBusy,              &
       nWTransmitting,kHzRequested,result(50)
  common/datcom2/dd(2,5760000),ss(400,NFFT),savg(NFFT),nparams0
  common/savecom/revision,fname
!### REMEMBER that /npar/ is not updated until nparams=nparams0 is executed. ###
  common/npar/fcenter,nutc,fselected,mousedf,mousefqso,nagain,            &
       ndepth,ndiskdat,ntx60,newdat,nn1,nn2,nfcal,nfshift,                 &
       ntx30a,ntx30b,ntol,n60,nCFOM,nfsample,ndop58,nmode,               &
       ndop00,nsave,nn3,nn4,nhsym,mycall,mygrid,hiscall,hisgrid,      &
       datetime,junk1,junk2,bAlso30
  equivalence (nparams,fcenter)
  data first/.true./
  save first

  nparams=nparams0                     !Copy parameters into common/npar/
  datetime(12:)='00       '
  npatience=1
  newdat=1                          !Always on ??

  if(ndiskdat.eq.1) then
     call chkstat(dd,nhsym,pdb)
     if((abs(pdb(1)-pdb(2)).gt.3.0 .and. pdb(1).gt.1.0) .or.  &
          pdb(1).lt.1.0) ntx30a=20                               !Tx 1st half
     if((abs(pdb(3)-pdb(4)).gt.3.0 .and. pdb(3).gt.1.0) .or.  &
          pdb(3).lt.1.0) ntx30b=20                               !Tx 2nd half
     if(pdb(4).lt.0.04) then
        ntx30a=0  !Older 56s files have no Tx
        ntx30b=0  !Older 56s files have no Tx
     endif
  endif

  if(ntx30a.gt.5) then
     dd(1:2,1:30*96000)=0.
     ss(1:200,1:NFFT)=0.
     do i=1,NFFT
        savg(i)=sum(ss(201:400,i))
     enddo
  endif
  if(ntx30b.gt.5) then
     dd(1:2,30*96000+1:60*96000)=0.
     ss(201:400,1:NFFT)=0.
     do i=1,NFFT
        savg(i)=sum(ss(1:200,i))
     enddo
  endif

  if(nhsym.gt.200 .and. ntx30b.gt.5) go to 10
  call timer('decode0 ',0)
  call decode0(dd,ss,savg)
  call timer('decode0 ',1)

10 continue
!  call date_and_time(VALUES=values)
!  n60b=values(7)
!  nd=n60b-n60
!  if(nd.lt.0) nd=nd+60
!  write(*,3002) nutc,nagain,nhsym,n60,n60b,nd,ntx30a,ntx30b,ndecodes,  &
!       nsave,revision
!3002 format('A',i5.4,i3,i5,7i4,1x,a22)
!  flush(6)

  if(ndiskdat.eq.0) then
     if(nhsym.eq.390 .and.                                                   &
          (nsave.eq.2 .or. (nsave.eq.1 .and. ndecodes.ge.1))) then
        call save_qm(fname,revision,mycall,mygrid,dd,ntx30a,ntx30b,fcenter,  &
             nutc,ndop00,ndop58)
     endif
  endif

  return
end subroutine q65c

subroutine all_done

  use timer_module, only: timer
  use timer_impl, only: fini_timer

  call timer('decode0 ',101)
  call fini_timer

  return
end subroutine all_done
