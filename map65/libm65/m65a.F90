subroutine m65a

  interface
     function address_m65()
     integer*1, pointer :: address_m65
     end function address_m65
  end interface
  
  integer*1 attach_m65,lock_m65,unlock_m65
  integer size_m65
  integer*1, pointer :: p_m65
  character*80 cwd
  logical fileExists
  common/tracer/limtrace,lu

  call getcwd(cwd)
  call ftninit(trim(cwd))
  limtrace=0
  lu=12
  i1=attach_m65()

10 inquire(file=trim(cwd)//'/.lock',exist=fileExists)
  if(fileExists) then
     call sleep_msec(100)
     go to 10
  endif

  inquire(file=trim(cwd)//'/.quit',exist=fileExists)
  if(fileExists) then
     call timer('decode0 ',101)
     call ftnquit
     i=detach_m65()
     go to 999
  endif
  
  nbytes=size_m65()
  if(nbytes.le.0) then
     print*,'m65a: Shared memory mem_m65 does not exist.' 
     print*,'Program m65a should be started automatically from within map65.'
     go to 999
  endif
  p_m65=>address_m65()
  call m65b(p_m65,nbytes)

100 inquire(file=trim(cwd)//'/.lock',exist=fileExists)
  if(fileExists) go to 10
  call sleep_msec(100)
  go to 100

999 return
end subroutine m65a

subroutine m65b(m65com,nbytes)
  integer*1 m65com(0:nbytes-1)
  kss=4*4*60*96000
  ksavg=kss+4*4*322*32768
  kfcenter=ksavg+4*4*32768
 call m65c(m65com(0),m65com(kss),m65com(ksavg),m65com(kfcenter))
  return
end subroutine m65b

subroutine m65c(dd,ss,savg,nparams0)
  integer*1 detach_m65
  real*4 dd(4,5760000),ss(4,322,32768),savg(4,32768)
  real*8 fcenter
  integer nparams0(40),nparams(40)
  character*12 mycall,hiscall
  character*6 mygrid,hisgrid
  character*20 datetime
  common/npar/fcenter,nutc,idphi,mousedf,mousefqso,nagain,              &
       ndepth,ndiskdat,neme,newdat,nfa,nfb,nfcal,nfshift,               &
       mcall3,nkeep,ntol,nxant,nrxlog,nfsample,nxpol,nmode,             &
       nfast,nsave,mycall,mygrid,hiscall,hisgrid,datetime
  equivalence (nparams,fcenter)
  
  nparams=nparams0                     !Copy parameters into common/npar/
  npatience=1
  if(iand(nrxlog,1).ne.0) then
     write(21,1000) datetime(:17)
1000 format(/'UTC Date: 'a17/78('-'))
     flush(21)
  endif
  if(iand(nrxlog,2).ne.0) rewind 21
  if(iand(nrxlog,4).ne.0) rewind 26

  nstandalone=0
  if(sum(nparams).ne.0) call decode0(dd,ss,savg,nstandalone)

  return
end subroutine m65c
