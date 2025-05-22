program testEchoCall

  parameter (NSPS=4096,NH=NSPS/2,NZ=3*12000)
  integer*2 iwave(NZ)                    !Raw data, 12000 Hz sample rate
  integer ihdr(11)
  integer*1 itone(6)
  character*120 fname
  character*6 rxcall,hhmmss
  common/echocom/nclearave,nsum,blue(4096),red(4096)
  common/echocom2/fspread_self,fspread_dx

  narg=iargc()
  if(narg.lt.1) then
     print*,'Usage: testEchoCall fname1 [fname2, ...]'
     go to 999
  endif

  write(*,1000) 
1000 format('  UTC     Hour   Level  Doppler  Width     N     Q     DF' &
            '    SNR   dBerr   Message'/82('-'))

  nclearave=1
  navg=10
  nauto=1
  nqual=0
  do ifile=1,narg
     call getarg(ifile,fname)
     open(10,file=trim(fname),access='stream',status='unknown')
     read(10) ihdr,iwave
     close(10)

! Retrieve params known at time of transmissiion and saved in iwave
     call save_echo_params(nDop,nDopAudio,nfrit,f1,fspread,ndf,itone,iwave,-1)
     fspread_self=fspread

     call avecho(iwave,0,nfrit,ndf,nauto,navg,nqual,f1,xlevel,snrdb,db_err,dfreq, &
          width,bDiskData,bEchoCall,txcall,rxcall)
     i1=index(fname,'.wav')
     hhmmss=fname(i1-6:i1-1)
     read(hhmmss,'(3i2)') ih,im,is
     hour=ih + im/60.0 + is/3600.0
     write(*,1110) hhmmss,hour,xlevel,ndop,fspread,nsum,nqual,nint(dfreq),  &
          snrdb,db_err,rxcall
1110 format(a6,2x,f7.4,2x,f5.2,1x,i7,1x,f7.1,1x,i5,1x,i5,1x,i6,1x,f6.1,1x,f7.1,3x,a6)
     nclearave=0
  enddo

999 end program testEchoCall
