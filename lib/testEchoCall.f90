program testEchoCall

  use, intrinsic :: iso_fortran_env, only: error_unit

  parameter (NSPS=4096,NH=NSPS/2,NZ=3*12000)
  integer*2 iwave(NZ)                    !Raw data, 12000 Hz sample rate
  integer ihdr(11)
  integer*4 itone(6)
  character*120 fname
  character*256 iomsg
  character*6 rxcall,hhmmss,txcall
  character*4 extension
  logical*1 bDiskData,bEchoCall
  logical valid_time
  integer i,ich,ios,name_length,nerrors
  common/echocom/nclearave,nsum,blue(4096),red(4096)
  common/echocom2/fspread_self,fspread_dx

  nerrors=0
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
  bDiskData=.true.
  bEchoCall=.false.
  txcall='      '
  do ifile=1,narg
     call getarg(ifile,fname)
     name_length=len_trim(fname)
     valid_time=.false.
     if(name_length.ge.10) then
        extension=fname(name_length-3:name_length)
        do i=1,len(extension)
           ich=iachar(extension(i:i))
           if(ich.ge.iachar('A') .and. ich.le.iachar('Z')) then
              extension(i:i)=achar(ich + iachar('a') - iachar('A'))
           endif
        enddo
        hhmmss=fname(name_length-9:name_length-4)
        if(extension.eq.'.wav' .and. verify(hhmmss,'0123456789').eq.0) then
           read(hhmmss,'(3i2)',iostat=ios) ih,im,is
           if(ios.eq.0) then
              valid_time=ih.le.23 .and. im.le.59 .and. is.le.59
           endif
        endif
     endif
     if(.not.valid_time) then
        write(error_unit,'(a)') &
             'testEchoCall: filename must end with a valid HHMMSS.wav timestamp: '//trim(fname)
        nerrors=nerrors+1
        cycle
     endif

     open(10,file=trim(fname),access='stream',status='old',action='read', &
          iostat=ios,iomsg=iomsg)
     if(ios.ne.0) then
        write(error_unit,'(a)') &
             'testEchoCall: cannot open '//trim(fname)//': '//trim(iomsg)
        nerrors=nerrors+1
        cycle
     endif

     read(10,iostat=ios,iomsg=iomsg) ihdr,iwave
     if(ios.ne.0) then
        close(10)
        write(error_unit,'(a)') &
             'testEchoCall: cannot read '//trim(fname)//': '//trim(iomsg)
        nerrors=nerrors+1
        cycle
     endif
     close(10)

! Retrieve params known at time of transmissiion and saved in iwave
     call save_echo_params(nDop,nDopAudio,nfrit,f1,fspread,ndf,itone,iwave,-1)
     fspread_self=fspread
     width=fspread

     call avecho(iwave,0,nfrit,nauto,navg,nqual,f1,xlevel,snrdb,db_err,dfreq, &
          width,bDiskData,bEchoCall,txcall,rxcall)
     hour=ih + im/60.0 + is/3600.0
     write(*,1110) hhmmss,hour,xlevel,ndop,fspread,nsum,nqual,nint(dfreq),  &
          snrdb,db_err,rxcall
1110 format(a6,2x,f7.4,2x,f5.2,1x,i7,1x,f7.1,1x,i5,1x,i5,1x,i6,1x,f6.1,1x,f7.1,3x,a6)
     nclearave=0
  enddo

999 if(nerrors.gt.0) stop 1
end program testEchoCall
