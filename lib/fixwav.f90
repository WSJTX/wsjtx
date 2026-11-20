program fixwav

  use, intrinsic :: iso_fortran_env, only: error_unit, input_unit, int64
  use wavhdr
  parameter (NBANDS=23,NMODES=11)
  type(hdr) h
  integer*2 id2(4)
  integer metadata_pos
  integer(int64) data_bytes,npts
  character*8 mode,mode0
  character*6 band
  character*12 arg
  character*80 infile
  character*1 c
  character*9 file_action
  logical ok

  nargs=iargc()
  if(nargs.ne.1 .and. nargs.ne.5) then
     print*,'Inspect: fixwav infile'
     print*,'Edit:    fixwav fMHz mode submode TRperiod infile'
     go to 999
  endif
  call getarg(1,infile)
  file_action='read'
  if(nargs.eq.1) go to 10
  file_action='readwrite'
  read(infile,*) fMHz
  call getarg(2,mode0)
  call getarg(3,arg)
  nsubmode0=-1
  do i=1,8
     if(arg(1:1).eq.char(ichar('A')-1+i)) nsubmode0=i
  enddo
  if(nsubmode0.lt.0) read(arg,*) nsubmode0
  call getarg(4,arg)
  read(arg,*) ntrperiod0
  call getarg(5,infile)

10 open(10,file=infile,status='old',access='stream',action=file_action,iostat=ios)
  if(ios.ne.0) then
     write(error_unit,'(a)') 'Cannot open WAV file.'
     stop 1
  endif
  read(10,iostat=ios) h
  if(ios.ne.0) then
     write(error_unit,'(a)') 'Cannot read WAV header.'
     stop 1
  endif
  if(h%ariff.ne.'RIFF' .or. h%awave.ne.'WAVE' .or. h%afmt.ne.'fmt ' .or. &
       h%lenfmt.ne.16 .or. h%nfmt2.ne.1 .or. h%nchan2.ne.1 .or. &
       h%nbitsam2.ne.16 .or. h%nbytesam2.ne.2 .or. h%adata.ne.'data') then
     write(error_unit,'(a)') 'Unsupported WAV layout: expected fixed-header mono 16-bit PCM.'
     stop 1
  endif
  data_bytes=iand(int(h%ndata,int64),int(z'FFFFFFFF',int64))
  if(data_bytes.lt.8_int64) then
     write(error_unit,'(a)') 'Invalid WAV data size.'
     stop 1
  endif
  npts=data_bytes/2_int64
  nfsample=h%nsamrate
  inquire(unit=10,pos=metadata_pos)
  read(10,iostat=ios) id2
  if(ios.ne.0) then
     write(error_unit,'(a)') 'Cannot read WAV samples.'
     stop 1
  endif
  write(*,1002) h%nchan2,h%nbitsam2,h%nsamrate,npts
1002 format('Channels:',i2,'   Bits/sample:',i3,'   Sample rate:',i6,   &
          '   Npts:',i0)

  call get_wsjtx_wav_params(id2,band,mode,nsubmode,ntrperiod,ok)
  if(nfsample.ne.11025 .and. nfsample.ne.12000) ok=.false.
  if(ok) write(*,1010) band,ntrperiod,mode,char(ichar('A')-1+id2(3))
1010 format('Band: ',a6,'  T/R period:',i4,'   Mode: ',a8,1x,a1)

  if(.not.ok) write(*,'(a)') 'File has no valid WSJT-X params.'
  if(nargs.eq.1) go to 999

  write(*,'(a)',advance='no') 'Do you want to add or change them? (Y/N): '
  read(input_unit,*,iostat=ios) c
  if(ios.ne.0) then
     write(error_unit,'(a)') 'Cannot read confirmation; WAV file unchanged.'
     stop 1
  endif
  if(c.ne.'y' .and. c.ne.'Y') go to 999

  print*,fMHz,mode0,nsubmode0,ntrperiod0
  call set_wsjtx_wav_params(fMHz,mode0,nsubmode0,ntrperiod0,id2)
  band=""
  mode=""
  nsubmode=0
  ntrperiod=0
  call get_wsjtx_wav_params(id2,band,mode,nsubmode,ntrperiod,ok)
  if(.not.ok) then
     write(error_unit,'(a)') 'Invalid replacement WAV metadata; file unchanged.'
     stop 1
  endif
  write(*,1010) band,ntrperiod,mode,char(ichar('A')-1+id2(3))
  write(10,pos=metadata_pos,iostat=ios) id2
  if(ios.ne.0) then
     write(error_unit,'(a)') 'Cannot write WAV metadata.'
     stop 1
  endif

999 end program fixwav
