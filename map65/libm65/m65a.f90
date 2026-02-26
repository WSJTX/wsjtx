subroutine m65a

  use timer_module, only: timer
  use timer_impl, only: init_timer !, limtrace
  use, intrinsic :: iso_c_binding, only: C_NULL_CHAR
  use FFTW3
  
  interface
     function address_m65()
     integer*1, pointer :: address_m65
     end function address_m65
  end interface
  
  integer*1 attach_m65
  integer size_m65
  integer*1, pointer :: p_m65
  character*80 cwd
  character wisfile*256
  logical fileExists

  call getcwd(cwd)
  call ftninit(trim(cwd))
  call init_timer (trim(cwd)//'/timer.out')

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
     i=detach_m65()
     ! Save FFTW wisdom and free memory
     wisfile=trim(cwd)//'/m65_wisdom.dat'// C_NULL_CHAR
     if(len(trim(wisfile)).gt.0) iret=fftwf_export_wisdom_to_filename(wisfile)
     call four2a(a,-1,1,1,1)
     call filbig(a,-1,1,0.0,0,0,0,0,0) !used for FFT plans
     call fftwf_cleanup_threads()
     call fftwf_cleanup()
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
  call sleep_msec(500)          ! wait for .lock to be recreated
  go to 10

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

  include 'njunk.f90'
  real*4 dd(4,5760000),ss(4,322,32768),savg(4,32768)
  real*8 fcenter
  integer nparams0(NJUNK+2),nparams(NJUNK+2)
  logical ldecoded
  character*12 mycall,hiscall
  character*6 mygrid,hisgrid
  character*20 datetime
  common/npar/fcenter,nutc,idphi,mousedf,mousefqso,nagain,              &
       ndepth,ndiskdat,neme,newdat,nfa,nfb,nfcal,nfshift,               &
       mcall3,nkeep,ntol,nxant,nrxlog,nfsample,nxpol,nmode,             &
       ndop00,nsave,max_drift,nhsym,mycall,mygrid,hiscall,hisgrid,       &
       datetime,junk1,junk2
  common/early/nhsym1,nhsym2,ldecoded(32768)
  equivalence (nparams,fcenter)
  
  nparams=nparams0                     !Copy parameters into common/npar/
  npatience=1
  if(nhsym.eq.nhsym1 .and. iand(nrxlog,1).ne.0) then
     write(21,1000) datetime(:17)
1000 format(/'UTC Date: 'a17/78('-'))
     flush(21)
  endif
  if(iand(nrxlog,2).ne.0) rewind(21)
  if(iand(nrxlog,4).ne.0) then
     if(nhsym.eq.nhsym1) rewind(26)
     if(nhsym.eq.nhsym2) backspace(26)
  endif

  nstandalone=0
  if(sum(nparams).ne.0) call decode0(dd,ss,savg,nstandalone)

  return
end subroutine m65c
